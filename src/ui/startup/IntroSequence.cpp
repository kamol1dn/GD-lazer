#include "IntroSequence.hpp"

#include "../../audio/AudioAnalyzer.hpp"
#include "../../audio/MusicPlayer.hpp"
#include "../../audio/Sfx.hpp"
#include "../../integrations/ModIntegrations.hpp"

#include <Geode/fmod/fmod.hpp>
#include <array>
#include <random>

using namespace geode::prelude;

namespace lazer {

namespace {
    // Dash by MDK as it ships with GD (Resources/Dash.mp3), measured from the
    // file: 128 BPM in 4/4, the first beat at 1.149 s with a faint pad from
    // 0.21 s before it, a crash on bar 5 (8.649 s), the voice saying
    // "Geometry Dash" at 15.0 s into the first drop at 16.149 s.
    constexpr float BPM = 128;
    constexpr float BEAT_MS = 60000.f / BPM; // 468.75
    constexpr float FIRST_BEAT_MS = 1149;
    constexpr float PAD_MS = 210;
    float beat(float n) { return FIRST_BEAT_MS + n * BEAT_MS; }

    // What's on screen, in beats from the first.
    constexpr int ICONS_BEAT = 0;   // bar 1: the icons punch in, one a beat, triangles glitching behind
    constexpr int GATHER_BEAT = 4;  // bar 2: they step together and grow, a step a beat
    constexpr int LOGO_BEAT = 8;    // bars 3-4: the logo draws itself in, down to its place on the menu
    constexpr int REVEAL_BEAT = 16; // bar 5, on the crash: the flash, and the menu under it
    constexpr float LOGO_BEATS = REVEAL_BEAT - LOGO_BEAT;

    // The notes of the opening arpeggio over those four bars, in 16ths from
    // the first beat (the song's onsets): a burst of triangles on each.
    constexpr std::array<int, 40> ONSETS {{
        0, 1, 3, 4, 7, 8, 9, 11, 12, 14,
        16, 17, 18, 20, 22, 23, 25, 27, 28,
        32, 35, 36, 38, 39, 40, 41, 43, 44, 46,
        48, 49, 50, 51, 53, 54, 55, 56, 57, 59, 62,
    }};

    // Bar 2, a step a beat: the icons' scale and the gap between them
    // (osu!'s RULESETS_2 / RULESETS_3 jumps, spread over four beats).
    struct Gather {
        float scale;
        float spacing;
    };
    constexpr std::array<Gather, 4> GATHER {{{1.5f, 120}, {2.2f, 60}, {3.2f, 25}, {4.5f, 10}}};
    constexpr float ICON_SPACING = 200;
    constexpr float PUNCH_MS = 250; // an icon's punch-in

    constexpr float SCALE_START = 1.2f;
    constexpr float SCALE_ADJUST = 0.8f;

    // Without the music player: the menu's music fades in as the intro's copy
    // of the song fades out after the reveal.
    constexpr float TRACK_FADE = 1800;

    constexpr float TRIANGLE_LIFE = 120;
    constexpr float ICON_SIZE = 30;

    constexpr float PI = 3.14159265f;

    // Closed polyline round a rounded square, starting at the middle of its
    // top edge and running clockwise (anticlockwise if `ccw`).
    std::vector<CCPoint> roundedSquare(float side, float corner, bool ccw) {
        float h = side / 2 - corner;
        CCPoint centres[4] {{h, h}, {h, -h}, {-h, -h}, {-h, h}}; // clockwise from top-right
        std::vector<CCPoint> pts {{0, side / 2}};
        for (int c = 0; c < 4; c++) {
            float start = PI / 2 - c * PI / 2;
            for (int i = 0; i <= 8; i++) {
                float a = start - (PI / 2) * i / 8;
                pts.push_back(centres[c] + CCPoint(std::cos(a), std::sin(a)) * corner);
            }
        }
        pts.push_back({0, side / 2});
        if (ccw) std::reverse(pts.begin(), pts.end());
        return pts;
    }

