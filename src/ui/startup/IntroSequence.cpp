#include "IntroSequence.hpp"

#include "../../audio/AudioAnalyzer.hpp"
#include "../../audio/MusicPlayer.hpp"
#include "../../integrations/ModIntegrations.hpp"
#include "../core/Text.hpp"

#include <Geode/fmod/fmod.hpp>
#include <random>

using namespace geode::prelude;

namespace lazer {

namespace {
    // IntroTriangles.TrianglesIntroSequence
    constexpr float TEXT_1 = 200;
    constexpr float TEXT_2 = 400;
    constexpr float TEXT_3 = 700;
    constexpr float TEXT_4 = 900;
    constexpr float TEXT_GLITCH = 1060;
    constexpr float RULESETS_1 = 1450;
    constexpr float RULESETS_2 = 1650;
    constexpr float RULESETS_3 = 1850;
    constexpr float LOGO_SCALE_DURATION = 920;
    constexpr float LOGO_1 = 2080;
    constexpr float LOGO_2 = LOGO_1 + LOGO_SCALE_DURATION;
    constexpr float SCALE_START = 1.2f;
    constexpr float SCALE_ADJUST = 0.8f;

    // IntroScreen.StartTrack for a non-theme song: starts at silence and swells in.
    constexpr float TRACK_START = 600;  // IntroCircles.TRACK_START_DELAY
    constexpr float TRACK_FADE = 2600;
    // The reveal waits for the song's next beat inside this window.
    constexpr float REVEAL_EARLIEST = LOGO_2 - 50;
    constexpr float REVEAL_LATEST = LOGO_2 + 300;

    constexpr float TIME_BETWEEN_TRIANGLES = 22;
    constexpr float TRIANGLE_LIFE = 120;
    constexpr float RULESET_ICON = 30;

    constexpr ccColor4B PINK {255, 102, 170, 255};

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
    m_win = CCDirector::get()->getWinSize();
    m_k = m_win.height / 768.f;
    CCPoint center = m_win / 2;

    m_content = CCNode::create();
    this->addChild(m_content);

    // IntroScreen.CreateBackground: black until the menu is revealed.
    m_content->addChild(CCLayerColor::create({0, 0, 0, 255}), -1);

    m_triangleDraw = CCDrawNode::create();
    m_triangleDraw->setBlendFunc({GL_ONE, GL_ONE}); // additive
    m_content->addChild(m_triangleDraw);

    m_text = CCNode::create();
    m_text->setPosition(center + CCPoint(0, 5 * m_k)); // Padding.Bottom = 10
    m_content->addChild(m_text, 1);

    // osu!'s ruleset icons, as GD's building blocks: your cube, a spike, an orb and a trigger.
    m_rulesetsScale = CCNode::create();
    m_rulesetsScale->setPosition(center);
    m_content->addChild(m_rulesetsScale, 2);
    m_rulesets = CCNode::create();
    m_rulesets->setVisible(false);
    m_rulesetsScale->addChild(m_rulesets);

    float icon = RULESET_ICON * m_k;
    m_icons.push_back(integrations::playerIcon(false, icon));
    for (auto frame : {"spike_01_001.png", "ring_01_001.png", "edit_eMoveComBtn_001.png"}) {
        auto sprite = CCSprite::createWithSpriteFrameName(frame);
        if (!sprite) sprite = CCSprite::create();
        m_icons.push_back(fitted(sprite, icon));
    }
    for (auto node : m_icons) {
        if (node) m_rulesets->addChild(node);
    }
    std::erase(m_icons, nullptr);

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
    m_logoDisc = RoundedBox::create({r * 2, r * 2}, r, PINK);
    m_logoDisc->setScale(0);
    m_logo->addChild(m_logoDisc);
    m_logoText = makeText("GD", Weight::Bold, r * 0.95f);
    m_logoText->setOpacity(0);
    m_logo->addChild(m_logoText, 1);
    m_logoRing = CCDrawNode::create();
    m_logo->addChild(m_logoRing, 2);

