#include "WaveOverlay.hpp"

#include "../../audio/Sfx.hpp"
#include "../core/Text.hpp"
#include "../core/Triangles.hpp"
#include "../core/Theme.hpp"

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
                       std::string const& title, std::string const& description, float headerHeight) {
    if (!CCNode::init()) return false;
    auto win = CCDirector::sharedDirector()->getWinSize();
    m_k = unitScale();
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

    float headerH = headerHeight * m_k;
    // Shorter headers get proportionally smaller text.
    float textScale = std::min(1.f, headerHeight / HEADER_HEIGHT * 1.15f);
    auto bodyBg = CCLayerColor::create(scheme.background5());
    bodyBg->setContentSize({win.width, m_height - headerH});
    m_content->addChild(bodyBg, 0);

    auto headerBg = CCLayerColor::create(scheme.background4());
    headerBg->setContentSize({win.width, headerH});
    headerBg->setPosition({0, m_height - headerH});
    m_content->addChild(headerBg, 0);
    // The header's own life: a wash of the scheme's colour to the right and
    // osu!'s triangles drifting up through it (kept inside the band).
    auto wash = CCLayerGradient::create({0, 0, 0, 0}, scheme.get(0.35f, 0.22f), {1, -0.35f});
    wash->setContentSize({win.width, headerH});
    wash->setPosition({0, m_height - headerH});
    m_content->addChild(wash, 0);
    auto triangles = Triangles::create({win.width, headerH}, 90 * m_k, std::max(12, int(win.width / (60 * m_k))));
    triangles->setColor(theme::rgb(scheme.light1()));
    triangles->setAlphaRange(0.03f, 0.10f);
    triangles->setVelocity(0.25f);
    triangles->setClipped(true);
    triangles->setPosition({0, m_height - headerH});
    m_content->addChild(triangles, 1);

    // A thin accent line under the header.
    auto accent = CCLayerColor::create(scheme.highlight1());
    accent->setContentSize({win.width, 2 * m_k});
    accent->setPosition({0, m_height - headerH});
    m_content->addChild(accent, 1);

    float pad = HORIZONTAL_PADDING * m_k;
    auto iconLabel = makeIcon(icon, 34 * m_k * textScale);
    iconLabel->setColor(theme::rgb(scheme.highlight1()));
    iconLabel->setAnchorPoint({0, 0.5f});
    iconLabel->setPosition({pad, m_height - headerH / 2});
    m_content->addChild(iconLabel, 1);

    float textX = pad + iconLabel->getScaledContentSize().width + 16 * m_k;
    auto titleLabel = makeText(title, Weight::Regular, 36 * m_k * textScale);
    titleLabel->setAnchorPoint({0, 0});
    titleLabel->setPosition({textX, m_height - headerH / 2 - 2 * m_k});
    m_content->addChild(titleLabel, 1);

    auto descLabel = makeText(description, Weight::Regular, 18 * m_k * textScale);
    descLabel->setColor(theme::rgb(scheme.content2()));
    descLabel->setAnchorPoint({0, 1});
    descLabel->setPosition({textX, m_height - headerH / 2 - 4 * m_k});
    m_content->addChild(descLabel, 1);

    // Close button: osu! closes overlays with Escape or a click outside, but
    // phones have neither (and not every phone has a back gesture).
    float closeSize = std::min(44.f * m_k, headerH * 0.6f);
    m_closeButton = RoundedBox::create({closeSize, closeSize}, closeSize / 2, scheme.background3());
    m_closeButton->setPosition({win.width - pad * 0.6f - closeSize / 2, m_height - headerH / 2});
    auto closeIcon = makeIcon(icon::XMARK, closeSize * 0.45f);
    closeIcon->setColor(theme::rgb(scheme.content1()));
    anchorOnGlyph(closeIcon);
    closeIcon->setPosition({closeSize / 2, closeSize / 2});
    m_closeButton->addChild(closeIcon, 1);
    m_content->addChild(m_closeButton, 3);

    m_body = CCNode::create();
    m_body->setContentSize({win.width, m_height - headerH});
    m_content->addChild(m_body, 2);

    m_header = CCNode::create();
    m_header->setContentSize({win.width, headerH});
    m_header->setPosition({0, m_height - headerH});
    m_content->addChild(m_header, 2);

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
    // One under this (a profile under its comment history) keeps it set.
    if (auto parent = this->getParent()) {
        for (auto child : CCArrayExt<CCNode*>(parent->getChildren())) {
            if (child == this) continue;
            if (auto other = typeinfo_cast<WaveOverlay*>(child); other && other->isOpen()) g_overlayOpen = true;
        }
    }
    // WaveContainer.PopOut
    sfx::play(sfx::sound::WAVE_POP_OUT);
    for (size_t i = 0; i < m_waves.size(); i++) m_waveY[i].to(m_height, DISAPPEAR_DURATION, Easing::InSine);
    m_contentY.to(1.f, DISAPPEAR_DURATION, Easing::In);
    if (m_hovered) { m_hovered->setHovered(false); m_hovered = nullptr; }
}

float WaveOverlay::headerRight() const {
    return m_closeButton->getPositionX() - m_closeButton->getContentSize().width / 2;
}

bool WaveOverlay::back() {
    if (!m_open) return false;
    close();
    return true;
}

void WaveOverlay::removeInteractive(SettingsRow* row) {
    std::erase(m_interactive, row);
    if (m_hovered == row) m_hovered = nullptr;
    if (m_pressed == row) m_pressed = nullptr;
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
    if (!m_open && settled && this->isVisible()) {
        this->setVisible(false);
        onClosed();
        return;
    }
    if (!this->isVisible()) return;

    // Hover once the content has mostly arrived.
    bool interactive = m_open && m_contentY.get() < 0.2f;
    bool closeHovered = interactive && overClose(geode::cocos::getMousePos());
    if (closeHovered && !m_closeHovered) sfx::hover(sfx::sound::DEFAULT_HOVER);
    m_closeHovered = closeHovered;
    m_closeButton->setFillColor(closeHovered ? m_scheme.highlight1() : m_scheme.background3());
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
    if (touchOnOpenCard(touch->getLocation())) return false;
    // The toolbar above stays usable.
    if (touch->getLocation().y > m_height) return false;
    m_closePressed = overClose(touch->getLocation());
    m_pressed = m_closePressed ? nullptr : rowAt(touch->getLocation());
    return true; // swallow everything else so the menu underneath doesn't react
}

bool WaveOverlay::overClose(CCPoint world) {
    auto local = m_closeButton->convertToNodeSpace(world);
    auto size = m_closeButton->getContentSize();
    // A little larger than drawn, for fingers.
    float slop = 8 * m_k;
    return local.x >= -slop && local.y >= -slop && local.x <= size.width + slop && local.y <= size.height + slop;
}

void WaveOverlay::ccTouchEnded(CCTouch* touch, CCEvent*) {
    if (m_closePressed && overClose(touch->getLocation())) {
        m_closePressed = false;
        sfx::click(sfx::sound::DEFAULT_SELECT);
        close();
        return;
    }
    m_closePressed = false;
    if (m_pressed && rowAt(touch->getLocation()) == m_pressed) {
        m_pressed->onClick(m_pressed->convertToNodeSpace(touch->getLocation()));
    }
    m_pressed = nullptr;
}

} // namespace lazer