    // Clockwise circle from `startAngle`.
    std::vector<CCPoint> circle(float radius, float startAngle, int segments) {
        std::vector<CCPoint> pts;
        for (int i = 0; i <= segments; i++) {
            float a = startAngle - 2 * PI * i / segments;
            pts.push_back(CCPoint(std::cos(a), std::sin(a)) * radius);
        }
        return pts;
    }

    // `progress` remapped to 0..1 over [from, to], eased.
    float stage(float progress, float from, float to) {
        return static_cast<float>(ease(Easing::OutQuad, std::clamp((progress - from) / (to - from), 0.f, 1.f)));
    }

    float eased(Easing easing, float t) {
        return static_cast<float>(ease(easing, std::clamp(t, 0.f, 1.f)));
    }

    std::mt19937& rng() {
        static std::mt19937 r {std::random_device {}()};
        return r;
    }
    float random01() { return std::uniform_real_distribution<float>(0.f, 1.f)(rng()); }

    CCNode* fitted(CCNode* node, float size) {
        auto s = node->getContentSize();
        float longest = std::max(s.width, s.height);
        if (longest > 0) node->setScale(size / longest);
        return node;
    }

    // GD's copy of the song, for the intro's own playback. GD plays its songs
    // by bare file name (on Android the full path is inside the APK).
    std::string dashPath() {
        std::string file = MusicPlayer::introFile();
#ifdef GEODE_IS_ANDROID
        return file;
#else
        return CCFileUtils::sharedFileUtils()->fullPathForFilename(file.c_str(), false);
#endif
    }
}

IntroSequence* IntroSequence::create(float logoRadius, std::function<void()> onReveal) {
    auto ret = new IntroSequence();
    if (ret->init(logoRadius, std::move(onReveal))) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

bool IntroSequence::init(float logoRadius, std::function<void()> onReveal) {
    if (!CCLayer::init()) return false;
    m_onReveal = std::move(onReveal);
    // The music player's copy needs no preparing; our own is opened now,
    // during the loading screen's fade, not on the first frame.
    if (!MusicPlayer::get().introTrackPossible()) sfx::preloadFile(dashPath());
    m_win = CCDirector::get()->getWinSize();
    m_k = m_win.height / 768.f;
    m_palette = PlayerPalette::current();
    CCPoint center = m_win / 2;

    m_content = CCNode::create();
    this->addChild(m_content);

    // IntroScreen.CreateBackground: black until the menu is revealed.
    m_content->addChild(CCLayerColor::create({0, 0, 0, 255}), -1);

    m_triangleDraw = CCDrawNode::create();
    m_triangleDraw->setBlendFunc({GL_ONE, GL_ONE}); // additive
    m_content->addChild(m_triangleDraw);

    // osu!'s ruleset icons, as GD's building blocks: your cube, a spike, an orb and a trigger.
    m_iconsScale = CCNode::create();
    m_iconsScale->setPosition(center);
    m_content->addChild(m_iconsScale, 2);
    m_icons = CCNode::create();
    m_icons->setVisible(false);
    m_iconsScale->addChild(m_icons);

    float icon = ICON_SIZE * m_k;
    std::vector<CCNode*> icons;
    icons.push_back(integrations::playerIcon(false, icon));
    for (auto frame : {"spike_01_001.png", "ring_01_001.png", "edit_eMoveComBtn_001.png"}) {
        auto sprite = CCSprite::createWithSpriteFrameName(frame);
        if (!sprite) sprite = CCSprite::create();
        icons.push_back(fitted(sprite, icon));
    }
    for (auto node : icons) {
        if (!node) continue;
        auto holder = CCNode::create();
        holder->addChild(node);
        holder->setVisible(false);
        m_icons->addChild(holder);
        m_iconHolders.push_back(holder);
    }

    // The logo drawing itself in (osu!'s LazerLogo), sized so that it ends up
    // exactly on the menu logo once it has shrunk (scale 0.4 x 1.0).
    m_logoBaseRadius = logoRadius / ((SCALE_START - SCALE_ADJUST) * (SCALE_START - SCALE_ADJUST * 0.25f));
    m_logoContainer = CCNode::create();
    m_logoContainer->setPosition(center);
    m_content->addChild(m_logoContainer, 3);
    m_logo = CCNode::create();
    m_logo->setVisible(false);
    m_logoContainer->addChild(m_logo);

    float r = m_logoBaseRadius;
    m_logoDraw = CCDrawNode::create();
    m_logo->addChild(m_logoDraw);
    // The menu logo's rim, and a cube about the size of its centre icon.
    m_ringPath = circle(r * 0.96f, PI / 2, 128);
    m_cubePath = roundedSquare(r * 0.8f, r * 0.1f, false);
    m_innerPath = roundedSquare(r * 0.36f, r * 0.05f, true);

    this->setTouchEnabled(true);
    this->setKeypadEnabled(true);
    this->scheduleUpdate();
    return true;
}

void IntroSequence::registerWithTouchDispatcher() {
    CCDirector::get()->getTouchDispatcher()->addTargetedDelegate(this, -600, true);
}

bool IntroSequence::ccTouchBegan(CCTouch*, CCEvent*) {
    skip();
    return true;
}

void IntroSequence::keyBackClicked() {
    skip();
}

void IntroSequence::skip() {
    if (m_started && !m_revealed) reveal();
}

void IntroSequence::setMusicVolume(float volume) {
    if (auto channel = FMODAudioEngine::get()->getActiveMusicChannel(0)) channel->setVolume(volume);
}

void IntroSequence::start() {
    m_started = true;
    m_musicTrack = MusicPlayer::get().startIntroTrack();
    if (!m_musicTrack) {
        m_cue = sfx::playCueFile(dashPath());
        if (m_cue) m_cue->getVolume(&m_cueVolume);
        log::info("Intro plays its own copy of Dash ({})", m_cue ? "ok" : "failed to open");
    }
}

void IntroSequence::spawnTriangles(int count, float size) {
    for (int i = 0; i < count; i++) {
        m_triangles.push_back({{random01(), random01()}, (random01() + 0.2f) * size * m_k, random01() < 0.5f, 0});
    }
}

// GlitchingTriangles: a trickle of triangles while `emitting`, each flashing
// out over 120 ms, plus the bursts spawnTriangles adds on the song's notes.
void IntroSequence::updateTriangles(float ms, bool emitting, float intervalMs) {
    for (auto& t : m_triangles) t.ageMs += ms;
    std::erase_if(m_triangles, [](Triangle const& t) { return t.ageMs >= TRIANGLE_LIFE; });

    if (emitting) {
        m_triangleClock += ms;
        while (m_triangleClock >= intervalMs) {
            m_triangleClock -= intervalMs;
            spawnTriangles(1, 80);
        }
    } else {
        m_triangleClock = 0;
    }

    m_triangleDraw->clear();
    float areaW = m_win.width * 0.4f, areaH = m_win.height * 0.16f;
    CCPoint topLeft {(m_win.width - areaW) / 2, (m_win.height + areaH) / 2};
    for (auto const& t : m_triangles) {
        float a = 1.f - t.ageMs / TRIANGLE_LIFE;
        float x = topLeft.x + t.pos.x * areaW, y = topLeft.y - t.pos.y * areaH, s = t.size;
        CCPoint verts[3] {{x + s / 2, y}, {x + s, y - s}, {x, y - s}};
        ccColor4F white {a, a, a, a};
        if (t.outline) m_triangleDraw->drawPolygon(verts, 3, {0, 0, 0, 0}, 1.2f * m_k, white);
        else m_triangleDraw->drawPolygon(verts, 3, white, 0, white);
    }
}

void IntroSequence::layoutIcons(float spacing) {
    float icon = ICON_SIZE * m_k, gap = spacing * m_k;
    size_t n = m_iconHolders.size();
    float total = icon * n + gap * (n - 1);
    for (size_t i = 0; i < n; i++) {
        m_iconHolders[i]->setPosition({-total / 2 + icon / 2 + i * (icon + gap), 0});
    }
}

void IntroSequence::drawStroke(std::vector<CCPoint> const& path, float progress, float width,
                               ccColor3B from, ccColor3B to) {
    if (progress <= 0 || path.size() < 2) return;
    // By length, so the stroke advances at a constant speed.
    std::vector<float> lengths {0};
    for (size_t i = 1; i < path.size(); i++) lengths.push_back(lengths.back() + ccpDistance(path[i - 1], path[i]));
    float total = lengths.back(), drawTo = total * std::min(progress, 1.f);
    auto colorAt = [&](float t) {
        auto mix = [t](GLubyte a, GLubyte b) { return (a + (b - a) * t) / 255.f; };
        return ccColor4F {mix(from.r, to.r), mix(from.g, to.g), mix(from.b, to.b), 1.f};
    };
    for (size_t i = 1; i < path.size() && lengths[i - 1] < drawTo; i++) {
        CCPoint a = path[i - 1], b = path[i];
        if (lengths[i] > drawTo) b = a + (b - a) * ((drawTo - lengths[i - 1]) / (lengths[i] - lengths[i - 1]));
        m_logoDraw->drawSegment(a, b, width / 2, colorAt(lengths[i] / total));
    }
}

// osu!'s LogoAnimation: thick strokes in the player's two colours trace the
// ring, then the cube and its inner square, each with a thin glow-coloured
// highlight racing along just behind. At the reveal the real logo takes over
// under the flash.
void IntroSequence::drawLogo(float progress) {
    float r = m_logoBaseRadius;
    auto a = m_palette.gradientA, b = m_palette.gradientB, glow = m_palette.rim;
    ccColor3B white {255, 255, 255};
    m_logoDraw->clear();

    drawStroke(m_ringPath, stage(progress, 0.f, 0.7f), r * 0.08f, a, b);
    drawStroke(m_cubePath, stage(progress, 0.18f, 0.8f), r * 0.07f, b, a);
    drawStroke(m_innerPath, stage(progress, 0.4f, 0.92f), r * 0.06f, a, b);

    drawStroke(m_ringPath, stage(progress, 0.08f, 0.8f), r * 0.022f, glow, white);
    drawStroke(m_cubePath, stage(progress, 0.28f, 0.9f), r * 0.02f, glow, white);
    drawStroke(m_innerPath, stage(progress, 0.5f, 1.f), r * 0.018f, glow, white);
}

void IntroSequence::reveal() {
    m_revealed = true;
    m_revealMs = m_timeMs;
    // The song plays on as the menu's; without it, the menu's own music starts
    // now and fades in (osu!'s IntroScreen.StartTrack).
    if (!m_musicTrack) MusicPlayer::get().releaseIntro();
    m_content->setVisible(false);
    this->setKeypadEnabled(false);
    this->setTouchEnabled(false);

    // GameWideFlash: an additive white flash fading out over a second.
    if (auto parent = this->getParent()) {
        auto flash = CCLayerColor::create({255, 255, 255, 255});
        flash->setBlendFunc({GL_SRC_ALPHA, GL_ONE});
        parent->addChild(flash, this->getZOrder() + 1);
        flash->runAction(CCSequence::create(
            CCEaseOut::create(CCFadeTo::create(1.f, 0), 2.f),
            CCRemoveSelf::create(),
            nullptr
        ));
    }
    if (m_onReveal) m_onReveal();
}

void IntroSequence::update(float dt) {
    AudioAnalyzer::get().update(dt);

    // Don't start while the loading screen is still fading into the menu.
    if (!m_started) {
        if (typeinfo_cast<CCTransitionScene*>(CCDirector::get()->getRunningScene())) {
            setMusicVolume(0);
            return;
        }
        start();
        dt = 0;
    }

    float ms = dt * 1000.f;
    m_lastMs = m_timeMs;
    m_timeMs += ms;
    // The song is the clock: a long frame (the menu's textures loading, the
    // game losing focus) would otherwise leave the timeline ahead of or behind
    // what's heard. Small differences are its block granularity; left alone.
    unsigned int position = 0;
    bool havePosition = false;
    if (m_musicTrack) {
        auto engine = FMODAudioEngine::get();
        if (engine->isMusicPlaying(0)) {
            position = engine->getMusicTimeMS(0);
            havePosition = true;
        }
    } else if (m_cue) {
        bool playing = false;
        if (m_cue->isPlaying(&playing) != FMOD_OK || !playing) m_cue = nullptr;
        else if (m_cue->getPosition(&position, FMOD_TIMEUNIT_MS) == FMOD_OK) havePosition = true;
    }
    if (havePosition && std::abs(static_cast<float>(position) - m_timeMs) > 60.f) m_timeMs = static_cast<float>(position);

    if (m_revealed) {
        if (m_musicTrack) {
            this->removeFromParent();
            return;
        }
        float fade = std::min(1.f, (m_timeMs - m_revealMs) / TRACK_FADE);
        setMusicVolume(eased(Easing::OutQuad, fade));
        if (m_cue) m_cue->setVolume(m_cueVolume * (1 - fade));
        if (fade >= 1.f) {
            if (m_cue) m_cue->stop();
            m_cue = nullptr;
            this->removeFromParent();
        }
        return;
    }
    // Our own copy plays: whatever is on the music channel waits for the reveal.
    if (!m_musicTrack) setMusicVolume(0);

    float beatPos = (m_timeMs - FIRST_BEAT_MS) / BEAT_MS; // beats since the first (negative before it)
    float phase = beatPos - std::floor(beatPos);         // how far into the current beat
    float pulse = 1 + 0.16f * std::pow(1 - phase, 3.f);  // a kick on every beat, easing off

    // --- triangles: a trickle from the pad, and a burst on every note, until the icons gather ---
    bool triangles = m_timeMs >= PAD_MS && beatPos < GATHER_BEAT;
    if (triangles) {
        for (int n : ONSETS) {
            float t = beat(n / 4.f);
            if (m_lastMs < t && m_timeMs >= t) spawnTriangles(6, 110);
        }
    }
    updateTriangles(ms, triangles, beatPos < 0 ? 90.f : 45.f);

    // --- icons: bar 1 punch in, bar 2 step together ---
    bool icons = beatPos >= ICONS_BEAT && beatPos < LOGO_BEAT;
    m_icons->setVisible(icons);
    if (icons) {
        int step = std::clamp(static_cast<int>(std::floor(beatPos - GATHER_BEAT)) + 1, 0, static_cast<int>(GATHER.size()));
        float scale = step == 0 ? 1.f : GATHER[step - 1].scale;
        float spacing = step == 0 ? ICON_SPACING : GATHER[step - 1].spacing;
        m_icons->setScale(scale);
        for (size_t i = 0; i < m_iconHolders.size(); i++) {
            float age = m_timeMs - beat(ICONS_BEAT + i);
            auto holder = m_iconHolders[i];
            holder->setVisible(age >= 0);
            if (age < 0) continue;
            holder->setScale(age < PUNCH_MS ? 1.8f - 0.8f * eased(Easing::OutQuint, age / PUNCH_MS) : pulse);
        }
        layoutIcons(spacing);
        // osu! eases the row down over the first bar and up over the second.
        float container = beatPos < GATHER_BEAT
            ? 1.f - 0.2f * (beatPos / GATHER_BEAT)
            : 0.8f + 0.5f * ((beatPos - GATHER_BEAT) / (LOGO_BEAT - GATHER_BEAT));
        m_iconsScale->setScale(container);
    }

    // --- logo: bars 3-4, drawn in while it settles onto the menu's ---
    bool logo = beatPos >= LOGO_BEAT;
    m_logo->setVisible(logo);
    if (logo) {
        float p = std::clamp((beatPos - LOGO_BEAT) / LOGO_BEATS, 0.f, 1.f);
        float container = SCALE_START - SCALE_ADJUST * 0.25f * eased(Easing::InQuad, p);
        float inner = SCALE_START - SCALE_ADJUST * eased(Easing::InQuint, (p - 0.7f) / 0.3f);
        float kick = 1 + 0.04f * std::pow(1 - phase, 3.f);
        m_logoContainer->setScale(container * kick);
        m_logo->setScale(inner);
        drawLogo(p);
    }

    // --- reveal, on the crash ---
    if (beatPos >= REVEAL_BEAT) reveal();
}

} // namespace lazer
