// osu!'s menu cursor (MenuCursorContainer): the arrow from osu-resources in
// place of the system cursor, drawn above everything, even other mods' ImGui
// menus (Eclipse): it's drawn last, right before the frame is shown. It shrinks and glows
// pink while a button is held, turns to follow a drag, springs back on
// release, and taps. It shows wherever the system cursor would show: the
// system one is kept hidden over the window (Win32's ShowCursor, which GLFW
// never touches) and the cursor GLFW set, which GD and other mods change
// through CCEGLView or GLFW itself, says whether it would be showing: none
// at all while hidden (gameplay) or disabled (GD's "lock cursor in game").
// That way a mod's menu over gameplay (QOLMod's, Eclipse's) leaves the
// cursor the way vanilla would, whatever it does to put it back.
// PC only: phones have no pointer.

#include "MenuCursor.hpp"

#include <Geode/Geode.hpp>

#ifdef GEODE_IS_WINDOWS

#include <Geode/modify/CCEGLView.hpp>
#include <Geode/modify/GameManager.hpp>
#include <Geode/modify/MenuLayer.hpp>

#include "../../audio/Sfx.hpp"
#include "Easing.hpp"
#include "Quips.hpp"
#include "RoundedBox.hpp"
#include "Text.hpp"
#include "Theme.hpp"

#include <Windows.h>

#include <algorithm>
#include <array>
#include <ctime>
#include <cmath>
#include <string>
#include <vector>

using namespace geode::prelude;

namespace lazer {

namespace {
    bool g_enabled = false;
    // The system cursor is hidden over the window while ours draws (a
    // per-thread count in Windows: kept balanced).
    bool g_systemHidden = false;

    void hideSystemCursor(bool hide) {
        if (hide == g_systemHidden) return;
        g_systemHidden = hide;
        ShowCursor(hide ? FALSE : TRUE);
    }

    bool settingEnabled() { return Mod::get()->getSettingValue<bool>("custom-cursor"); }
    constexpr float BASE_SCALE = 0.15f;              // Cursor.base_scale
    constexpr float TILT_MAX = 30.f;                 // degrees, moving tilt
    constexpr ccColor3B PINK {255, 102, 170};        // OsuColour.Pink
    // The arrow's tip in the (unpadded) 312 x 442 texture: the click point.
    constexpr float TIP_X = 16.f, TIP_Y = 6.f, TEX_W = 312.f, TEX_H = 442.f;

    CCSprite* cursorSprite(char const* file) {
        auto texture = CCTextureCache::get()->addImage(file, false);
        if (!texture) return nullptr;
        // The images are padded to 512 x 512 so they can be mipmapped: drawn at
        // ~15% of their size, plain linear filtering would make them jagged.
        texture->generateMipmap();
        ccTexParams params {GL_LINEAR_MIPMAP_LINEAR, GL_LINEAR, GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE};
        texture->setTexParameters(&params);
        float csf = CC_CONTENT_SCALE_FACTOR();
        auto sprite = CCSprite::createWithTexture(texture, {0, 0, TEX_W / csf, TEX_H / csf});
        sprite->setAnchorPoint({TIP_X / TEX_W, 1 - TIP_Y / TEX_H});
        return sprite;
    }

    enum class Drag { None, Started, Rotating };

    // Shaking it left and right gets it talking, more each time you keep going.
    // Past the last line it means it: a second after that fades, it switches
    // itself off (the custom cursor setting), and the system cursor is back.
    constexpr std::array<char const*, 10> NAGS {{
        "hey, stop!",
        "i said stop",
        "that's not funny",
        "i'm getting dizzy...",
        "ok, you're doing it on purpose",
        "i'm not a toy!",
        "you know what, fine",
        "...are you done?",
        "i'm telling the logo about this",
        "that's it. i'm out.",
    }};
    // And spinning it round until it has to spin all the way back.
    constexpr std::array<char const*, 3> DIZZY {{
        "woah, don't do it again",
        "i mean it. no more spinning",
        "...i think i'm gonna be sick",
    }};
    constexpr float SHAKE_SPEED = 450.f;    // px/s, a swing has to be at least this fast
    constexpr float SHAKE_TRAVEL = 40.f;    // px, and this long
    constexpr int SHAKE_TURNS = 6;          // direction changes within a second
    constexpr float DIZZY_TURN = 300.f;     // degrees wound up at release
    constexpr float DIZZY_S = 1.3f;         // how long the wobble lasts

