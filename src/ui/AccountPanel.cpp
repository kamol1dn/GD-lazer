#include "AccountPanel.hpp"

#include "../audio/Sfx.hpp"
#include "../settings/Account.hpp"
#include "ModIntegrations.hpp"
#include "Text.hpp"
#include "Theme.hpp"

#include <Geode/Geode.hpp>

using namespace geode::prelude;

namespace lazer {

namespace {
    constexpr float WIDTH = 360;
    constexpr float MARGIN = 10;
    constexpr float CORNER = 10;
    constexpr float TRANSITION = 500;
    constexpr float ITEM_HEIGHT = 38;
    constexpr ccColor3B DANGER {255, 110, 110};

    GLubyte toByte(float a) { return static_cast<GLubyte>(std::clamp(a, 0.f, 1.f) * 255.f); }

    bool nodeContains(CCNode* node, CCPoint world) {
        auto local = node->convertToNodeSpace(world);
        auto size = node->getContentSize();
        return local.x >= 0 && local.y >= 0 && local.x <= size.width && local.y <= size.height;
    }

    int stat(StatKey key) {
        return GameStatsManager::sharedState()->getStat(fmt::format("{}", static_cast<int>(key)).c_str());
    }

    std::string withCommas(long long v) {
        auto s = fmt::format("{}", v);
        for (int i = int(s.size()) - 3; i > 0; i -= 3) s.insert(size_t(i), ",");
        return s;
    }
}

AccountPanel* AccountPanel::create(float toolbarHeight, Actions actions) {
    auto ret = new AccountPanel();
    if (ret->init(toolbarHeight, std::move(actions))) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

bool AccountPanel::init(float toolbarHeight, Actions actions) {
    if (!CCNode::init()) return false;
    auto win = CCDirector::sharedDirector()->getWinSize();
    this->setContentSize(win);
    m_k = win.height / 768.f;
    m_toolbarHeight = toolbarHeight;
    m_actions = std::move(actions);

    // Drops from the top-right corner, under the user button.
    m_panel = CCNodeRGBA::create();
    m_panel->setCascadeOpacityEnabled(true);
    m_panel->setAnchorPoint({1, 1});
    m_panel->setPosition({win.width - MARGIN * m_k, win.height - toolbarHeight - MARGIN * m_k});
    this->addChild(m_panel);

    this->setVisible(false);
    this->scheduleUpdate();
    return true;
}

// --- building ---

void AccountPanel::rebuild() {
    m_panel->removeAllChildren();
    m_items.clear();
    m_pressed = nullptr;
    m_status = nullptr;
    m_builtLoggedIn = account::loggedIn();

    float w = WIDTH * m_k;
    // Children are placed with y measured down from the top (negative); the
    // height is only known at the end, when they're shifted into place.
    float y = buildHeader(w, 0);
    y = buildStats(w, y);
    m_itemsTop = y;
    y = buildItems(w, y);
    float h = y + 8 * m_k;

    m_panel->setContentSize({w, h});
    auto base = RoundedBox::create({w, h}, CORNER * m_k, theme::BACKGROUND4);
    base->setShadow(CORNER * m_k * 1.5f, {0, 0, 0, 110});
    base->setPosition({w / 2, h / 2});
    m_panel->addChild(base, -1);

    for (auto child : CCArrayExt<CCNode*>(m_panel->getChildren())) {
        if (child == base) continue;
        child->setPositionY(h + child->getPositionY());
    }
}

float AccountPanel::buildHeader(float w, float top) {
    float k = m_k;
    float h = 128 * k;
    auto gm = GameManager::get();

    // Tinted with the player's main colour, like an osu! profile cover.
    auto tint = gm->colorForIdx(gm->getPlayerColor());
    auto bg = RoundedBox::create({w, h}, CORNER * k, theme::lerp(theme::BACKGROUND5, {tint.r, tint.g, tint.b, 255}, 0.28f));
    bg->setCornerRadii(CORNER * k, CORNER * k, 0, 0);
    bg->setPosition({w / 2, -(top + h / 2)});
    m_panel->addChild(bg);

    // Player 1's icon in a tile, player 2's (2P Skins) tucked into its corner.
    float tile = 80 * k;
    float tileX = 16 * k + tile / 2, tileY = -(top + 16 * k + tile / 2);
    auto p1Tile = RoundedBox::create({tile, tile}, 14 * k, {0, 0, 0, 90});
    p1Tile->setPosition({tileX, tileY});
    m_panel->addChild(p1Tile, 1);
    if (auto p1 = integrations::playerIcon(false, 50 * k)) {
        p1->setPosition({tileX, tileY});
        m_panel->addChild(p1, 2);
    }
    if (auto p2 = integrations::playerIcon(true, 26 * k)) {
        float mini = 40 * k;
        CCPoint at {tileX + tile / 2 - mini / 4, tileY - tile / 2 + mini / 4};
        auto p2Tile = RoundedBox::create({mini, mini}, 10 * k, theme::BACKGROUND6);
        p2Tile->setBorder(2 * k, theme::BACKGROUND4);
        p2Tile->setPosition(at);
        m_panel->addChild(p2Tile, 3);
        p2->setPosition(at);
        m_panel->addChild(p2, 4);
    }

    float textX = 16 * k + tile + 16 * k;
    float textW = w - textX - 16 * k;
    auto name = makeText(account::username(), Weight::Bold, 24 * k);
    name->setAnchorPoint({0, 1});
    name->setPosition({textX, -(top + 18 * k)});
    float nameW = name->getScaledContentSize().width;
    if (nameW > textW) name->setScale(name->getScale() * textW / nameW);
    m_panel->addChild(name, 1);

    m_status = makeText("", Weight::Regular, 14 * k);
    m_status->setColor(theme::CONTENT2);
    m_status->setAnchorPoint({0, 1});
    m_status->setPosition({textX, -(top + 48 * k)});
    m_panel->addChild(m_status, 1);

    // Better Progression: badge, level, and EXP towards the next one.
    if (auto progress = integrations::betterProgression()) {
        float rowY = -(top + 86 * k);
        float x = textX;
        if (auto badge = integrations::progressionBadge(progress->level, 26 * k)) {
            badge->setPosition({x + 13 * k, rowY});
            m_panel->addChild(badge, 1);
            x += 32 * k;
        }
        auto level = makeText(fmt::format("level {}", progress->level), Weight::SemiBold, 15 * k);
        level->setAnchorPoint({0, 0.5f});
        level->setPosition({x, rowY});
        m_panel->addChild(level, 1);

        long long into = progress->exp - progress->levelStart;
        long long span = std::max(1LL, progress->levelEnd - progress->levelStart);
        auto exp = makeText(fmt::format("{} / {} exp", withCommas(into), withCommas(span)), Weight::Regular, 12 * k);
        exp->setColor(theme::CONTENT2);
        exp->setAnchorPoint({1, 0.5f});
        exp->setPosition({w - 16 * k, rowY});
        m_panel->addChild(exp, 1);

        float barW = w - textX - 16 * k, barY = -(top + 108 * k);
        auto track = RoundedBox::create({barW, 6 * k}, 3 * k, {0, 0, 0, 110});
        track->setAnchorPoint({0, 0.5f});
        track->setPosition({textX, barY});
        m_panel->addChild(track, 1);
        float fraction = std::clamp(float(into) / float(span), 0.f, 1.f);
        auto fill = RoundedBox::create({std::max(6 * k, barW * fraction), 6 * k}, 3 * k, theme::HIGHLIGHT1);
        fill->setAnchorPoint({0, 0.5f});
        fill->setPosition({textX, barY});
        m_panel->addChild(fill, 2);
    }
    return top + h;
}

float AccountPanel::buildStats(float w, float top) {
    float k = m_k;
    float h = 48 * k;
    auto strip = CCLayerColor::create(theme::BACKGROUND5, w, h);
    strip->setPosition({0, -(top + h)});
    m_panel->addChild(strip);

    struct Stat { char const* sprite; int value; };
    Stat stats[] {
        {"GJ_starsIcon_001.png", stat(StatKey::Stars)},
        {"GJ_moonsIcon_001.png", stat(StatKey::Moons)},
        {"GJ_diamondsIcon_001.png", stat(StatKey::Diamonds)},
        {"GJ_demonIcon_001.png", stat(StatKey::Demons)},
    };
    float cell = w / 4;
    for (int i = 0; i < 4; i++) {
        float cx = cell * i + cell / 2, cy = -(top + h / 2);
        auto number = makeText(withCommas(stats[i].value), Weight::SemiBold, 16 * k);
        auto icon = CCSprite::createWithSpriteFrameName(stats[i].sprite);
        float iconSize = 16 * k;
        float totalW = iconSize + 6 * k + number->getScaledContentSize().width;
        if (icon) {
            auto s = icon->getContentSize();
            icon->setScale(iconSize / std::max(s.width, s.height));
            icon->setPosition({cx - totalW / 2 + iconSize / 2, cy});
            m_panel->addChild(icon, 1);
        }
        number->setAnchorPoint({0, 0.5f});
        number->setPosition({cx - totalW / 2 + iconSize + 6 * k, cy});
        m_panel->addChild(number, 1);
    }
    return top + h + 8 * k;
}

void AccountPanel::addItem(char const* glyph, std::string const& label, std::function<void()> action, bool danger) {
    float k = m_k;
    float w = WIDTH * k - 16 * k, h = ITEM_HEIGHT * k;
    float top = m_itemsTop;

    auto node = CCNodeRGBA::create();
    node->setCascadeOpacityEnabled(true);
    node->setContentSize({w, h});
    node->setAnchorPoint({0, 1});
    node->setPosition({8 * k, -top});
    m_panel->addChild(node, 1);

    auto hover = RoundedBox::create({w, h}, 6 * k, theme::DARK3);
    hover->setPosition({w / 2, h / 2});
    hover->setOpacity(0);
    node->addChild(hover);

    auto icon = makeIcon(glyph, 15 * k);
    icon->setColor(danger ? DANGER : theme::LIGHT1);
    icon->setPosition({20 * k, h / 2});
    node->addChild(icon, 1);

    auto text = makeText(label, Weight::Regular, 16 * k);
    text->setColor(danger ? DANGER : theme::CONTENT1);
    text->setAnchorPoint({0, 0.5f});
    text->setPosition({42 * k, h / 2});
    node->addChild(text, 1);

    m_items.push_back({node, hover, std::move(action), danger});
    m_itemsTop += h;
}

float AccountPanel::buildItems(float w, float top) {
    m_itemsTop = top;
    auto run = [this](std::function<void()> fn) {
        return [this, fn] {
            close();
            if (fn) fn();
        };
    };
    addItem(icon::ID_CARD, "view profile", run(m_actions.viewProfile));
    addItem(icon::SHIRT, "icon kit", run(m_actions.iconKit));

    // Account actions (GD's own, with its confirmations).
    m_itemsTop += 6 * m_k;
    auto divider = CCLayerColor::create(theme::BACKGROUND6, w - 32 * m_k, 1 * m_k);
    divider->setPosition({16 * m_k, -m_itemsTop});
    m_panel->addChild(divider, 1);
    m_itemsTop += 7 * m_k;

    if (account::loggedIn()) {
        addItem(icon::CLOUD_UP, "save progress", run(account::save));
        addItem(icon::CLOUD_DOWN, "load progress", run(account::load));
        addItem(icon::ROTATE, "refresh login", run(account::refreshLogin));
        addItem(icon::USER_GEAR, "manage account", run(account::manage));
        addItem(icon::SIGN_OUT, "sign out", run(account::unlink), true);
    } else {
        addItem(icon::SIGN_IN, "log in", run(account::logIn));
        addItem(icon::USER_PLUS, "register", run(account::registerAccount));
    }
    return m_itemsTop;
}

// --- open / close ---

void AccountPanel::onEnter() {
    CCNode::onEnter();
    // Above the menu, below the toolbar's own buttons (-131).
    CCDirector::sharedDirector()->getTouchDispatcher()->addTargetedDelegate(this, -130, true);
}

void AccountPanel::onExit() {
    CCDirector::sharedDirector()->getTouchDispatcher()->removeDelegate(this);
    CCNode::onExit();
}

void AccountPanel::open() {
    if (m_open) return;
    m_open = true;
    rebuild(); // icons, level and login state may have changed
    this->setVisible(true);
    m_alpha.to(1.f, TRANSITION, Easing::OutQuint);
    m_scale.to(1.f, TRANSITION, Easing::OutElasticHalf);
    sfx::play(sfx::sound::OVERLAY_POP_IN);
}

void AccountPanel::close() {
    if (!m_open) return;
    m_open = false;
    m_alpha.to(0.f, TRANSITION, Easing::OutQuint);
    m_scale.to(0.9f, TRANSITION, Easing::OutQuint);
    sfx::play(sfx::sound::OVERLAY_POP_OUT);
    m_pressed = nullptr;
}

bool AccountPanel::back() {
    if (!m_open) return false;
    close();
    return true;
}

AccountPanel::Item* AccountPanel::itemAt(CCPoint world) {
    for (auto& item : m_items) {
        if (nodeContains(item.node, world)) return &item;
    }
    return nullptr;
}

void AccountPanel::update(float dt) {
    m_alpha.update(dt);
    m_scale.update(dt);
    if (!m_open && m_alpha.get() <= 0.001f) {
        this->setVisible(false);
        return;
    }
    m_panel->setOpacity(toByte(m_alpha.get()));
    m_panel->setScale(m_scale.get());

    // Logging in / out from here swaps the menu.
    if (m_open && account::loggedIn() != m_builtLoggedIn) rebuild();

    if (m_status) {
        auto status = account::busy() ? "working..." : account::loggedIn() ? "logged in" : "playing as guest";
        if (std::string_view(m_status->getString()) != status) m_status->setString(status);
    }

    auto mouse = geode::cocos::getMousePos();
    auto hovered = m_open && m_alpha.get() > 0.5f ? itemAt(mouse) : nullptr;
    for (auto& item : m_items) {
        bool isHovered = &item == hovered;
        if (isHovered != item.hovered) {
            item.hovered = isHovered;
            if (isHovered) sfx::hover(sfx::sound::DEFAULT_HOVER);
            item.highlight.to(isHovered ? 1.f : 0.f, isHovered ? 100.f : 300.f, Easing::OutQuint);
        }
        item.highlight.update(dt);
        item.hover->setOpacity(toByte(item.highlight.get()));
    }
}

bool AccountPanel::ccTouchBegan(CCTouch* touch, CCEvent*) {
    if (!m_open || !this->isVisible()) return false;
    auto loc = touch->getLocation();
    auto win = CCDirector::sharedDirector()->getWinSize();
    if (!nodeContains(m_panel, loc)) {
        // Clicking elsewhere closes it (the toolbar handles its own button).
        if (loc.y < win.height - m_toolbarHeight) close();
        return false;
    }
    m_pressed = itemAt(loc);
    return true; // the card swallows everything that lands on it
}

void AccountPanel::ccTouchEnded(CCTouch* touch, CCEvent*) {
    auto pressed = m_pressed;
    m_pressed = nullptr;
    if (!pressed || itemAt(touch->getLocation()) != pressed) return;
    sfx::click(sfx::sound::DEFAULT_SELECT);
    auto action = pressed->action; // may rebuild the list
    if (action) action();
}

} // namespace lazer
