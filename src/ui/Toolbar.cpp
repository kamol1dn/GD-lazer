#include "Toolbar.hpp"

#include "Text.hpp"

#include <Geode/Geode.hpp>
#include <chrono>
#include <ctime>

using namespace geode::prelude;

namespace lazer {

namespace {
    // Toolbar.cs / ToolbarButton.cs, in osu!'s 768px-tall units.
    constexpr float OSU_HEIGHT = 768.f;
    constexpr float TOOLBAR_HEIGHT = 40.f;
    constexpr float TRANSITION = 500.f;
    constexpr ccColor4B BG {26, 26, 26, 255};          // OsuColour.Gray(0.1f)
    constexpr ccColor4B HOVER {80, 80, 80, 180};       // OsuColour.Gray(80).Opacity(180)
    constexpr ccColor4B TOOLTIP_BG {20, 20, 20, 235};

    GLubyte toByte(float a) { return static_cast<GLubyte>(std::clamp(a, 0.f, 1.f) * 255.f); }

    auto const g_startTime = std::chrono::steady_clock::now();

    void fitInto(CCNode* node, float box) {
        auto size = node->getContentSize();
        float largest = std::max(size.width, size.height);
        if (largest > 0) node->setScale(box / largest);
    }

    void setOpacityDeep(CCNode* node, GLubyte o) {
        if (auto rgba = typeinfo_cast<CCRGBAProtocol*>(node)) rgba->setOpacity(o);
        for (auto child : CCArrayExt<CCNode*>(node->getChildren())) setOpacityDeep(child, o);
    }
}

// ---------------------------------------------------------------------------

CCSprite* snapshotNode(CCNode* node) {
    auto size = node->getContentSize();
    if (size.width <= 0 || size.height <= 0) return nullptr;

    auto rt = CCRenderTexture::create(int(std::ceil(size.width)), int(std::ceil(size.height)));
    if (!rt) return nullptr;

    auto pos = node->getPosition();
    float sx = node->getScaleX(), sy = node->getScaleY(), rot = node->getRotation();
    bool visible = node->isVisible();
    node->setPosition(node->isIgnoreAnchorPointForPosition() ? CCPointZero : node->getAnchorPointInPoints());
    node->setScale(1);
    node->setRotation(0);
    node->setVisible(true);

    rt->beginWithClear(0, 0, 0, 0);
    node->visit();
    rt->end();

    node->setPosition(pos);
    node->setScaleX(sx);
    node->setScaleY(sy);
    node->setRotation(rot);
    node->setVisible(visible);

    auto sprite = CCSprite::createWithTexture(rt->getSprite()->getTexture());
    sprite->setFlipY(true);
    return sprite;
}

// ---------------------------------------------------------------------------

ToolbarButton* ToolbarButton::create(CCNode* icon, std::string const& tooltip, float height,
                                     std::function<void()> action) {
    auto ret = new ToolbarButton();
    if (ret->init(icon, tooltip, height, std::move(action))) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

bool ToolbarButton::init(CCNode* icon, std::string const& tooltip, float height,
                         std::function<void()> action) {
    if (!CCNode::init()) return false;
    m_action = std::move(action);
    CCSize size {height * 1.1f, height};
    this->setContentSize(size);
    this->setAnchorPoint({0, 0});

    m_hoverBg = RoundedBox::create(size, 0, HOVER);
    m_hoverBg->setPosition(size / 2);
    m_hoverBg->setOpacity(0);
    this->addChild(m_hoverBg);

    if (icon) {
        fitInto(icon, height * 0.45f);
        icon->setAnchorPoint({0.5f, 0.5f});
        icon->setPosition(size / 2);
        this->addChild(icon, 1);
    }

    // Tooltip hangs below the toolbar.
    if (!tooltip.empty()) {
        auto text = makeText(tooltip, Weight::SemiBold, height * 0.38f);
        auto textSize = text->getScaledContentSize();
        CCSize boxSize {textSize.width + height * 0.5f, height * 0.62f};
        auto box = RoundedBox::create(boxSize, height * 0.12f, TOOLTIP_BG);
        text->setPosition(boxSize / 2);
        box->addChild(text);
        box->setPosition({size.width / 2, -boxSize.height / 2 - height * 0.15f});
        m_tooltip = box;
        setOpacityDeep(m_tooltip, 0);
        this->addChild(m_tooltip, 2);
    }

    this->scheduleUpdate();
    return true;
}

void ToolbarButton::onEnter() {
    CCNode::onEnter();
    CCDirector::sharedDirector()->getTouchDispatcher()->addTargetedDelegate(this, -131, true);
}

void ToolbarButton::onExit() {
    CCDirector::sharedDirector()->getTouchDispatcher()->removeDelegate(this);
    CCNode::onExit();
}

bool ToolbarButton::containsWorldPoint(CCPoint p) {
    auto local = this->convertToNodeSpace(p);
    auto size = this->getContentSize();
    return local.x >= 0 && local.y >= 0 && local.x <= size.width && local.y <= size.height;
}

void ToolbarButton::update(float dt) {
    // Only interactive while actually on screen.
    bool visible = m_enabled;
    for (CCNode* n = this; n && visible; n = n->getParent()) visible = n->isVisible();

    bool hovered = visible && containsWorldPoint(geode::cocos::getMousePos());
    if (hovered != m_hovered) {
        m_hovered = hovered;
        m_hoverAlpha.to(hovered ? 1.f : 0.f, 200, hovered ? Easing::OutQuint : Easing::None);
    }
    m_hoverAlpha.update(dt);
    m_flash.update(dt);

    // Hover background, plus a brief white flash on click.
    float flash = m_flash.get();
    m_hoverBg->setFillColor({
        static_cast<GLubyte>(HOVER.r + (255 - HOVER.r) * flash),
        static_cast<GLubyte>(HOVER.g + (255 - HOVER.g) * flash),
        static_cast<GLubyte>(HOVER.b + (255 - HOVER.b) * flash),
        static_cast<GLubyte>(HOVER.a + (100 - HOVER.a) * flash),
    });
    m_hoverBg->setOpacity(toByte(std::max(m_hoverAlpha.get(), flash)));
    if (m_tooltip) setOpacityDeep(m_tooltip, toByte(m_hoverAlpha.get()));
}

bool ToolbarButton::ccTouchBegan(CCTouch* touch, CCEvent*) {
    if (!m_hovered || !containsWorldPoint(touch->getLocation())) return false;
    m_pressed = true;
    return true;
}

void ToolbarButton::ccTouchEnded(CCTouch* touch, CCEvent*) {
    if (!m_pressed) return;
    m_pressed = false;
    if (!containsWorldPoint(touch->getLocation())) return;
    m_flash.set(1.f);
    m_flash.to(0.f, 500, Easing::OutQuint);
    if (m_action) m_action();
}

// ---------------------------------------------------------------------------

Toolbar* Toolbar::create() {
    auto ret = new Toolbar();
    if (ret->init()) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

bool Toolbar::init() {
    if (!CCNode::init()) return false;
    auto win = CCDirector::sharedDirector()->getWinSize();
    m_height = TOOLBAR_HEIGHT * win.height / OSU_HEIGHT;
    this->setContentSize({win.width, m_height});

    m_content = CCNode::create();
    m_content->setContentSize({win.width, m_height});
    this->addChild(m_content);

    // Soft shadow under the bar (Toolbar.cs gradient).
    auto shadow = CCLayerGradient::create({0, 0, 0, 150}, {0, 0, 0, 0});
    shadow->setContentSize({win.width, m_height * 1.5f});
    shadow->setPosition({0, -m_height * 1.5f});
    m_content->addChild(shadow, -1);

    auto bg = CCLayerColor::create(BG);
    bg->setContentSize({win.width, m_height});
    m_content->addChild(bg, 0);

    m_clock = makeText("00:00:00", Weight::SemiBold, m_height * 0.42f);
    m_clock->setAnchorPoint({1, 0.5f});
    m_content->addChild(m_clock, 1);
    m_running = makeText("running 00:00:00", Weight::Regular, m_height * 0.26f);
    m_running->setAnchorPoint({1, 0.5f});
    m_running->setColor({255, 102, 170});
    m_content->addChild(m_running, 1);

    this->setVisible(false);
    this->scheduleUpdate();
    this->layout();
    return true;
}

void Toolbar::addLeft(Item item) {
    auto b = ToolbarButton::create(item.icon, item.tooltip, m_height, std::move(item.action));
    m_content->addChild(b, 2);
    m_left.push_back(b);
    layout();
}

void Toolbar::addRight(Item item) {
    auto b = ToolbarButton::create(item.icon, item.tooltip, m_height, std::move(item.action));
    m_content->addChild(b, 2);
    m_right.push_back(b);
    layout();
}

void Toolbar::setUser(std::string const& name, std::function<void()> action) {
    if (m_user) m_user->removeFromParent();

    // Avatar circle + name, as one wide toolbar button.
    auto holder = CCNode::create();
    float avatar = m_height * 0.62f;
    auto circle = RoundedBox::create({avatar, avatar}, avatar / 2, {70, 70, 70, 255});
    auto glyph = makeIcon(icon::USER, avatar * 0.5f);
    glyph->setPosition(CCSize(avatar, avatar) / 2);
    circle->addChild(glyph);
    auto text = makeText(name, Weight::SemiBold, m_height * 0.4f);
    text->setAnchorPoint({0, 0.5f});

    float gap = m_height * 0.2f;
    CCSize size {avatar + gap + text->getScaledContentSize().width, avatar};
    holder->setContentSize(size);
    circle->setPosition({avatar / 2, avatar / 2});
    text->setPosition({avatar + gap, avatar / 2});
    holder->addChild(circle);
    holder->addChild(text);

    auto button = ToolbarButton::create(nullptr, "", m_height, std::move(action));
    // Widen the button to fit the user section.
    CCSize bsize {size.width + m_height * 0.6f, m_height};
    button->setContentSize(bsize);
    holder->setAnchorPoint({0.5f, 0.5f});
    holder->setPosition(bsize / 2);
    button->addChild(holder, 1);
    // The hover background was created at the default size; resize it.
    for (auto child : CCArrayExt<CCNode*>(button->getChildren())) {
        if (auto box = typeinfo_cast<RoundedBox*>(child)) {
            box->setContentSize(bsize);
            box->setPosition(bsize / 2);
        }
    }

    m_user = button;
    m_content->addChild(button, 2);
    layout();
}

void Toolbar::layout() {
    auto win = CCDirector::sharedDirector()->getWinSize();
    float pad = m_height * 0.3f;

    float x = 0;
    for (auto b : m_left) {
        b->setPosition({x, 0});
        x += b->getContentSize().width;
    }

    float right = win.width - pad;
    m_clock->setPosition({right, m_height * 0.6f});
    m_running->setPosition({right, m_height * 0.24f});
    right -= std::max(m_clock->getScaledContentSize().width, m_running->getScaledContentSize().width) + pad;

    if (m_user) {
        right -= m_user->getContentSize().width;
        m_user->setPosition({right, 0});
    }
    for (auto it = m_right.rbegin(); it != m_right.rend(); ++it) {
        right -= (*it)->getContentSize().width;
        (*it)->setPosition({right, 0});
    }
    m_rightEdge = right;
}

void Toolbar::show() {
    this->setVisible(true);
    m_offset.to(0, TRANSITION, Easing::OutQuint);
    m_alpha.to(1, TRANSITION / 2, Easing::OutQuint);
}

void Toolbar::hide() {
    m_offset.to(1, TRANSITION, Easing::OutQuint);
    m_alpha.to(0, TRANSITION, Easing::OutQuint);
}

void Toolbar::update(float dt) {
    m_offset.update(dt);
    m_alpha.update(dt);

    auto win = CCDirector::sharedDirector()->getWinSize();
    m_content->setPosition({0, win.height - m_height + m_offset.get() * m_height * 2.5f});
    if (m_alpha.get() <= 0.001f && m_alpha.target() <= 0) this->setVisible(false);

    // Clock: wall time plus how long the game has been running.
    auto now = std::time(nullptr);
    std::tm local {};
#ifdef _WIN32
    localtime_s(&local, &now);
#else
    localtime_r(&now, &local);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%H:%M:%S", &local);
    m_clock->setString(buf);

    auto running = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now() - g_startTime).count();
    std::snprintf(buf, sizeof(buf), "running %02lld:%02lld:%02lld",
                  (long long)running / 3600, (long long)running / 60 % 60, (long long)running % 60);
    m_running->setString(buf);
}

} // namespace lazer