    // A little speech bubble beside the cursor: pops in, fades after a while.
    class Bubble : public CCNode {
    public:
        static Bubble* create() {
            auto ret = new Bubble();
            if (ret->init()) {
                ret->autorelease();
                return ret;
            }
            delete ret;
            return nullptr;
        }

        bool init() override {
            if (!CCNode::init()) return false;
            float k = unitScale();
            m_bg = RoundedBox::create({10, 10}, 8 * k, {0x22, 0x1a, 0x21, 235});
            m_bg->setAnchorPoint({0, 0});
            this->addChild(m_bg);
            m_label = makeText(" ", Weight::SemiBold, 13 * k);
            m_label->setAnchorPoint({0, 0.5f});
            this->addChild(m_label, 1);
            this->setVisible(false);
            return true;
        }

        void say(std::string const& text) {
            float k = unitScale();
            m_label->setString(text.c_str());
            auto size = m_label->getScaledContentSize();
            float padX = 9 * k, padY = 5 * k;
            CCSize box {size.width + padX * 2, size.height + padY * 2};
            m_bg->setContentSize(box);
            m_bg->setRadius(std::min(8 * k, box.height / 2));
            m_label->setPosition({padX, box.height / 2});
            this->setContentSize(box);
            this->setAnchorPoint({0, 0});
            this->setVisible(true);
            m_scale.set(0.6f);
            m_scale.to(1, 500, Easing::OutElasticHalf);
            m_alpha.to(1, 150, Easing::OutQuint);
            m_left = showFor(text);
        }

        // How long a line stays before it fades (300 ms more).
        static float showFor(std::string const& text) { return 2.2f + text.size() * 0.03f; }
        static constexpr float FADE_S = 0.3f;

        void tick(float dt) {
            if (!this->isVisible()) return;
            m_left -= dt;
            if (m_left <= 0 && m_alpha.target() > 0) {
                m_alpha.to(0, 300, Easing::OutQuint);
                m_scale.to(0.8f, 300, Easing::OutQuint);
            }
            m_alpha.update(dt);
            m_scale.update(dt);
            float a = std::clamp(m_alpha.get(), 0.f, 1.f);
            this->setScale(m_scale.get());
            m_bg->setFillColor({0x22, 0x1a, 0x21, GLubyte(235 * a)});
            m_label->setOpacity(GLubyte(255 * a));
            if (m_left <= 0 && a <= 0.001f) this->setVisible(false);
        }

    private:
        RoundedBox* m_bg = nullptr;
        CCLabelBMFont* m_label = nullptr;
        Tweened<float> m_alpha {0.f};
        Tweened<float> m_scale {1.f};
        float m_left = 0;
    };

    class MenuCursor : public CCNode {
    public:
        void say(std::string const& text) {
            if (m_bubble) m_bubble->say(text);
        }

        static MenuCursor* create() {
            auto ret = new MenuCursor();
            if (ret->init()) {
                ret->autorelease();
                return ret;
            }
            delete ret;
            return nullptr;
        }

        bool init() override {
            if (!CCNode::init()) return false;
            m_holder = CCNode::create();
            this->addChild(m_holder);
            m_base = cursorSprite("menu-cursor.png"_spr);
            m_additive = cursorSprite("menu-cursor-additive.png"_spr);
            if (!m_base || !m_additive) return false;
            m_holder->addChild(m_base);
            m_additive->setColor(PINK);
            m_additive->setOpacity(0);
            m_additive->setBlendFunc({GL_ONE, GL_ONE}); // additive (the texture is premultiplied)
            m_holder->addChild(m_additive);
            // Up and to the right of the tip (the arrow hangs below it).
            m_bubble = Bubble::create();
            if (m_bubble) {
                float k = unitScale();
                m_bubble->setPosition({12 * k, 10 * k});
                this->addChild(m_bubble, 1);
            }
            return true;
        }

