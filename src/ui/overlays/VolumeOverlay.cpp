// osu!'s volume overlay (osu.Game/Overlays/VolumeOverlay.cs, Overlays/Volume/
// VolumeMeter.cs): three meters at the left of the screen, a ring that fills
// clockwise with the volume and the number inside, each with its name in a
// dark pill beside it. The wheel changes the selected meter (music unless the
// mouse is over another); the first notch while hidden only shows the overlay,
// like osu!. Faster scrolling steps further (osu!'s acceleration), and each
// percent ticks (UI/notch-tick), pitched up with the volume.
//
// The wheel reaches it two ways: GD's mouse dispatcher is hooked, and an event
// none of the mod's own scroll views used (they mark the ones they take) in
// one of the mod's screens changes the volume; with Alt held, it does
// anywhere outside the editor. The overlay isn't in the scene graph: it's
// drawn right before each frame is shown, over every scene, under the cursor.

#include "VolumeOverlay.hpp"

#include <Geode/Geode.hpp>

namespace lazer::volume {
    namespace {
        bool g_wheelHandled = false;
    }
    void markWheelHandled() { g_wheelHandled = true; }
}

#if defined(GEODE_IS_WINDOWS) || defined(GEODE_IS_MACOS)

#include "../../audio/Sfx.hpp"
#include "../core/Easing.hpp"
#include "../core/RingArc.hpp"
#include "../core/RoundedBox.hpp"
#include "../core/Text.hpp"
#include "../core/Theme.hpp"
#include "../select/SongSelect.hpp"

#include <Geode/modify/CCEGLView.hpp>
#include <Geode/modify/CCMouseDispatcher.hpp>
#include <Geode/modify/GameManager.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <string>

using namespace geode::prelude;

namespace lazer::volume {

namespace {
    // osu! sizes (768 px tall screen), scaled by unitScale(). osu! lines the
    // meters up by their left edges; here their rings share one axis.
    constexpr float OFFSET = 10.f;            // VolumeOverlay.offset: the margin and spacing
    constexpr float BAND_WIDTH = 300.f;       // the dark fade behind the meters
    constexpr float SMALL_METER = 125.f;      // VolumeMeter circleSize
    constexpr float BIG_METER = 150.f;        // MasterVolumeMeter's
    constexpr float LABEL_HEIGHT = 20.f;      // LABEL_SIZE
    constexpr float LABEL_RADIUS = 10.f;
    constexpr float LABEL_MARGIN = 32.f;      // each side of the name
    constexpr float RING_OUTER = 0.78f;       // progress_end_radius, of the circle
    constexpr float RING_INNER = 0.75f;       // progress_start_radius
    constexpr float RING_SWEEP = 0.75f;       // the ring covers three quarters of a turn
    constexpr float NUMBER_SIZE = 0.16f;      // of the circle
    constexpr float FADE_IN_MS = 220.f;       // it slides in from the edge as it fades in
    constexpr float FADE_OUT_MS = 260.f;      // and back out
    constexpr float SLIDE = 40.f;             // how far it comes in from
    constexpr float HIDE_MS = 1000.f;         // schedulePopOut
    constexpr float VOLUME_TWEEN_MS = 400.f;  // DisplayVolume follows the volume
    constexpr float SELECT_MS = 500.f;        // transition_length
    constexpr float SELECT_SCALE = 1.04f;
    constexpr float UNITS_PER_NOTCH = 12.f;   // GD reports ~12 per physical notch
    constexpr float ADJUST_STEP = 0.01f;      // one notch, before acceleration
    constexpr float MAX_ACCELERATION = 5.f;
    constexpr float ACCELERATION_MULTIPLIER = 1.8f;
    constexpr float ACCELERATION_RESET_MS = 150.f;
    constexpr float TICK_DEBOUNCE_MS = 30.f;

    constexpr ccColor4B GRAY1 {0x11, 0x11, 0x11, 255};        // OsuColour.Gray1
    constexpr ccColor4B BLUE_DARKER {0x22, 0x99, 0xbb, 255};  // small meters
    constexpr ccColor4B PINK_DARKER {0xbb, 0x11, 0x77, 255};  // the big one
    constexpr ccColor4B WHITE {255, 255, 255, 255};

    bool settingOn() {
        auto mod = Mod::get();
        return mod->getSettingValue<bool>("enabled") && mod->getSettingValue<bool>("scroll-volume");
    }

