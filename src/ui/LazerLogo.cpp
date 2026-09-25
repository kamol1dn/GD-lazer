#include "LazerLogo.hpp"

#include "../audio/AudioAnalyzer.hpp"
#include "Text.hpp"

#include <Geode/utils/cocos.hpp>

using namespace cocos2d;

namespace lazer {

namespace {
    constexpr ccColor4B PINK {255, 102, 170, 255}; // osu!'s signature #ff66aa
    constexpr ccColor4B WHITE {255, 255, 255, 255};
    // OsuLogo.cs
    constexpr float EARLY_ACTIVATION = 60.f;
    constexpr float VISUALISER_ALPHA = 0.5f;

    GLubyte toByte(float a) { return static_cast<GLubyte>(std::clamp(a, 0.f, 1.f) * 255.f); }
}

LazerLogo* LazerLogo::create(float radius) {
    auto ret = new LazerLogo();
    if (ret->init(radius)) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

bool LazerLogo::init(float radius) {
    if (!CCNode::init()) return false;
    m_radius = radius;

    CCSize size {radius * 2, radius * 2};
    this->setContentSize(size);
    this->setAnchorPoint({0.5f, 0.5f});

    auto makeLayer = [&](CCNode* parent) {
        auto n = CCNode::create();
        n->setContentSize(size);
        n->setAnchorPoint({0.5f, 0.5f});
        n->setPosition(size / 2);
        parent->addChild(n);
        return n;
    };
    m_bounce = makeLayer(this);
    m_beat = makeLayer(m_bounce);
    m_amplitudeNode = makeLayer(m_beat);
    m_hover = makeLayer(m_amplitudeNode);

    m_visualiser = LogoVisualisation::create(size.width);
    m_visualiser->setPosition(size / 2);
    m_visualiser->setOpacity(toByte(VISUALISER_ALPHA));
    m_hover->addChild(m_visualiser, -2);

    m_disc = RoundedBox::create(size, radius, PINK);
    m_disc->setBorder(radius * 0.08f, WHITE);
    m_disc->setShadow(radius * 0.25f, {0, 0, 0, 90});
    m_disc->setPosition(size / 2);
    m_hover->addChild(m_disc);

    auto label = makeText("GD", Weight::Bold, radius * 0.95f);
    label->setPosition(size / 2);
    m_hover->addChild(label);

    // Soft white copy of the logo that swells outwards on each beat.
    m_ripple = RoundedBox::create(size, radius, WHITE);
    m_ripple->setPosition(size / 2);
    m_ripple->setOpacity(0);
    m_bounce->addChild(m_ripple, -1);

    // Ring that expands and fades out on impact.
    m_impact = RoundedBox::create(size, radius, {0, 0, 0, 0});
    m_impact->setBorder(radius * 0.06f, WHITE);
    m_impact->setPosition(size / 2);
    m_impact->setOpacity(0);
    m_bounce->addChild(m_impact, -1);

    m_lastBeat = AudioAnalyzer::get().beatIndex();
    this->scheduleUpdate();
    return true;
}

void LazerLogo::onEnter() {
    CCNode::onEnter();
    CCDirector::sharedDirector()->getTouchDispatcher()->addTargetedDelegate(this, -130, true);
}

void LazerLogo::onExit() {
    CCDirector::sharedDirector()->getTouchDispatcher()->removeDelegate(this);
    CCNode::onExit();
}

bool LazerLogo::containsWorldPoint(CCPoint p) {
    auto local = m_disc->convertToNodeSpace(p);
    auto center = m_disc->getContentSize() / 2;
    return ccpDistance(local, center) <= m_radius;
}

// OsuLogo.OnNewBeat
void LazerLogo::onBeat(float amplitude, float beatLength) {
    float adjust = std::min(1.f, 0.4f + amplitude);
    m_beatLength = beatLength;

    m_beatScale.to(1 - 0.02f * adjust, EARLY_ACTIVATION, Easing::Out);
    m_beatSecondPhaseMs = EARLY_ACTIVATION;

    m_rippleScale.set(m_amplitudeScale);
    m_rippleScale.to(m_amplitudeScale * (1 + 0.04f * adjust), beatLength, Easing::OutQuint);
    m_rippleAlpha.set(0.15f * adjust);
    m_rippleAlpha.to(0, beatLength, Easing::OutQuint);
}

void LazerLogo::update(float dt) {
    float ms = dt * 1000.f;
    auto& audio = AudioAnalyzer::get();
    audio.update(dt);

    if (audio.beatIndex() != m_lastBeat) {
        m_lastBeat = audio.beatIndex();
        onBeat(audio.amplitude(), audio.beatLength());
    }
    if (m_beatSecondPhaseMs >= 0) {
        m_beatSecondPhaseMs -= ms;
        if (m_beatSecondPhaseMs < 0) m_beatScale.to(1, m_beatLength * 2, Easing::OutQuint);
    }

    bool hovered = this->isVisible() && containsWorldPoint(geode::cocos::getMousePos());
    if (hovered != m_hovered) {
        m_hovered = hovered;
        m_hoverScale.to(hovered ? 1.1f : 1.f, 500, Easing::OutElastic);
    }

    // Louder music -> logo shrinks slightly (OsuLogo.Update).
    constexpr float cutoff = 0.4f;
    float target = 1.f - std::max(0.f, audio.amplitude() - cutoff) * 0.04f;
    m_amplitudeScale = damp(m_amplitudeScale, target, 0.9, ms);

    for (auto t : {&m_bounceScale, &m_hoverScale, &m_impactScale, &m_impactAlpha,
                   &m_beatScale, &m_rippleScale, &m_rippleAlpha}) {
        t->update(dt);
    }

    m_bounce->setScale(m_bounceScale);
    m_beat->setScale(m_beatScale);
    m_amplitudeNode->setScale(m_amplitudeScale);
    m_hover->setScale(m_hoverScale);
    m_impact->setScale(m_impactScale);
    m_impact->setOpacity(toByte(m_impactAlpha.get()));
    m_ripple->setScale(m_rippleScale);
    m_ripple->setOpacity(toByte(m_rippleAlpha.get()));
}

void LazerLogo::playImpact() {
    m_impactScale.set(1.f);
    m_impactScale.to(1.12f, 250, Easing::None);
    m_impactAlpha.set(1.f);
    m_impactAlpha.to(0.f, 250, Easing::InQuint);
}

bool LazerLogo::ccTouchBegan(CCTouch* touch, CCEvent*) {
    if (!this->isVisible() || !containsWorldPoint(touch->getLocation())) return false;
    m_pressed = true;
    m_bounceScale.to(0.9f, 1000, Easing::Out);
    return true;
}

void LazerLogo::ccTouchEnded(CCTouch* touch, CCEvent*) {
    if (!m_pressed) return;
    m_pressed = false;
    m_bounceScale.to(1.f, 500, Easing::OutElastic);
    if (containsWorldPoint(touch->getLocation())) {
        if (m_callback) m_callback();
    }
}

void LazerLogo::ccTouchCancelled(CCTouch*, CCEvent*) {
    m_pressed = false;
    m_bounceScale.to(1.f, 500, Easing::OutElastic);
}

} // namespace lazer