        void visit() override {
            float dt = CCDirector::get()->getDeltaTime();
            float ms = dt * 1000.f;

            bool enabled = settingEnabled();
            if (enabled != g_enabled) {
                g_enabled = enabled;
                hideSystemCursor(enabled);
            }
            if (!enabled) return;

            auto view = CCEGLView::get();
            float pxPerPoint = view->getScaleX();

            // Mouse position from Windows, in the game window's client area.
            HWND window = WindowFromDC(wglGetCurrentDC());
            POINT p;
            RECT client;
            bool inside = false;
            CCPoint pos = m_lastPos;
            if (window && GetCursorPos(&p) && ScreenToClient(window, &p) && GetClientRect(window, &client)
                && client.right > 0 && client.bottom > 0) {
                inside = p.x >= 0 && p.y >= 0 && p.x < client.right && p.y < client.bottom;
                auto size = CCDirector::get()->getWinSize();
                pos = CCPoint(p.x / float(client.right) * size.width, (1 - p.y / float(client.bottom)) * size.height);
            }
            bool focused = window && GetForegroundWindow() == window;

            // Visibility: whether the system cursor would be showing, and only
            // over the window (outside, the system cursor is back). GLFW hides
            // it (its hidden mode, for gameplay, and its disabled mode, GD's
            // "lock cursor in game" pinning it to the middle of the window) by
            // setting no cursor at all: a null current cursor. ShowCursor's
            // count, ours, leaves that alone.
            // Another mod may hide it with ShowCursor as well: its count,
            // less our own.
            HCURSOR current = GetCursor();
            int count = ShowCursor(FALSE) + 1;
            ShowCursor(TRUE);
            int others = count - (g_systemHidden ? -1 : 0);
            bool shown = current != nullptr && others >= 0;
            bool visible = shown && inside;
            if (visible != m_visible) {
                m_visible = visible;
                if (visible) { // PopIn
                    m_alpha.to(1, 250, Easing::OutQuint);
                    m_scale.to(1, 400, Easing::OutQuint);
                } else { // PopOut
                    m_alpha.to(0, 250, Easing::OutQuint);
                    m_scale.to(0.6f, 250, Easing::In);
                }
                if (m_drag == Drag::None) m_rotation.to(0, 400, Easing::OutQuint);
                // Leaving the window: gone at once, the system cursor takes over.
                if (!inside) m_alpha.set(0);
            }

            // Buttons.
            bool down = focused && inside
                && ((GetAsyncKeyState(VK_LBUTTON) | GetAsyncKeyState(VK_RBUTTON) | GetAsyncKeyState(VK_MBUTTON)) & 0x8000);
            if (down && !m_down) onDown(pos);
            else if (!down && m_down) onUp();
            m_down = down;

            // Drag rotation (in pixels, like osu!).
            CCPoint px = pos * pxPerPoint;
            if (m_drag != Drag::None) {
                if (px != m_lastMovePx) onMove(px);
                if (ccpDistance(m_downPx, m_lastMovePx) > 60) {
                    // Interpolation.ValueAt(0.04, down, last, 0, elapsed): the pivot floats after the cursor.
                    float f = ms > 0 ? std::min(1.f, 0.04f / ms) : 0.f;
                    m_downPx = m_downPx + (m_lastMovePx - m_downPx) * f;
                }
            }

            // Tilt while moving (ours, not osu!'s): the arrow hangs from its tip and
            // its body swings back against the motion, more the faster it goes.
            // Stiffer than the drag rotation, and off while that's turning it.
            m_time += dt;
            // Left alone, it wonders where you went (once per lull); at the
            // hour marks it notices how long you've been here; late at night
            // or early in the morning it notices the time, once the menu is up.
            if (m_hasPos && (std::abs(pos.x - m_lastPos.x) > 0.5f || std::abs(pos.y - m_lastPos.y) > 0.5f)) {
                m_idleS = 0;
                m_idleSaid = false;
            } else if (visible && focused) {
                m_idleS += dt;
                if (m_idleS > 45 && !m_idleSaid) {
                    m_idleSaid = true;
                    quips::say("idle");
                }
            }
            if (visible && focused) {
                if (m_time > 7200 && !m_saidTwoHours) {
                    m_saidTwoHours = true;
                    quips::say("two-hours");
                } else if (m_time > 3600 && !m_saidHour) {
                    m_saidHour = true;
                    quips::say("hour");
                } else if (m_time > 12 && !m_saidClock) {
                    m_saidClock = true;
                    auto t = std::time(nullptr);
                    std::tm local {};
                    localtime_s(&local, &t);
                    if (local.tm_hour < 4) quips::say("midnight", 0.8f);
                    else if (local.tm_hour >= 5 && local.tm_hour < 7) quips::say("early", 0.8f);
                }
            }
            if (m_hasPos && ms > 0 && inside) {
                CCPoint v = (pos - m_lastPos) * pxPerPoint / dt; // px/s, y up
                m_velocity = CCPoint(damp(m_velocity.x, v.x, 0.9, ms), damp(m_velocity.y, v.y, 0.9, ms));
                watchShaking((pos.x - m_lastPos.x) * pxPerPoint);
            }
            float tiltTarget = 0;
            bool tilting = Mod::get()->getSettingValue<bool>("cursor-rotation") && m_drag != Drag::Rotating && m_visible;
            if (tilting) {
                // Torque from the drag on the body (tip -> middle of the arrow, y down):
                // positive = clockwise, like cocos rotation.
                constexpr float BODY_X = 0.6f, BODY_Y = 0.8f;
                float vx = m_velocity.x, vyDown = -m_velocity.y;
                float torque = BODY_X * -vyDown - BODY_Y * -vx;
                // ~13 degrees at 1000 px/s, easing off towards 30.
                tiltTarget = TILT_MAX * std::tanh(torque * 0.015f / TILT_MAX);
            }
            m_tilt = damp(m_tilt, tiltTarget, 0.97, ms);

            for (auto t : {&m_alpha, &m_scale, &m_press, &m_rotation, &m_glow}) t->update(dt);
            if (m_bubble) m_bubble->tick(dt);
            if (m_visible && focused) quips::followSong();

            // Said its last word: it leaves.
            if (m_leaveAt >= 0 && m_time >= m_leaveAt) {
                m_leaveAt = -1;
                m_nagLevel = 0;
                Mod::get()->setSettingValue<bool>("custom-cursor", false);
                return;
            }

            // Dizzy: once it has spun back, it shivers for a moment.
            float wobble = 0;
            CCPoint jitter;
            if (m_dizzyAt >= 0 && m_time >= m_dizzyAt) {
                float t = m_time - m_dizzyAt;
                if (t >= DIZZY_S) {
                    m_dizzyAt = -1;
                } else {
                    if (!m_dizzySaid) {
                        m_dizzySaid = true;
                        if (m_bubble) m_bubble->say(DIZZY[std::min<int>(m_dizzyCount - 1, DIZZY.size() - 1)]);
                    }
                    float fade = (1 - t / DIZZY_S) * (1 - t / DIZZY_S);
                    wobble = 9.f * fade * std::sin(t * 2 * float(M_PI) * 13);
                    jitter = CCPoint(1.6f * fade * std::sin(t * 2 * float(M_PI) * 17), 1.2f * fade * std::cos(t * 2 * float(M_PI) * 11)) / pxPerPoint;
                }
            }

            this->setPosition(pos);
            m_lastPos = pos;
            m_hasPos = inside;
            float size = static_cast<float>(Mod::get()->getSettingValue<double>("cursor-size"));
            // osu! draws it in screen pixels: texture pixels x base scale x size.
            float scale = BASE_SCALE * size * CC_CONTENT_SCALE_FACTOR() / pxPerPoint;
            m_holder->setScale(scale * m_scale.get() * m_press.get());
            m_holder->setRotation(m_rotation.get() + m_tilt + wobble);
            m_holder->setPosition(jitter);
            auto alpha = static_cast<GLubyte>(std::clamp(m_alpha.get(), 0.f, 1.f) * 255);
            m_base->setOpacity(alpha);
            m_additive->setOpacity(static_cast<GLubyte>(std::clamp(m_glow.get() * m_alpha.get(), 0.f, 1.f) * 255));
            if (alpha > 0) CCNode::visit();
        }