    // One volume: what it's called, what it reads and writes.
    struct Channel {
        char const* name;
        float size;
        ccColor4B color;
        std::function<float()> get;
        std::function<void(float)> set;
    };

    std::array<Channel, 3> channels() {
        return {{
            {"EFFECTS", SMALL_METER, BLUE_DARKER,
             [] { return GameManager::get()->m_sfxVolume; },
             [](float v) {
                 GameManager::get()->m_sfxVolume = v;
                 FMODAudioEngine::sharedEngine()->setEffectsVolume(v);
             }},
            {"MUSIC", BIG_METER, PINK_DARKER,
             [] { return GameManager::get()->m_bgVolume; },
             [](float v) {
                 GameManager::get()->m_bgVolume = v;
                 FMODAudioEngine::sharedEngine()->setBackgroundMusicVolume(v);
             }},
            {"INTERFACE", SMALL_METER, BLUE_DARKER,
             [] { return Mod::get()->getSettingValue<int64_t>("ui-sound-volume") / 100.f; },
             [](float v) { Mod::get()->setSettingValue<int64_t>("ui-sound-volume", int64_t(std::round(v * 100))); }},
        }};
    }
    constexpr int MUSIC = 1; // the default selection, osu!'s master

    // The dark fade behind the meters: black at the screen's edge to nothing
    // across its width, and fading out above the meters. Two quads with
    // vertex colours (cocos' layer gradient can't fade both ways).
    class Band : public CCNodeRGBA {
    public:
        static Band* create(CCSize size, float cap) {
            auto ret = new Band();
            if (ret->init()) {
                ret->autorelease();
                ret->setContentSize(size);
                ret->m_cap = cap;
                ret->setAnchorPoint({0, 0});
                ret->setShaderProgram(CCShaderCache::sharedShaderCache()->programForKey(kCCShader_PositionColor));
                return ret;
            }
            delete ret;
            return nullptr;
        }

        void draw() override {
            auto size = this->getContentSize();
            float a = this->getDisplayedOpacity() / 255.f * 0.75f;
            float body = std::max(0.f, size.height - m_cap);
            // Premultiplied black: only the alpha varies.
            GLfloat verts[] = {
                0, 0,  size.width, 0,  0, body,  size.width, body,
                0, body,  size.width, body,  0, size.height,  size.width, size.height,
            };
            GLfloat colors[] = {
                0, 0, 0, a,  0, 0, 0, 0,  0, 0, 0, a,  0, 0, 0, 0,
                0, 0, 0, a,  0, 0, 0, 0,  0, 0, 0, 0,  0, 0, 0, 0,
            };
            CC_NODE_DRAW_SETUP();
            ccGLBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
            ccGLEnableVertexAttribs(kCCVertexAttribFlag_Position | kCCVertexAttribFlag_Color);
            glVertexAttribPointer(kCCVertexAttrib_Position, 2, GL_FLOAT, GL_FALSE, 0, verts);
            glVertexAttribPointer(kCCVertexAttrib_Color, 4, GL_FLOAT, GL_FALSE, 0, colors);
            glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
            glDrawArrays(GL_TRIANGLE_STRIP, 4, 4);
        }

    private:
        float m_cap = 0;
    };

    class VolumeOverlay : public CCNodeRGBA {
    public:
        static VolumeOverlay* create() {
            auto ret = new VolumeOverlay();
            if (ret->init()) {
                ret->autorelease();
                return ret;
            }
            delete ret;
            return nullptr;
        }

        bool init() override {
            if (!CCNodeRGBA::init()) return false;
            m_channels = channels();
            float k = m_k = unitScale();
            this->setCascadeOpacityEnabled(true);
            this->setOpacity(0);

            auto win = CCDirector::sharedDirector()->getWinSize();

            // The fade behind the meters, the screen's full height.
            auto band = Band::create({BAND_WIDTH * k, win.height}, 0);
            band->setPosition({0, 0});
            this->addChild(band);

            // The meters, stacked and centred on the screen's height, their
            // rings on one axis (the small ones are moved in to the big one's).
            float total = 0, widest = 0;
            for (auto& c : m_channels) {
                total += c.size * k;
                widest = std::max(widest, c.size * k);
            }
            total += OFFSET * k * (m_channels.size() - 1);
            float y = win.height / 2 + total / 2;
            for (size_t i = 0; i < m_channels.size(); i++) {
                auto& c = m_channels[i];
                float size = c.size * k;
                y -= size / 2;
                buildMeter(m_meters[i], c, size, {OFFSET * k + (widest - size) / 2, y}, k);
                y -= size / 2 + OFFSET * k;
            }
            select(MUSIC, false);
            return true;
        }