    this->setTouchEnabled(true);
    this->setKeypadEnabled(true);
    setMusicVolume(0);
    this->scheduleUpdate();
    return true;
}

void IntroSequence::registerWithTouchDispatcher() {
    CCDirector::get()->getTouchDispatcher()->addTargetedDelegate(this, -600, true);
}

void IntroSequence::setMusicVolume(float volume) {
    if (auto channel = FMODAudioEngine::get()->getActiveMusicChannel(0)) channel->setVolume(volume);
}

void IntroSequence::setText(std::string const& text) {
    m_text->removeAllChildren();
    m_chars.clear();
    for (char c : text) {
        auto label = makeText(std::string(1, c), Weight::Regular, 42 * m_k);
        m_text->addChild(label);
        m_chars.push_back(label);
    }
    layoutText();
}

void IntroSequence::layoutText() {
    float spacing = m_spacing * m_k;
    std::vector<float> widths;
    float total = 0;
    for (auto label : m_chars) {
        float w = label->getScaledContentSize().width;
        if (std::string_view(label->getString()) == " ") w = std::max(w, 42 * m_k * 0.25f);
        widths.push_back(w);
        total += w;
    }
    if (!m_chars.empty()) total += spacing * (m_chars.size() - 1);
    float x = -total / 2;
    for (size_t i = 0; i < m_chars.size(); i++) {
        m_chars[i]->setPosition({x + widths[i] / 2, 0});
        x += widths[i] + spacing;
    }
}

// GlitchingTriangles: a new triangle every 22 ms, each flashing out over 120 ms.
void IntroSequence::updateTriangles(float ms) {
    for (auto& t : m_triangles) t.ageMs += ms;
    std::erase_if(m_triangles, [](Triangle const& t) { return t.ageMs >= TRIANGLE_LIFE; });

    if (m_trianglesOn) {
        m_triangleClock += ms;
        while (m_triangleClock >= TIME_BETWEEN_TRIANGLES) {
            m_triangleClock -= TIME_BETWEEN_TRIANGLES;
            m_triangles.push_back({{random01(), random01()}, (random01() + 0.2f) * 80 * m_k, random01() < 0.5f, 0});
        }
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

void IntroSequence::layoutRulesets() {
    float icon = RULESET_ICON * m_k, spacing = m_rulesetSpacing * m_k;
    float total = icon * m_icons.size() + spacing * (m_icons.size() - 1);
    for (size_t i = 0; i < m_icons.size(); i++) {
        m_icons[i]->setPosition({-total / 2 + icon / 2 + i * (icon + spacing), 0});
    }
}

// The logo's white rim drawn round clockwise from the top, the pink disc
// filling in behind it and the "GD" fading in last.
void IntroSequence::drawLogo(float progress) {
    float r = m_logoBaseRadius;
    float rim = r * 0.08f;
    m_logoRing->clear();
    int segments = std::max(1, static_cast<int>(96 * progress));
    constexpr float PI = 3.14159265f;
    auto at = [&](float t) {
        float angle = PI / 2 - 2 * PI * t;
        return CCPoint(std::cos(angle), std::sin(angle)) * (r - rim / 2);
    };
    if (progress > 0) {
        for (int i = 0; i < segments; i++) {
            m_logoRing->drawSegment(at(progress * i / segments), at(progress * (i + 1) / segments), rim / 2, {1, 1, 1, 1});
        }
    }
    float fill = std::clamp((progress - 0.25f) / 0.75f, 0.f, 1.f);
    m_logoDisc->setScale(static_cast<float>(ease(Easing::OutCubic, fill)));
    m_logoText->setOpacity(static_cast<GLubyte>(std::clamp((progress - 0.6f) / 0.4f, 0.f, 1.f) * 255));
}

void IntroSequence::reveal() {
    m_revealed = true;
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
    auto& audio = AudioAnalyzer::get();
    audio.update(dt);

    // Don't start while the loading screen is still fading into the menu.
    if (!m_started) {
        if (typeinfo_cast<CCTransitionScene*>(CCDirector::get()->getRunningScene())) {
            setMusicVolume(0);
            return;
        }
        m_started = true;
        dt = 0;
    }

    float ms = dt * 1000.f;
    m_lastMs = m_timeMs;
    m_timeMs += ms;
    auto crossed = [this](float t) { return m_lastMs < t && m_timeMs >= t; };

    // --- music ---
    if (crossed(TRACK_START)) {
        MusicPlayer::get().releaseIntro();
        m_trackStarted = true;
    }
    float fade = m_timeMs < TRACK_START ? 0.f : std::min(1.f, (m_timeMs - TRACK_START) / TRACK_FADE);
    setMusicVolume(static_cast<float>(ease(Easing::InCubic, fade)));

    if (m_revealed) {
        if (fade >= 1.f) this->removeFromParent();
        return;
    }

    // --- text ---
    if (crossed(TEXT_1)) setText("wel");
    if (crossed(TEXT_2)) setText("welcome");
    if (crossed(TEXT_3)) setText("welcome to");
    if (crossed(TEXT_4)) setText("welcome to geometry dash");
    if (m_timeMs >= TEXT_4 && !m_chars.empty()) {
        m_spacing = 5 + 45 * std::min(1.f, (m_timeMs - TEXT_4) / 5000.f);
        layoutText();
    }
    if (crossed(TEXT_GLITCH)) m_trianglesOn = true;

    // --- rulesets ---
    if (crossed(RULESETS_1)) {
        m_rulesetsScaleTween.to(0.8f, 1000, Easing::None);
        m_rulesets->setVisible(true);
        m_rulesets->setScale(1);
        m_rulesetSpacing = 200;
        m_text->setVisible(false);
        m_trianglesOn = false;
        m_triangles.clear();
    }
    if (crossed(RULESETS_2)) {
        m_rulesets->setScale(2);
        m_rulesetSpacing = 30;
    }
    if (crossed(RULESETS_3)) {
        m_rulesets->setScale(4);
        m_rulesetSpacing = 10;
        m_rulesetsScaleTween.to(1.3f, 1000, Easing::None);
    }
    m_rulesetsScaleTween.update(dt);
    m_rulesetsScale->setScale(m_rulesetsScaleTween);
    if (m_rulesets->isVisible()) layoutRulesets();

    // --- logo ---
    if (crossed(LOGO_1)) {
        m_rulesets->setVisible(false);
        m_logo->setVisible(true);
        m_logoScale.set(SCALE_START);
        m_logoContainerScale.set(SCALE_START);
        m_logoContainerScale.to(SCALE_START - SCALE_ADJUST * 0.25f, LOGO_SCALE_DURATION, Easing::InQuad);
    }
    if (crossed(LOGO_1 + LOGO_SCALE_DURATION * 0.7f)) {
        m_logoScale.to(SCALE_START - SCALE_ADJUST, LOGO_SCALE_DURATION * 0.3f, Easing::InQuint);
    }
    if (m_logo->isVisible()) {
        m_logoScale.update(dt);
        m_logoContainerScale.update(dt);
        m_logo->setScale(m_logoScale);
        m_logoContainer->setScale(m_logoContainerScale);
        drawLogo(std::min(1.f, (m_timeMs - LOGO_1) / LOGO_SCALE_DURATION));
    }

    updateTriangles(ms);

    // --- reveal, on the song's beat ---
    if (crossed(REVEAL_EARLIEST)) m_beatAtStart = audio.beatIndex();
    bool onBeat = m_timeMs >= REVEAL_EARLIEST && audio.beatIndex() != m_beatAtStart;
    if (onBeat || m_timeMs >= REVEAL_LATEST) reveal();
}

} // namespace lazer