    private:
        void onDown(CCPoint pos) {
            if (!m_visible) return;
            m_press.set(1);
            m_press.to(0.9f, 800, Easing::OutQuint);
            m_glow.set(0);
            m_glow.to(1, 800, Easing::OutQuint);
            if (Mod::get()->getSettingValue<bool>("cursor-rotation") && m_drag != Drag::Rotating) {
                m_drag = Drag::Started;
                m_downPx = pos * CCEGLView::get()->getScaleX();
                m_lastMovePx = m_downPx;
            }
            sfx::play(sfx::sound::CURSOR_TAP, 0.01f, 1.f);
        }

        void onUp() {
            m_glow.set(1);
            m_glow.to(0, 500, Easing::OutQuint);
            m_press.to(1, 500, Easing::OutElastic);
            if (m_drag != Drag::None) {
                float r = m_rotation.get();
                float springMs = 400 * (0.5f + std::abs(r / 960));
                m_rotation.to(0, springMs, Easing::OutElasticQuarter);
                m_drag = Drag::None;
                // Wound up so far it has to spin all the way back: dizzy once it's there.
                if (std::abs(r) >= DIZZY_TURN) {
                    if (m_time - m_lastDizzy > 30) m_dizzyCount = 0;
                    m_dizzyCount++;
                    m_lastDizzy = m_time;
                    m_dizzyAt = m_time + springMs / 1000.f * 0.6f;
                    m_dizzySaid = false;
                }
            }
            if (m_visible) sfx::play(sfx::sound::CURSOR_TAP, 0.01f, 0.8f);
        }