        bool shown() const { return m_visible; }

        // The meter under the mouse, -1 for none (while shown).
        int meterAt(CCPoint p) const {
            if (!m_visible) return -1;
            p.x += SLIDE * m_k * m_slide.get(); // while it slides
            for (size_t i = 0; i < m_meters.size(); i++) {
                auto& m = m_meters[i];
                auto pos = m.root->getPosition();
                auto size = m.root->getContentSize();
                if (p.x >= pos.x && p.x <= pos.x + size.width && p.y >= pos.y - size.height / 2 && p.y <= pos.y + size.height / 2) {
                    return static_cast<int>(i);
                }
            }
            return -1;
        }

        void select(int index, bool sound = true) {
            if (index < 0 || index >= static_cast<int>(m_meters.size()) || index == m_selected) return;
            m_selected = index;
            for (size_t i = 0; i < m_meters.size(); i++) {
                auto& m = m_meters[i];
                bool on = static_cast<int>(i) == index;
                m.scale.to(on ? SELECT_SCALE : 1.f, SELECT_MS, Easing::OutExpo);
                m.glow.to(on ? 1.f : 0.f, SELECT_MS, Easing::OutExpo);
            }
            if (sound) sfx::hover(sfx::sound::BUTTON_HOVER);
        }

        void show() {
            if (!m_visible) {
                // Back to the default meter when it comes up again.
                select(MUSIC, false);
                for (auto& m : m_meters) m.display.set(m.channel->get());
                m_visible = true;
                m_alpha.to(1.f, FADE_IN_MS, Easing::OutQuint);
                m_slide.to(0.f, FADE_IN_MS * 1.5f, Easing::OutQuint);
            }
            m_hideIn = HIDE_MS;
        }

        void hide() {
            if (!m_visible) return;
            m_visible = false;
            m_alpha.to(0.f, FADE_OUT_MS, Easing::OutQuint);
            m_slide.to(1.f, FADE_OUT_MS, Easing::OutQuint);
        }

        // One wheel event, in notches (positive = up). The first while hidden
        // only shows the overlay (osu!'s Adjust).
        void wheel(float notches) {
            if (!m_visible) return show();
            adjust(notches);
            show();
        }

        void adjust(float delta) {
            if (delta == 0) return;
            // Every step within 150 ms of the last speeds up, to five times.
            m_accelerationIn = ACCELERATION_RESET_MS;
            delta *= m_acceleration;
            m_acceleration = std::min(MAX_ACCELERATION, m_acceleration * ACCELERATION_MULTIPLIER);

            auto& m = m_meters[static_cast<size_t>(m_selected)];
            float v = m.channel->get();
            float step = std::max(ADJUST_STEP, std::fabs(delta * ADJUST_STEP));
            v = std::clamp(std::round((v + (delta > 0 ? step : -step)) * 100.f) / 100.f, 0.f, 1.f);
            m.channel->set(v);
            m.display.to(v, VOLUME_TWEEN_MS, Easing::OutQuint);
        }

        void tick(float dt) {
            float ms = dt * 1000.f;
            if (m_accelerationIn > 0) {
                m_accelerationIn -= ms;
                if (m_accelerationIn <= 0) m_acceleration = 1.f;
            }
            if (m_tickAgo < 1000.f) m_tickAgo += ms;

            auto mouse = geode::cocos::getMousePos();
            bool hovered = meterAt(mouse) >= 0;
            if (m_visible) {
                // Moving over a meter picks it, and keeps the overlay up.
                if (hovered && (mouse.x != m_lastMouse.x || mouse.y != m_lastMouse.y)) {
                    select(meterAt(mouse));
                    m_hideIn = HIDE_MS;
                }
                m_hideIn -= ms;
                if (m_hideIn <= 0 && !hovered) hide();
            }
            m_lastMouse = mouse;

            m_alpha.update(dt);
            m_slide.update(dt);
            this->setOpacity(static_cast<GLubyte>(std::round(m_alpha.get() * 255)));
            this->setPositionX(-SLIDE * m_k * m_slide.get());
            for (auto& m : m_meters) {
                m.display.update(dt);
                m.scale.update(dt);
                m.glow.update(dt);
                m.root->setScale(m.scale.get());
                m.glowBox->setOpacity(static_cast<GLubyte>(std::round(m.glow.get() * m_alpha.get() * 255)));
                float v = std::clamp(m.display.get(), 0.f, 1.f);
                m.arc->setArc(0.5f, v * RING_SWEEP);
                m.arcGlow->setArc(0.5f, v * RING_SWEEP);
                int percent = static_cast<int>(std::round(v * 100));
                if (percent != m.shownPercent) {
                    bool first = m.shownPercent < 0;
                    m.shownPercent = percent;
                    m.number->setString(v >= 0.995f ? "MAX" : fmt::format("{}", percent).c_str());
                    if (!first && m_visible) playTick(v, percent);
                }
            }
        }

