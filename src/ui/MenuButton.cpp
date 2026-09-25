#include "MenuButton.hpp"

#include "../audio/AudioAnalyzer.hpp"
#include "Text.hpp"

#include <Geode/utils/cocos.hpp>
#include <cmath>

using namespace cocos2d;

namespace lazer {

namespace {
    // Values from MainMenuButton.cs.
    constexpr float HOVER_SCALE = 1.2f;
    constexpr float BOUNCE_COMPRESSION = 0.9f;
    constexpr float BOUNCE_ROTATION = 8.f;

    // Used when nothing is playing, so hover still has some life (120 BPM).
    constexpr float FALLBACK_BEAT_MS = 500.f;

    GLubyte toByte(float a) { return static_cast<GLubyte>(std::clamp(a, 0.f, 1.f) * 255.f); }
}

MenuButton* MenuButton::create(
    std::string const& label, std::string const& icon, ccColor3B color,
    Style style, std::function<void()> callback
) {
    auto ret = new MenuButton();
    if (ret->init(label, icon, color, style, std::move(callback))) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

bool MenuButton::init(
    std::string const& label, std::string const& icon, ccColor3B color,
    Style style, std::function<void()> callback
) {
    if (!CCNode::init()) return false;
    m_style = style;
    m_color = color;
    m_callback = std::move(callback);

    m_bg = RoundedBox::create({0, style.height}, 0, {color.r, color.g, color.b, 255});
    m_bg->setShadow(style.height * 0.08f, {0, 0, 0, 51});
    m_bg->setSkewX(CC_RADIANS_TO_DEGREES(std::atan(style.wedge / style.height)));
    this->addChild(m_bg);

    // Content is not sheared, only the background.
    m_content = CCNode::create();
    this->addChild(m_content, 1);

    float h = style.height;
    m_iconHolder = CCNode::create();
    m_iconHolder->setPosition({0, h * 0.04f + h * 0.08f});
    m_content->addChild(m_iconHolder);

    m_icon = makeIcon(icon.c_str(), h * 0.32f);
    m_iconHolder->addChild(m_icon);

    m_label = makeText(label, Weight::SemiBold, h * 0.2f);
    m_label->setAnchorPoint({0.5f, 0.f});
    m_label->setPosition({-h * 0.03f, -h / 2 + h * 0.07f});
    m_content->addChild(m_label);

    m_lastBeat = AudioAnalyzer::get().beatIndex();
    this->setVisible(false);
    this->scheduleUpdate();
    return true;
}

void MenuButton::onEnter() {
    CCNode::onEnter();
    CCDirector::sharedDirector()->getTouchDispatcher()->addTargetedDelegate(this, -129, true);
}

void MenuButton::onExit() {
    CCDirector::sharedDirector()->getTouchDispatcher()->removeDelegate(this);
    CCNode::onExit();
}

void MenuButton::setState(State state, int contractStyle) {
    if (state == m_state) return;
    m_state = state;

    switch (state) {
        case State::Contracted:
            if (contractStyle == 1) {
                m_width.to(0, 400, Easing::InSine);
                m_alpha.to(0, 800, Easing::None);
            } else {
                m_width.to(0, 500, Easing::OutExpo);
                m_alpha.to(0, 500, Easing::None);
            }
            setHovered(false);
            break;
        case State::Expanded:
            m_width.to(m_style.width, 500, Easing::OutExpo);
            m_alpha.to(1, 500 / 6.f, Easing::None);
            break;
        case State::Exploded:
            m_width.to(m_style.width * 2, 200, Easing::OutExpo);
            m_alpha.to(0, 200 / 4.f * 3, Easing::None);
            break;
    }
}

bool MenuButton::acceptsInput() {
    return m_state == State::Expanded && this->isVisible()
        && m_width.get() / m_style.width >= 0.8f;
}

bool MenuButton::containsWorldPoint(CCPoint p) {
    auto local = this->convertToNodeSpace(p);
    // Undo the shear, then it's a plain rectangle check.
    float x = local.x - local.y * (m_style.wedge / m_style.height);
    return std::abs(x) <= m_width.get() / 2 && std::abs(local.y) <= m_style.height / 2;
}

void MenuButton::setHovered(bool hovered) {
    if (hovered == m_hovered) return;
    m_hovered = hovered;

    if (hovered) {
        m_rightward = false;
        m_beatTimer = 0;
        float d = m_beatLength / 2;
        m_iconRotation.to(BOUNCE_ROTATION * (m_rightward ? -1 : 1), d, Easing::InOutSine);
        m_iconScaleX.to(HOVER_SCALE, d, Easing::Out);
        m_iconScaleY.to(HOVER_SCALE * BOUNCE_COMPRESSION, d, Easing::Out);
        m_width.to(m_style.width * 1.5f, 500, Easing::OutElastic);
        m_hoverFlash.to(0.1f, 1000, Easing::OutQuint);
    } else {
        m_halfBeatMs = -1;
        m_iconRotation.to(0, 500, Easing::Out);
        m_iconY.to(0, 500, Easing::Out);
        m_iconScaleX.to(1, 200, Easing::Out);
        m_iconScaleY.to(1, 200, Easing::Out);
        if (m_state == State::Expanded) m_width.to(m_style.width, 500, Easing::OutElastic);
        m_hoverFlash.to(0, 1000, Easing::OutQuint);
    }
}

// Icon wobble while hovered (MainMenuButton.OnNewBeat).
void MenuButton::onBeat(float beatLength) {
    m_beatLength = beatLength;
    float d = beatLength / 2;
    m_halfBeatMs = d;
    m_rightward = !m_rightward;
    m_iconRotation.to(m_rightward ? BOUNCE_ROTATION : -BOUNCE_ROTATION, d * 2, Easing::InOutSine);
    m_iconY.to(m_style.height * 0.1f, d, Easing::Out);
    m_iconScaleX.to(HOVER_SCALE, d, Easing::Out);
    m_iconScaleY.to(HOVER_SCALE, d, Easing::Out);
}

void MenuButton::update(float dt) {
    float ms = dt * 1000.f;
    auto& audio = AudioAnalyzer::get();
    audio.update(dt);
    bool beat = audio.beatIndex() != m_lastBeat;
    m_lastBeat = audio.beatIndex();

    if (m_hovered) {
        if (audio.isPlaying()) {
            if (beat) onBeat(audio.beatLength());
        } else {
            m_beatTimer += ms;
            if (m_beatTimer >= FALLBACK_BEAT_MS) {
                m_beatTimer -= FALLBACK_BEAT_MS;
                onBeat(FALLBACK_BEAT_MS);
            }
        }
        if (m_halfBeatMs >= 0) {
            m_halfBeatMs -= ms;
            if (m_halfBeatMs < 0) {
                // Second half of the bounce: drop back down, squashed.
                float d = m_beatLength / 2;
                m_iconY.to(0, d, Easing::In);
                m_iconScaleY.to(HOVER_SCALE * BOUNCE_COMPRESSION, d, Easing::In);
            }
        }
    }

    m_width.update(dt);
    m_alpha.update(dt);
    m_hoverFlash.update(dt);
    m_iconScaleX.update(dt);
    m_iconScaleY.update(dt);
    m_iconRotation.update(dt);
    m_iconY.update(dt);

    float width = std::max(0.f, m_width.get());
    float alpha = m_alpha.get();
    this->setVisible(alpha > 0.001f || width > 0.5f);

    bool hovered = acceptsInput() && containsWorldPoint(geode::cocos::getMousePos());
    setHovered(hovered);

    m_bg->setContentSize({width, m_style.height});
    // Hover layer: blend the fill towards white (osu uses an additive white box).
    float f = std::clamp(m_hoverFlash.get(), 0.f, 1.f);
    m_bg->setFillColor({
        static_cast<GLubyte>(m_color.r + (255 - m_color.r) * f),
        static_cast<GLubyte>(m_color.g + (255 - m_color.g) * f),
        static_cast<GLubyte>(m_color.b + (255 - m_color.b) * f),
        255,
    });
    m_bg->setOpacity(toByte(alpha));

    // Content fades in only once the background is mostly open.
    float contentAlpha = std::clamp((width / m_style.width - 0.5f) / 0.3f, 0.f, 1.f) * alpha;
    m_icon->setOpacity(toByte(contentAlpha));
    m_label->setOpacity(toByte(contentAlpha));

    m_iconHolder->setScaleX(m_iconScaleX);
    m_iconHolder->setScaleY(m_iconScaleY);
    m_iconHolder->setRotation(m_iconRotation);
    m_iconHolder->setPositionY(m_style.height * 0.12f + m_iconY.get());
}

bool MenuButton::ccTouchBegan(CCTouch* touch, CCEvent*) {
    if (!acceptsInput() || !containsWorldPoint(touch->getLocation())) return false;
    m_pressed = true;
    return true;
}

void MenuButton::ccTouchEnded(CCTouch* touch, CCEvent*) {
    if (!m_pressed) return;
    m_pressed = false;
    if (!acceptsInput() || !containsWorldPoint(touch->getLocation())) return;
    trigger();
}

void MenuButton::trigger() {
    m_hoverFlash.set(0.9f);
    m_hoverFlash.to(0, 800, Easing::OutExpo);
    if (m_explodes) setState(State::Exploded);
    if (m_callback) m_callback();
}

} // namespace lazer