        // Fast left-right-left swings: a direction change counts when the
        // swing before it was quick and long enough.
        void watchShaking(float dx) {
            int dir = dx > 0 ? 1 : dx < 0 ? -1 : 0;
            if (dir != 0) {
                if (dir == m_shakeDir) {
                    m_shakeTravel += std::abs(dx);
                } else {
                    if (m_shakeTravel >= SHAKE_TRAVEL && std::abs(m_velocity.x) >= SHAKE_SPEED) m_reversals.push_back(m_time);
                    m_shakeDir = dir;
                    m_shakeTravel = std::abs(dx);
                }
            }
            while (!m_reversals.empty() && m_reversals.front() < m_time - 1.f) m_reversals.erase(m_reversals.begin());
            if (m_reversals.size() < size_t(SHAKE_TURNS) || m_time - m_lastNag < 1.5f) return;
            m_reversals.clear();
            // Left alone for a while, it forgets.
            if (m_time - m_lastNag > 8) m_nagLevel = 0;
            auto line = NAGS[std::min<int>(m_nagLevel, NAGS.size() - 1)];
            if (m_bubble) m_bubble->say(line);
            if (m_nagLevel >= int(NAGS.size()) - 1 && m_leaveAt < 0) {
                m_leaveAt = m_time + Bubble::showFor(line) + Bubble::FADE_S + 1.f;
            }
            m_nagLevel++;
            m_lastNag = m_time;
        }

        void onMove(CCPoint px) {
            m_lastMovePx = px;
            float distance = ccpDistance(px, m_downPx);
            // Not until it's moved a bit from where the button went down.
            if (m_drag == Drag::Started && distance > 80) m_drag = Drag::Rotating;
            if (m_drag != Drag::Rotating || distance <= 0) return;
            // osu!'s y points down: flip ours.
            CCPoint offset = px - m_downPx;
            float degrees = std::atan2(-offset.x, -offset.y) * 180.f / float(M_PI) + 24.3f;
            // The shortest way round.
            float current = m_rotation.get();
            float diff = std::fmod(degrees - current, 360.f);
            if (diff < -180) diff += 360;
            if (diff > 180) diff -= 360;
            m_rotation.to(current + diff, 120, Easing::OutQuint);
        }

