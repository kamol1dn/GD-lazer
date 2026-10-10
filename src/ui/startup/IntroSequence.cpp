#include "IntroSequence.hpp"

#include "../../audio/AudioAnalyzer.hpp"
#include "../../audio/MusicPlayer.hpp"
#include "../../audio/Sfx.hpp"
#include "../../integrations/ModIntegrations.hpp"
#include "../core/Text.hpp"

#include <Geode/fmod/fmod.hpp>
#include <array>
#include <random>

using namespace geode::prelude;

namespace lazer {

namespace {
    // Dash by MDK as it ships with GD (Resources/Dash.mp3), measured from the
    // file: 128 BPM in 4/4, the first beat at 1.149 s, the voice saying
    // "Geometry Dash" at 15.0 s ("Dash" from about 15.75 s) into the first
    // drop at 16.149 s (bar 9).
    constexpr float BPM = 128;
    constexpr float BEAT_MS = 60000.f / BPM; // 468.75
    constexpr float FIRST_BEAT_MS = 1149;

    // The bar before the drop (bar 8, beats 28-31 from the song's first):
    // longer dragged on every start.
    constexpr int START_BEAT = 28;
    constexpr unsigned START_MS = static_cast<unsigned>(FIRST_BEAT_MS + START_BEAT * BEAT_MS); // 14274
    constexpr float HOP_MS = 14743;    // beat 29: the cube hops
    constexpr float WRITE_MS = 15000;  // "Geometry": the cube dashes right, writing it
    constexpr float WRITE_END_MS = 15450;
    constexpr float DASH_MS = 15750;   // "Dash": DASH slams in
    constexpr float DROP_MS = 16149;   // the drop: the flash
    // Starting mid-song, it fades in over the first beats.
    constexpr float FADE_IN_MS = 1000;
    // After the flash the words zoom through the camera, over the menu.
    constexpr float ZOOM_MS = 260;

    // The stabs and kicks of the bar (the song's onsets), for a burst of
    // streaks and a nudge of the camera on each.
    constexpr std::array<float, 8> HITS {{14274, 14392, 14626, 14743, 14990, 15212, 15460, 15680}};

    // Without the music player: the menu's music fades in as the intro's copy
    // of the song fades out after the reveal.
    constexpr float TRACK_FADE = 1800;

    constexpr float CUBE_SIZE = 44;
    constexpr float TRAIL_MS = 260;
    constexpr float GEOMETRY_SIZE = 54;
    constexpr float DASH_SIZE = 104;
    constexpr float LETTER_SPACING = 4;
    constexpr float LETTER_IN_MS = 160;

    float eased(Easing easing, float t) {
        return static_cast<float>(ease(easing, std::clamp(t, 0.f, 1.f)));
    }
    float lerp(float a, float b, float t) { return a + (b - a) * t; }