        bool drawn() const { return m_alpha.get() > 0.f || m_visible; }

    private:
        struct Meter {
            Channel const* channel = nullptr;
            CCNode* root = nullptr;
            RoundedBox* glowBox = nullptr;
            RingArc* arc = nullptr;
            RingArc* arcGlow = nullptr;
            CCLabelBMFont* number = nullptr;
            Tweened<float> display {0.f};
            Tweened<float> scale {1.f};
            Tweened<float> glow {0.f};
            int shownPercent = -1;
        };

        void buildMeter(Meter& m, Channel const& c, float size, CCPoint leftCentre, float k) {
            m.channel = &c;
            // The root's left edge is at leftCentre; it scales about that edge.
            m.root = CCNode::create();
            m.root->setAnchorPoint({0, 0.5f});
            m.root->setPosition(leftCentre);
            this->addChild(m.root);

            // The dark disc, its glow while selected, and the ring on it.
            auto disc = RoundedBox::create({size, size}, size / 2, GRAY1);
            disc->setPosition({size / 2, 0});
            m.root->addChild(disc);
            m.glowBox = RoundedBox::create({size, size}, size / 2, {0, 0, 0, 0});
            m.glowBox->setShadow(OFFSET * k, {c.color.r, c.color.g, c.color.b, 80});
            m.glowBox->setPosition({size / 2, 0});
            m.glowBox->setOpacity(0);
            m.root->addChild(m.glowBox, -1);

            float outer = size * RING_OUTER, thickness = size * (RING_OUTER - RING_INNER) / 2;
            m.arcGlow = RingArc::create(outer, thickness, {c.color.r, c.color.g, c.color.b, 0});
            m.arcGlow->setGlow(thickness * 3.f, {c.color.r, c.color.g, c.color.b, 200});
            m.arcGlow->setPosition({size / 2, 0});
            m.root->addChild(m.arcGlow, 1);
            m.arc = RingArc::create(outer, thickness, WHITE);
            m.arc->setPosition({size / 2, 0});
            m.root->addChild(m.arc, 2);

            m.number = makeText("0", Weight::Bold, size * NUMBER_SIZE * 1.3f);
            m.number->setPosition({size / 2, 0});
            m.root->addChild(m.number, 3);

            // Its name, in a pill to the right.
            auto name = makeText(c.name, Weight::Bold, 15 * k);
            float labelW = name->getScaledContentSize().width + 2 * LABEL_MARGIN * k, labelH = LABEL_HEIGHT * k;
            auto label = RoundedBox::create({labelW, labelH}, LABEL_RADIUS * k, GRAY1);
            label->setPosition({size + OFFSET * k + labelW / 2, 0});
            m.root->addChild(label);
            name->setPosition(label->getPosition());
            m.root->addChild(name, 1);

            m.root->setContentSize({size + OFFSET * k + labelW, size});
            m.display.set(c.get());
        }

        void playTick(float volume, int percent) {
            if (m_tickAgo < TICK_DEBOUNCE_MS) return;
            m_tickAgo = 0;
            float frequency = 0.99f + volume * 0.1f;
            // Pitched down at the ends, max included.
            if (percent == 0 || percent == 100) frequency -= 0.5f;
            sfx::play(sfx::sound::NOTCH_TICK, 0.01f, frequency);
        }