        CCNode* m_holder = nullptr;
        CCSprite* m_base = nullptr;
        CCSprite* m_additive = nullptr;
        Tweened<float> m_alpha {0.f};
        Tweened<float> m_scale {1.f};
        Tweened<float> m_press {1.f};
        Tweened<float> m_rotation {0.f};
        Tweened<float> m_glow {0.f};
        bool m_visible = false;
        bool m_down = false;
        Drag m_drag = Drag::None;
        CCPoint m_downPx, m_lastMovePx, m_lastPos;
        bool m_hasPos = false;
        CCPoint m_velocity;
        float m_tilt = 0;

        Bubble* m_bubble = nullptr;
        float m_time = 0;
        int m_shakeDir = 0;
        float m_shakeTravel = 0;
        std::vector<float> m_reversals;
        int m_nagLevel = 0;
        float m_lastNag = -100;
        float m_leaveAt = -1;
        float m_dizzyAt = -1;
        bool m_dizzySaid = false;
        int m_dizzyCount = 0;
        float m_lastDizzy = -100;
        float m_idleS = 0;
        bool m_idleSaid = false;
        bool m_saidHour = false, m_saidTwoHours = false, m_saidClock = false;
    };

    // Not in the scene graph: visited by hand in swapBuffers. Kept for the
    // whole game (no release at exit, after cocos is gone).
    MenuCursor* g_cursor = nullptr;
    // Dropped for a graphics reload, to be made again at the next menu.
    bool g_dropped = false;

    void createCursor() {
        if (auto cursor = MenuCursor::create()) {
            cursor->retain();
            g_cursor = cursor;
        }
    }
}

void cursorSay(std::string const& text) {
    if (g_cursor && g_enabled) g_cursor->say(text);
}

} // namespace lazer

class $modify(LazerCursorView, CCEGLView) {
    // After every other mod's hook: ImGui menus draw in theirs, and the
    // cursor goes over them.
    static void onModify(auto& self) {
        (void)self.setHookPriorityPre("cocos2d::CCEGLView::swapBuffers", Priority::LastPre);
    }

    void swapBuffers() {
        if (lazer::g_cursor) {
            // No ccGLInvalidateStateCache here: it also frees cocos's matrix
            // stacks (projection included) and the game draws nothing after.
            // ImGui's backend restores the GL state it touches.
            kmGLMatrixMode(KM_GL_MODELVIEW);
            kmGLPushMatrix();
            kmGLLoadIdentity();
            lazer::g_cursor->visit();
            kmGLPopMatrix();
        }
        CCEGLView::swapBuffers();
    }
};

// Only once the game has loaded (the intro is starting): the loading screen
// stutters, and a drawn cursor would stutter with it. The system cursor
// stays until then.
void lazer::releaseMenuCursor() {
    if (g_cursor) {
        g_cursor->release();
        g_cursor = nullptr;
    }
    g_dropped = false;
    if (g_enabled) {
        g_enabled = false;
        hideSystemCursor(false);
    }
}

$on_game(Loaded) {
    Loader::get()->queueInMainThread([] { lazer::createCursor(); });
}

// Switching between fullscreen and windowed makes a new GL context and GD
// reloads every texture: the cursor's would be dead (drawn black). Let it go
// while the old context is still there, and show the system cursor meanwhile.
class $modify(LazerCursorReload, GameManager) {
    void reloadAll(bool switchingModes, bool toFullscreen, bool borderless, bool fix, bool unused) {
        if (lazer::g_cursor) {
            lazer::releaseMenuCursor();
            lazer::g_dropped = true;
        }
        GameManager::reloadAll(switchingModes, toFullscreen, borderless, fix, unused);
    }
};

class $modify(LazerCursorMenu, MenuLayer) {
    bool init() {
        if (!MenuLayer::init()) return false;
        if (lazer::g_dropped) {
            lazer::g_dropped = false;
            Loader::get()->queueInMainThread([] { lazer::createCursor(); });
        }
        return true;
    }
};

#endif
