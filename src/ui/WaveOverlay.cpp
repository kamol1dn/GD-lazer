#include "WaveOverlay.hpp"

#include "../audio/Sfx.hpp"
#include "Text.hpp"

#include <Geode/Geode.hpp>

using namespace geode::prelude;

namespace lazer {

namespace {
    // WaveContainer.cs
    constexpr float APPEAR_DURATION = 800.f;
    constexpr float DISAPPEAR_DURATION = 500.f;
    // Per wave: rotation, final top-edge position (osu! px, negative = above the top), right-anchored?
    struct WaveDef { float rotation; float finalY; bool right; };
    constexpr std::array<WaveDef, 4> WAVES {{
        {13.f, -930.f, false},
        {-7.f, -560.f, true},
        {4.f, -390.f, false},
        {-2.f, -220.f, true},
    }};
    // WaveOverlayContainer.HORIZONTAL_PADDING
    constexpr float HORIZONTAL_PADDING = 50.f;
    constexpr float HEADER_HEIGHT = 110.f;
}

bool WaveOverlay::init(float topInset, theme::Scheme scheme, char const* icon,
                       std::string const& title, std::string const& description) {
    if (!CCNode::init()) return false;
    auto win = CCDirector::sharedDirector()->getWinSize();
    m_k = win.height / 768.f;
    m_scheme = scheme;
    m_topInset = topInset;
    m_height = win.height - topInset;
    this->setContentSize({win.width, m_height});

    // Waves: 1.5x the screen in both directions, tinted from the colour scheme.
    std::array<ccColor4B, 4> colours {scheme.light4(), scheme.light3(), scheme.dark4(), scheme.dark3()};
    m_waveClip = CCNode::create();
    this->addChild(m_waveClip, 0);
    for (size_t i = 0; i < WAVES.size(); i++) {
        auto wave = RoundedBox::create({win.width * 1.5f, m_height * 1.5f}, 0, colours[i]);
        wave->setAnchorPoint({WAVES[i].right ? 1.f : 0.f, 1.f});
        wave->setRotation(WAVES[i].rotation);
        wave->setShadow(20 * m_k, {0, 0, 0, 50});
        m_waveClip->addChild(wave, int(i));
        m_waves[i] = wave;
        m_waveFinal[i] = WAVES[i].finalY * m_k;
        m_waveY[i].set(m_height);
    }

    // Content: header + body, rising after the waves.
    m_content = CCNode::create();
    m_content->setContentSize({win.width, m_height});
    this->addChild(m_content, 1);

    float headerH = HEADER_HEIGHT * m_k;
    auto bodyBg = CCLayerColor::create(scheme.background5());
    bodyBg->setContentSize({win.width, m_height - headerH});
    m_content->addChild(bodyBg, 0);

    auto headerBg = CCLayerColor::create(scheme.background4());
    headerBg->setContentSize({win.width, headerH});
    headerBg->setPosition({0, m_height - headerH});
    m_content->addChild(headerBg, 0);

    // A thin accent line under the header.
    auto accent = CCLayerColor::create(scheme.highlight1());
    accent->setContentSize({win.width, 2 * m_k});
    accent->setPosition({0, m_height - headerH});
    m_content->addChild(accent, 1);

    float pad = HORIZONTAL_PADDING * m_k;
    auto iconLabel = makeIcon(icon, 34 * m_k);
    iconLabel->setColor(theme::rgb(scheme.highlight1()));
    iconLabel->setAnchorPoint({0, 0.5f});
    iconLabel->setPosition({pad, m_height - headerH / 2});
    m_content->addChild(iconLabel, 1);

    float textX = pad + iconLabel->getScaledContentSize().width + 16 * m_k;
    auto titleLabel = makeText(title, Weight::Regular, 36 * m_k);
    titleLabel->setAnchorPoint({0, 0});
    titleLabel->setPosition({textX, m_height - headerH / 2 - 2 * m_k});
    m_content->addChild(titleLabel, 1);

    auto descLabel = makeText(description, Weight::Regular, 18 * m_k);
    descLabel->setColor(theme::rgb(scheme.content2()));
    descLabel->setAnchorPoint({0, 1});
    descLabel->setPosition({textX, m_height - headerH / 2 - 4 * m_k});
    m_content->addChild(descLabel, 1);

    m_body = CCNode::create();
    m_body->setContentSize({win.width, m_height - headerH});
    m_content->addChild(m_body, 2);

    m_contentY.set(1.f);
    this->setVisible(false);
    this->scheduleUpdate();
    return true;
}

void WaveOverlay::onEnter() {
    CCNode::onEnter();
    CCDirector::sharedDirector()->getTouchDispatcher()->addTargetedDelegate(this, -140, true);
}

void WaveOverlay::onExit() {
    CCDirector::sharedDirector()->getTouchDispatcher()->removeDelegate(this);
    if (m_open) g_overlayOpen = false;
    CCNode::onExit();
}

void WaveOverlay::open() {
    if (m_open) return;
    m_open = true;
    g_overlayOpen = true;
    this->setVisible(true);
    // WaveContainer.PopIn
    sfx::play(sfx::sound::WAVE_POP_IN);
    for (size_t i = 0; i < m_waves.size(); i++) m_waveY[i].to(m_waveFinal[i], APPEAR_DURATION, Easing::OutSine);
    m_contentY.to(0.f, APPEAR_DURATION, Easing::OutQuint);
    onOpened();
}

void WaveOverlay::close() {
    if (!m_open) return;
    m_open = false;
    g_overlayOpen = false;
    // WaveContainer.PopOut
    sfx::play(sfx::sound::WAVE_POP_OUT);
    for (size_t i = 0; i < m_waves.size(); i++) m_waveY[i].to(m_height, DISAPPEAR_DURATION, Easing::InSine);
    m_contentY.to(1.f, DISAPPEAR_DURATION, Easing::In);
    if (m_hovered) { m_hovered->setHovered(false); m_hovered = nullptr; }
}

bool WaveOverlay::back() {
    if (!m_open) return false;
    close();
    return true;
}

SettingsRow* WaveOverlay::rowAt(CCPoint world) {
    for (auto row : m_interactive) {
        if (!row->isVisible()) continue;
        auto local = row->convertToNodeSpace(world);
        auto size = row->getContentSize();
        if (local.x >= 0 && local.y >= 0 && local.x <= size.width && local.y <= size.height) return row;
    }
    return nullptr;
}

void WaveOverlay::update(float dt) {
    for (auto& t : m_waveY) t.update(dt);
    m_contentY.update(dt);

    for (size_t i = 0; i < m_waves.size(); i++) {
        m_waves[i]->setPosition({WAVES[i].right ? this->getContentSize().width : 0.f, m_height - m_waveY[i].get()});
    }
    m_content->setPositionY(-m_contentY.get() * m_height);

    bool settled = m_contentY.get() >= 0.999f;
    for (auto& t : m_waveY) settled = settled && t.get() >= m_height - 0.5f;
    if (!m_open && settled) this->setVisible(false);
    if (!this->isVisible()) return;

    // Hover once the content has mostly arrived.
    bool interactive = m_open && m_contentY.get() < 0.2f;
    auto hovered = interactive ? rowAt(geode::cocos::getMousePos()) : nullptr;
    if (hovered != m_hovered) {
        if (m_hovered) m_hovered->setHovered(false);
        if (hovered) hovered->setHovered(true);
        m_hovered = hovered;
    }
    onUpdate(dt);
}

bool WaveOverlay::ccTouchBegan(CCTouch* touch, CCEvent*) {
    if (!m_open || !this->isVisible()) return false;
    // The toolbar above stays usable.
    if (touch->getLocation().y > m_height) return false;
    m_pressed = rowAt(touch->getLocation());
    return true; // swallow everything else so the menu underneath doesn't react
}

void WaveOverlay::ccTouchEnded(CCTouch* touch, CCEvent*) {
    if (m_pressed && rowAt(touch->getLocation()) == m_pressed) {
        m_pressed->onClick(m_pressed->convertToNodeSpace(touch->getLocation()));
    }
    m_pressed = nullptr;
}

} // namespace lazer