    std::mt19937& rng() {
        static std::mt19937 r {std::random_device {}()};
        return r;
    }
    float random01() { return std::uniform_real_distribution<float>(0.f, 1.f)(rng()); }
    float randomBetween(float lo, float hi) { return lerp(lo, hi, random01()); }

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

IntroSequence* IntroSequence::create(std::function<void()> onReveal) {
    auto ret = new IntroSequence();
    if (ret->init(std::move(onReveal))) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

bool IntroSequence::init(std::function<void()> onReveal) {
    if (!CCLayer::init()) return false;
    m_onReveal = std::move(onReveal);
    // The music player's copy needs no preparing; our own is opened now,
    // during the loading screen's fade, not on the first frame.
    if (!MusicPlayer::get().introTrackPossible()) sfx::preloadFile(dashPath());
    m_win = CCDirector::get()->getWinSize();
    m_k = m_win.height / 768.f;
    m_palette = PlayerPalette::current();
    CCPoint center = m_win / 2;

    // Black until the menu is revealed; the camera shake moves everything
    // over it, so the backdrop is a little larger than the screen.
    auto backdrop = CCLayerColor::create({0, 0, 0, 255}, m_win.width + 80 * m_k, m_win.height + 80 * m_k);
    backdrop->setPosition({-40 * m_k, -40 * m_k});
    this->addChild(backdrop, -1);

    m_content = CCNode::create();
    this->addChild(m_content);

    m_streakDraw = CCDrawNode::create();
    m_streakDraw->setBlendFunc({GL_ONE, GL_ONE}); // additive
    m_content->addChild(m_streakDraw);

    m_trailDraw = CCDrawNode::create();
    m_trailDraw->setBlendFunc({GL_ONE, GL_ONE});
    m_content->addChild(m_trailDraw, 1);

    m_cubeSize = CUBE_SIZE * m_k;
    m_cube = integrations::playerIcon(false, m_cubeSize);
    if (m_cube) {
        m_cube->setVisible(false);
        m_content->addChild(m_cube, 2);
    }

    // GEOMETRY, a label per letter so each can land on its own, over DASH.
    m_text = CCNode::create();
    m_text->setPosition(center);
    m_content->addChild(m_text, 3);
    for (char c : std::string("GEOMETRY")) {
        auto label = makeText(std::string(1, c), Weight::SemiBold, GEOMETRY_SIZE * m_k);
        label->setOpacity(0);
        m_text->addChild(label);
        m_letters.push_back(label);
    }
    layoutLetters(LETTER_SPACING * m_k);
    m_letterX.clear();
    for (auto label : m_letters) m_letterX.push_back(label->getPositionX());

    m_dash = makeText("DASH", Weight::Bold, DASH_SIZE * m_k);
    m_dash->setColor(m_palette.rim);
    m_dash->setOpacity(0);
    m_dash->setPosition({0, -46 * m_k});
    m_text->addChild(m_dash);

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
    m_musicTrack = MusicPlayer::get().startIntroTrack(START_MS);
    if (!m_musicTrack) {
        m_cue = sfx::playCueFile(dashPath(), START_MS);
        if (m_cue) {
            m_cue->getVolume(&m_cueVolume);
            m_cue->setVolume(0);
        }
        log::info("Intro plays its own copy of Dash ({})", m_cue ? "ok" : "failed to open");
    }
}

void IntroSequence::layoutLetters(float spacing) {
    std::vector<float> widths;
    float total = 0;
    for (auto label : m_letters) {
        float w = label->getContentSize().width * label->getScaleX();
        widths.push_back(w);
        total += w;
    }
    if (!m_letters.empty()) total += spacing * (m_letters.size() - 1);
    float x = -total / 2;
    for (size_t i = 0; i < m_letters.size(); i++) {
        m_letters[i]->setPositionX(x + widths[i] / 2);
        x += widths[i] + spacing;
    }
}

// Streaks start past the right edge and rush left. The further from the
// middle line, the faster and longer: the edges of the view fly by.
void IntroSequence::spawnStreaks(int count, float widthScale, float alphaScale) {
    for (int i = 0; i < count; i++) {
        float y = random01() * m_win.height;
        float depth = std::abs(y - m_win.height / 2) / (m_win.height / 2); // 0 middle .. 1 edge
        float speed = lerp(1400, 3400, depth) * m_k * randomBetween(0.8f, 1.2f);
        m_streaks.push_back({
            m_win.width + randomBetween(0, 300) * m_k, y,
            lerp(60, 320, depth) * m_k * randomBetween(0.6f, 1.4f),
            lerp(1.2f, 2.6f, depth) * m_k * widthScale,
            speed,
            lerp(0.12f, 0.5f, depth) * alphaScale,
        });
    }
}

void IntroSequence::updateStreaks(float dt, float rate, float brightness) {
    m_streakClock += dt * rate;
    while (m_streakClock >= 1) {
        m_streakClock -= 1;
        spawnStreaks(1, 1, 1);
    }
    for (auto& s : m_streaks) s.x -= s.speed * dt;
    std::erase_if(m_streaks, [](Streak const& s) { return s.x + s.length < 0; });

    m_streakDraw->clear();
    for (auto const& s : m_streaks) {
        float a = std::min(1.f, s.alpha * brightness);
        // Bright at the head, trailing off behind it.
        ccColor4F head {a, a, a, a};
        CCPoint from {s.x - s.length, s.y}, to {s.x, s.y};
        m_streakDraw->drawSegment(from, to, s.width / 2, {a * 0.35f, a * 0.35f, a * 0.35f, a * 0.35f});
        m_streakDraw->drawSegment(from + (to - from) * 0.6f, to, s.width / 2, head);
    }
}

// In from the left to its mark on the first beat, a hop with a quarter turn
// on the second, then off to the right from "Geometry", writing as it goes.
// A trail in your colours follows while it's moving fast.
void IntroSequence::updateCube(float dt) {
    if (!m_cube) return;
    float t = m_timeMs;
    float cx = m_win.width / 2, cy = m_win.height / 2;
    float mark = cx - 250 * m_k;          // where it waits, left of the words
    float baseY = cy + 26 * m_k;          // on GEOMETRY's line
    float x, y = baseY, rotation = 0;
    bool visible = true;

    if (t < WRITE_MS) {
        float in = eased(Easing::OutQuint, (t - START_MS) / 330.f);
        x = lerp(-m_cubeSize, mark, in);
        if (t >= HOP_MS) {
            // A GD jump: up and down over ~350 ms, turning a quarter on the way.
            float p = std::clamp((t - HOP_MS) / 350.f, 0.f, 1.f);
            y += 70 * m_k * 4 * p * (1 - p);
            rotation = 90 * eased(Easing::OutQuad, p);
        }
    } else {
        rotation = 90;
        float p = (t - WRITE_MS) / (WRITE_END_MS - WRITE_MS);
        x = lerp(mark, m_win.width + m_cubeSize * 2, eased(Easing::InQuad, p));
        rotation += 180 * std::clamp(p, 0.f, 1.2f);
        visible = p < 1.2f;
    }
    // Beats land as a little bounce.
    float beatPos = (t - FIRST_BEAT_MS) / BEAT_MS;
    float phase = beatPos - std::floor(beatPos);
    float pulse = 1 + 0.1f * std::pow(1 - phase, 3.f);

    m_cube->setVisible(visible);
    m_cube->setPosition({x, y});
    m_cube->setRotation(rotation);
    m_cube->setScale(pulse);

    // Trail: the last quarter second of positions, fading.
    for (auto& p : m_trail) p.ageMs += dt * 1000;
    std::erase_if(m_trail, [](TrailPoint const& p) { return p.ageMs >= TRAIL_MS; });
    if (visible) m_trail.push_back({{x, y}, 0});
    m_trailDraw->clear();
    auto a = m_palette.gradientA, b = m_palette.gradientB;
    for (size_t i = 1; i < m_trail.size(); i++) {
        auto const& p0 = m_trail[i - 1];
        auto const& p1 = m_trail[i];
        float dist = ccpDistance(p0.pos, p1.pos);
        if (dist < 0.5f) continue;
        float life = 1 - p1.ageMs / TRAIL_MS;
        float alpha = life * life * std::min(1.f, dist / (6 * m_k)) * 0.9f;
        float mix = static_cast<float>(i) / m_trail.size();
        ccColor4F color {
            lerp(a.r, b.r, mix) / 255.f * alpha, lerp(a.g, b.g, mix) / 255.f * alpha,
            lerp(a.b, b.b, mix) / 255.f * alpha, alpha,
        };
        m_trailDraw->drawSegment(p0.pos, p1.pos, m_cubeSize * 0.32f * life, color);
    }
}

// GEOMETRY appears a letter at a time as the cube passes each (stretched tall
// and dropping into place); DASH slams in on the word with a shake; after
// that the letters drift apart and the words swell towards the drop.
void IntroSequence::updateText() {
    float t = m_timeMs;
    float cx = m_win.width / 2;
    float mark = cx - 250 * m_k;
    float right = m_win.width + m_cubeSize * 2;

    float spread = t >= DASH_MS ? eased(Easing::OutQuad, (t - DASH_MS) / (DROP_MS - DASH_MS)) : 0;
    layoutLetters((LETTER_SPACING + 9 * spread) * m_k);
    m_text->setScale(1 + 0.06f * spread);

    for (size_t i = 0; i < m_letters.size(); i++) {
        auto label = m_letters[i];
        // When the cube's centre crosses this letter (its motion is InQuad over the dash).
        float u = std::clamp((cx + m_letterX[i] - mark) / (right - mark), 0.f, 1.f);
        float at = WRITE_MS + std::sqrt(u) * (WRITE_END_MS - WRITE_MS);
        float age = t - at;
        if (age < 0) {
            label->setOpacity(0);
            continue;
        }
        float p = eased(Easing::OutQuint, age / LETTER_IN_MS);
        label->setOpacity(static_cast<GLubyte>(255 * std::min(1.f, age / 60.f)));
        label->setScaleY(lerp(2.4f, 1.f, p));
        label->setScaleX(lerp(0.7f, 1.f, p));
        label->setPositionY(lerp(34 * m_k, 26 * m_k, p));
    }

    float age = t - DASH_MS;
    if (age < 0) {
        m_dash->setOpacity(0);
    } else {
        float p = eased(Easing::OutQuint, age / 200.f);
        m_dash->setOpacity(static_cast<GLubyte>(255 * std::min(1.f, age / 50.f)));
        m_dash->setScale(lerp(3.2f, 1.f, p));
    }
}

void IntroSequence::reveal() {
    m_revealed = true;
    m_revealMs = m_timeMs;
    // The song plays on as the menu's; without it, the menu's own music starts
    // now and fades in (osu!'s IntroScreen.StartTrack).
    if (!m_musicTrack) MusicPlayer::get().releaseIntro();
    this->setKeypadEnabled(false);
    this->setTouchEnabled(false);

    // Everything but the words goes; they zoom through over the menu.
    for (auto child : CCArrayExt<CCNode*>(this->getChildren())) {
        if (child != m_content) child->setVisible(false);
    }
    m_streakDraw->setVisible(false);
    m_trailDraw->setVisible(false);
    if (m_cube) m_cube->setVisible(false);
    m_content->setPosition({0, 0});

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
    if (m_lastMs < 0) m_timeMs = START_MS; // the song's time, from where it starts
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

    float fadeIn = eased(Easing::OutQuad, (m_timeMs - START_MS) / FADE_IN_MS);
    if (m_revealed) {
        // The words zoom through the camera, then it's all gone (our own copy
        // of the song fading out under the menu's music first).
        float zoom = (m_timeMs - m_revealMs) / ZOOM_MS;
        if (zoom < 1) {
            m_text->setScale(1 + 3.5f * eased(Easing::InQuad, zoom));
            auto alpha = static_cast<GLubyte>(255 * (1 - eased(Easing::OutQuad, zoom)));
            for (auto label : m_letters) {
                if (label->getOpacity() > 0) label->setOpacity(std::min(label->getOpacity(), alpha));
            }
            if (m_dash->getOpacity() > 0) m_dash->setOpacity(std::min(m_dash->getOpacity(), alpha));
        } else {
            m_text->setVisible(false);
        }
        if (m_musicTrack) {
            setMusicVolume(1);
            if (zoom >= 1) this->removeFromParent();
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
    if (m_musicTrack) {
        setMusicVolume(fadeIn);
    } else {
        // Our own copy plays: whatever is on the music channel waits for the reveal.
        setMusicVolume(0);
        if (m_cue) m_cue->setVolume(m_cueVolume * fadeIn);
    }

    // --- the bar's hits: a burst of streaks and a nudge of the camera on each; DASH's slam a kick ---
    float shake = 0;
    for (float hit : HITS) {
        if (m_lastMs < hit && m_timeMs >= hit) spawnStreaks(14, 1.8f, 1.6f);
        float age = m_timeMs - hit;
        if (age >= 0) shake = std::max(shake, 2.5f * std::exp(-age / 70.f));
    }
    if (m_lastMs < DASH_MS && m_timeMs >= DASH_MS) spawnStreaks(30, 2.2f, 1.8f);
    if (m_timeMs >= DASH_MS) shake = std::max(shake, 11.f * std::exp(-(m_timeMs - DASH_MS) / 90.f));
    m_content->setPosition({randomBetween(-shake, shake) * m_k, randomBetween(-shake, shake) * m_k});

    // --- streaks: picking up through the bar, pouring in towards the drop ---
    float build = std::clamp((m_timeMs - START_MS) / (DROP_MS - START_MS), 0.f, 1.f);
    float rate = lerp(25, 90, build) + (m_timeMs >= DASH_MS ? 120 * eased(Easing::InQuad, (m_timeMs - DASH_MS) / (DROP_MS - DASH_MS)) : 0);
    float brightness = (0.7f + 0.5f * build) * (0.6f + 0.4f * fadeIn);
    updateStreaks(dt, rate, brightness);

    updateCube(dt);
    updateText();

    // --- reveal, on the drop ---
    if (m_timeMs >= DROP_MS) reveal();
}

} // namespace lazer