        std::array<Channel, 3> m_channels;
        std::array<Meter, 3> m_meters;
        int m_selected = -1;
        bool m_visible = false;
        float m_k = 1;
        Tweened<float> m_alpha {0.f};
        Tweened<float> m_slide {1.f};          // 0 in place, 1 tucked past the edge
        float m_hideIn = 0;
        float m_acceleration = 1.f;
        float m_accelerationIn = 0;
        float m_tickAgo = 1000.f;
        CCPoint m_lastMouse;
    };

    // Not in the scene graph: visited by hand before each frame is shown.
    // Dropped for a graphics reload (its textures would be gone), made again
    // the next time it's needed.
    VolumeOverlay* g_overlay = nullptr;

    VolumeOverlay* overlay() {
        if (!g_overlay) {
            if (auto made = VolumeOverlay::create()) {
                made->retain();
                g_overlay = made;
            }
        }
        return g_overlay;
    }

    void dropOverlay() {
        if (g_overlay) {
            g_overlay->release();
            g_overlay = nullptr;
        }
    }

    // One of the mod's own screens is up: the menu, song select, your levels
    // and lists, a level page. A scroll nothing there used changes the volume.
    bool sceneIsOurs() {
        auto scene = CCDirector::sharedDirector()->getRunningScene();
        if (!scene || typeinfo_cast<CCTransitionScene*>(scene)) return false;
        for (auto child : CCArrayExt<CCNode*>(scene->getChildren())) {
            if (typeinfo_cast<MenuLayer*>(child) || typeinfo_cast<SongSelect*>(child)) return true;
            if (typeinfo_cast<LevelBrowserLayer*>(child) && child->getChildByID("level-listing"_spr)) return true;
            if (typeinfo_cast<LevelInfoLayer*>(child) && child->getChildByID("level-page"_spr)) return true;
        }
        return false;
    }

    bool inEditor() {
        return LevelEditorLayer::get() != nullptr;
    }
}

} // namespace lazer::volume

using namespace lazer::volume;

class $modify(LazerVolumeWheel, CCMouseDispatcher) {
    bool dispatchScrollMSG(float y, float x) {
        if (!settingOn() || inEditor()) return CCMouseDispatcher::dispatchScrollMSG(y, x);
        // GD's y grows downwards; a notch up turns the volume up.
        float notches = std::clamp(-y / UNITS_PER_NOTCH, -3.f, 3.f);
        auto shown = g_overlay && g_overlay->shown() ? g_overlay : nullptr;

        // Over one of the meters: that meter's.
        if (shown) {
            int meter = shown->meterAt(geode::cocos::getMousePos());
            if (meter >= 0) {
                shown->select(meter);
                shown->wheel(notches);
                return true;
            }
        }
        // Alt and the wheel: the volume, wherever you are.
        if (CCKeyboardDispatcher::get()->getAltKeyPressed()) {
            if (auto o = overlay()) o->wheel(notches);
            return true;
        }
        // Otherwise whatever's there scrolls, and in the mod's own screens a
        // wheel nothing took changes the volume.
        g_wheelHandled = false;
        bool ours = sceneIsOurs() && !lazer::popupOnTop();
        bool result = CCMouseDispatcher::dispatchScrollMSG(y, x);
        if (ours && !g_wheelHandled) {
            if (auto o = overlay()) o->wheel(notches);
        }
        return result;
    }
};

class $modify(LazerVolumeView, CCEGLView) {
    // Before the cursor's hook (LastPre): the overlay draws under the cursor.
    static void onModify(auto& self) {
        (void)self.setHookPriorityPre("cocos2d::CCEGLView::swapBuffers", Priority::VeryLatePre);
    }

    void swapBuffers() {
        if (g_overlay) {
            g_overlay->tick(CCDirector::sharedDirector()->getDeltaTime());
            if (g_overlay->drawn()) {
                kmGLMatrixMode(KM_GL_MODELVIEW);
                kmGLPushMatrix();
                kmGLLoadIdentity();
                g_overlay->visit();
                kmGLPopMatrix();
            }
        }
        CCEGLView::swapBuffers();
    }
};

// A new GL context (fullscreen <-> windowed) reloads every texture: let the
// overlay go while the old one is there; it's made again at the next wheel.
class $modify(LazerVolumeReload, GameManager) {
    void reloadAll(bool switchingModes, bool toFullscreen, bool borderless, bool fix, bool unused) {
        dropOverlay();
        GameManager::reloadAll(switchingModes, toFullscreen, borderless, fix, unused);
    }
};

#endif
