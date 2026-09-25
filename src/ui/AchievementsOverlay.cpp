#include "AchievementsOverlay.hpp"

#include "../audio/Sfx.hpp"
#include "Text.hpp"

#include <algorithm>
#include <cctype>

using namespace geode::prelude;

namespace lazer {

namespace {
    // osu!'s Pink overlay scheme.
    constexpr theme::Scheme SCHEME {333};

    constexpr float HORIZONTAL_PADDING = 50.f; // WaveOverlayContainer
    constexpr float CARD_MIN_WIDTH = 330.f;
    constexpr float CARD_HEIGHT = 84.f;
    constexpr float CARD_GAP = 10.f;
    constexpr float ICON_BOX = 64.f;

    // Index 0 is "all"; entries are assigned 1.. by categoryFor().
    constexpr std::array<char const*, 10> CATEGORIES {
        "all", "levels", "coins", "currency", "demons", "online", "paths & shards", "play", "secrets", "other",
    };

    std::string lower(std::string s) {
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
        return s;
    }

    // Groups GD's achievement identifiers ("geometry.ach.<group><n>").
    int categoryFor(std::string id) {
        constexpr std::string_view PREFIX = "geometry.ach.";
        if (id.starts_with(PREFIX)) id = id.substr(PREFIX.size());
        id = lower(id);
        auto starts = [&](std::initializer_list<char const*> list) {
            for (auto p : list) if (id.starts_with(p)) return true;
            return false;
        };
        if (starts({"path", "shard"})) return 6;
        if (id.find("coin") != std::string::npos) return 2;
        if (starts({"level", "world.", "subzero.", "mdlevel", "tower"})) return 1;
        if (starts({"stars", "moons", "diamonds"})) return 3;
        if (starts({"demon", "insane"})) return 4;
        if (starts({"custom", "ratediff", "rate", "mdrate", "like", "lists", "mappacks", "gauntlets", "daily",
                    "creator", "submit", "friends", "followcreator"})) return 5;
        if (starts({"jump", "attempt"})) return 7;
        if (id.find("secret") != std::string::npos || starts({"special"})) return 8;
        return 9;
    }

    // "ship_12" -> GD unlock type + id, as used by GJItemIcon.
    std::optional<std::pair<UnlockType, int>> parseUnlock(std::string const& icon) {
        auto sep = icon.rfind('_');
        if (sep == std::string::npos) return std::nullopt;
        auto kind = icon.substr(0, sep);
        auto id = utils::numFromString<int>(icon.substr(sep + 1));
        if (!id || *id <= 0) return std::nullopt;
        static std::unordered_map<std::string, int> const TYPES {
            {"icon", 1}, {"color", 2}, {"color2", 3}, {"ship", 4}, {"ball", 5}, {"bird", 6}, {"dart", 7},
            {"robot", 8}, {"spider", 9}, {"special", 10}, {"death", 11}, {"swing", 13}, {"jetpack", 14},
        };
        auto it = TYPES.find(kind);
        if (it == TYPES.end()) return std::nullopt;
        return std::make_pair(static_cast<UnlockType>(it->second), *id);
    }

    bool nodeContains(CCNode* node, CCPoint world) {
        auto local = node->convertToNodeSpace(world);
        auto size = node->getContentSize();
        return local.x >= 0 && local.y >= 0 && local.x <= size.width && local.y <= size.height;
    }

    // Shrinks a label to fit `maxWidth` (keeping its size otherwise).
    void fitWidth(CCLabelBMFont* label, float maxWidth) {
        float w = label->getScaledContentSize().width;
        if (w > maxWidth && w > 0) label->setScale(label->getScale() * maxWidth / w);
    }
}

AchievementsOverlay* AchievementsOverlay::create(float topInset) {
    auto ret = new AchievementsOverlay();
    if (ret->init(topInset)) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

bool AchievementsOverlay::init(float topInset) {
    if (!WaveOverlay::init(topInset, SCHEME, icon::TROPHY, "achievements", "every medal you've earned, and the ones still waiting")) {
        return false;
    }
    m_pad = HORIZONTAL_PADDING * m_k;
    m_gap = CARD_GAP * m_k;
    auto size = bodySize();

    buildSummary();
    buildFilters();
    buildTabs();

    // Card grid.
    float gridWidth = size.width - m_pad * 2;
    m_columns = std::max(1, int((gridWidth + m_gap) / (CARD_MIN_WIDTH * m_k + m_gap)));
    m_cardSize = CCSize((gridWidth - m_gap * (m_columns - 1)) / m_columns, CARD_HEIGHT * m_k);

    m_scroll = ScrollArea::create({size.width, size.height - m_topHeight});
    body()->addChild(m_scroll, 1);

    m_emptyLabel = makeText("nothing here", Weight::Regular, 20 * m_k);
    m_emptyLabel->setColor(theme::rgb(m_scheme.content2()));
    m_emptyLabel->setPosition({size.width / 2, (size.height - m_topHeight) / 2});
    body()->addChild(m_emptyLabel, 2);

    loadEntries();
    applyFilter();
    return true;
}

// --- building ---

void AchievementsOverlay::buildSummary() {
    auto size = bodySize();
    float top = size.height - 22 * m_k;

    auto caption = makeText("unlocked", Weight::Regular, 15 * m_k);
    caption->setColor(theme::rgb(m_scheme.content2()));
    caption->setAnchorPoint({0, 1});
    caption->setPosition({m_pad, top});
    body()->addChild(caption, 1);

    m_countLabel = makeText("0", Weight::Bold, 34 * m_k);
    m_countLabel->setAnchorPoint({0, 1});
    m_countLabel->setPosition({m_pad, top - 16 * m_k});
    body()->addChild(m_countLabel, 1);

    m_totalLabel = makeText("/ 0", Weight::Regular, 20 * m_k);
    m_totalLabel->setColor(theme::rgb(m_scheme.content2()));
    m_totalLabel->setAnchorPoint({0, 0});
    body()->addChild(m_totalLabel, 1);

    m_progressWidth = 300 * m_k;
    float barY = top - 62 * m_k;
    auto track = RoundedBox::create({m_progressWidth, 6 * m_k}, 3 * m_k, m_scheme.background6());
    track->setAnchorPoint({0, 0.5f});
    track->setPosition({m_pad, barY});
    body()->addChild(track, 1);
    m_progressFill = RoundedBox::create({0, 6 * m_k}, 3 * m_k, m_scheme.highlight1());
    m_progressFill->setAnchorPoint({0, 0.5f});
    m_progressFill->setPosition({m_pad, barY});
    body()->addChild(m_progressFill, 2);

    m_percentLabel = makeText("0%", Weight::SemiBold, 15 * m_k);
    m_percentLabel->setAnchorPoint({0, 0.5f});
    m_percentLabel->setPosition({m_pad + m_progressWidth + 10 * m_k, barY});
    body()->addChild(m_percentLabel, 1);
}

void AchievementsOverlay::buildFilters() {
    auto size = bodySize();
    float top = size.height - 22 * m_k;
    float h = 34 * m_k;
    float cy = top - 16 * m_k - h / 2;

    // Search box at the far right...
    float searchW = 260 * m_k;
    float right = size.width - m_pad;
    auto searchBg = RoundedBox::create({searchW, h}, 6 * m_k, m_scheme.background6());
    searchBg->setAnchorPoint({1, 0.5f});
    searchBg->setPosition({right, cy});
    body()->addChild(searchBg, 1);

    auto searchIcon = makeIcon(icon::SEARCH, 14 * m_k);
    searchIcon->setColor(theme::FOREGROUND1);
    searchIcon->setPosition({right - 16 * m_k, cy});
    body()->addChild(searchIcon, 2);

    constexpr float inputScale = 0.42f;
    m_search = TextInput::create((searchW - 40 * m_k) / inputScale, "search", "outfit-regular.fnt"_spr);
    m_search->hideBG();
    m_search->setTextAlign(TextInputAlign::Left);
    m_search->setScale(inputScale);
    m_search->setAnchorPoint({0, 0.5f});
    m_search->setPosition({right - searchW + 10 * m_k, cy});
    m_search->setCallback([this](std::string const& text) {
        m_query = lower(text);
        applyFilter();
    });
    body()->addChild(m_search, 2);

    // ...and the filter pills left of it.
    constexpr std::array<std::pair<char const*, Filter>, 4> FILTERS {{
        {"all", Filter::All}, {"unlocked", Filter::Unlocked}, {"in progress", Filter::InProgress}, {"locked", Filter::Locked},
    }};
    float x = right - searchW - 16 * m_k;
    for (auto it = FILTERS.rbegin(); it != FILTERS.rend(); ++it) {
        auto label = makeText(it->first, Weight::SemiBold, 15 * m_k);
        float w = label->getScaledContentSize().width + 28 * m_k;
        auto bg = RoundedBox::create({w, h}, h / 2, m_scheme.background6());
        bg->setAnchorPoint({1, 0.5f});
        bg->setPosition({x, cy});
        body()->addChild(bg, 1);
        label->setPosition({w / 2, h / 2});
        bg->addChild(label, 1);
        m_filters.push_back({bg, label, bg, static_cast<int>(it->second)});
        x -= w + 6 * m_k;
    }
}

void AchievementsOverlay::buildTabs() {
    auto size = bodySize();
    float y = size.height - 118 * m_k; // baseline row of the tabs
    float x = m_pad;
    for (size_t i = 0; i < CATEGORIES.size(); i++) {
        auto holder = CCNode::create();
        auto label = makeText(CATEGORIES[i], Weight::SemiBold, 17 * m_k);
        label->setAnchorPoint({0, 0});
        holder->addChild(label);
        auto labelSize = label->getScaledContentSize();
        holder->setContentSize({labelSize.width, labelSize.height + 10 * m_k});
        label->setPosition({0, 10 * m_k});
        holder->setPosition({x, y - 10 * m_k});
        body()->addChild(holder, 1);
        m_tabs.push_back({holder, label, nullptr, int(i)});
        x += labelSize.width + 26 * m_k;
    }

    // Line under the whole row, and the accent bar under the selected tab (OverlayTabControl).
    auto line = CCLayerColor::create(m_scheme.background6());
    line->setContentSize({size.width - m_pad * 2, 1 * m_k});
    line->setPosition({m_pad, y - 10 * m_k});
    body()->addChild(line, 1);

    m_tabUnderline = RoundedBox::create({10, 3 * m_k}, 1.5f * m_k, m_scheme.highlight1());
    m_tabUnderline->setAnchorPoint({0, 0.5f});
    m_tabUnderline->setPositionY(y - 10 * m_k);
    body()->addChild(m_tabUnderline, 2);

    m_topHeight = size.height - (y - 10 * m_k) + 14 * m_k;
    auto first = m_tabs.front().node;
    m_underlineX.set(first->getPositionX());
    m_underlineW.set(first->getContentSize().width);
}

void AchievementsOverlay::loadEntries() {
    for (auto& e : m_entries) {
        if (e.card) e.card->removeFromParent();
    }
    m_entries.clear();
    m_hoveredCard = nullptr;
    m_earned = 0;

    auto am = AchievementManager::sharedState();
    auto all = am->getAllAchievementsSorted(true);
    if (!all) return;
    for (auto obj : CCArrayExt<CCObject*>(all)) {
        auto dict = typeinfo_cast<CCDictionary*>(obj);
        if (!dict) continue;
        Entry e;
        e.id = dict->valueForKey("identifier")->getCString();
        e.title = dict->valueForKey("title")->getCString();
        e.earned = am->isAchievementEarned(e.id.c_str());
        e.percent = e.earned ? 100 : std::clamp(am->percentForAchievement(e.id.c_str()), 0, 99);
        e.description = dict->valueForKey(e.earned ? "achievedDescription" : "unachievedDescription")->getCString();
        e.icon = dict->valueForKey("icon")->getCString();
        e.search = lower(e.title + " " + e.description);
        e.category = categoryFor(e.id);
        if (e.earned) m_earned++;
        m_entries.push_back(std::move(e));
    }

    size_t total = m_entries.size();
    m_countLabel->setString(fmt::format("{}", m_earned).c_str());
    m_totalLabel->setString(fmt::format("/ {}", total).c_str());
    m_totalLabel->setPosition({
        m_countLabel->getPositionX() + m_countLabel->getScaledContentSize().width + 8 * m_k,
        m_countLabel->getPositionY() - m_countLabel->getScaledContentSize().height + 6 * m_k,
    });
    float fraction = total ? float(m_earned) / total : 0.f;
    m_progressFill->setContentSize({std::max(6 * m_k, m_progressWidth * fraction), 6 * m_k});
    m_percentLabel->setString(fmt::format("{}%", int(fraction * 100 + 0.5f)).c_str());
}

void AchievementsOverlay::applyFilter() {
    m_shown.clear();
    for (size_t i = 0; i < m_entries.size(); i++) {
        auto const& e = m_entries[i];
        if (m_category != 0 && e.category != m_category) continue;
        switch (m_filter) {
            case Filter::All: break;
            case Filter::Unlocked: if (!e.earned) continue; break;
            case Filter::InProgress: if (e.earned || e.percent <= 0) continue; break;
            case Filter::Locked: if (e.earned) continue; break;
        }
        if (!m_query.empty() && e.search.find(m_query) == std::string::npos) continue;
        m_shown.push_back(i);
    }
    // "In progress" reads best closest-to-done first.
    if (m_filter == Filter::InProgress) {
        std::stable_sort(m_shown.begin(), m_shown.end(), [this](size_t a, size_t b) {
            return m_entries[a].percent > m_entries[b].percent;
        });
    }

    for (auto& e : m_entries) {
        if (e.card) e.card->setVisible(false);
    }
    int rows = (int(m_shown.size()) + m_columns - 1) / m_columns;
    m_scroll->setContentHeight(rows * (m_cardSize.height + m_gap) + m_gap * 2);
    m_scroll->scrollTo(0, false);
    m_emptyLabel->setVisible(m_shown.empty());
    layoutVisibleCards();
}

CCNode* AchievementsOverlay::buildCard(Entry& e) {
    float k = m_k;
    auto w = m_cardSize.width, h = m_cardSize.height;
    auto card = CCNode::create();
    card->setContentSize(m_cardSize);
    card->setAnchorPoint({0, 1});

    auto bg = RoundedBox::create(m_cardSize, 8 * k, e.earned ? m_scheme.background4() : m_scheme.background5());
    bg->setPosition({w / 2, h / 2});
    card->addChild(bg);
    e.cardBg = bg;

    // Reward icon, in a small tile.
    float box = ICON_BOX * k;
    float boxX = 10 * k + box / 2;
    auto tile = RoundedBox::create({box, box}, 6 * k, m_scheme.background6());
    tile->setPosition({boxX, h / 2});
    card->addChild(tile, 1);
    CCNode* iconNode = nullptr;
    if (auto unlock = parseUnlock(e.icon)) {
        auto item = e.earned
            ? GJItemIcon::createBrowserItem(unlock->first, unlock->second)
            : GJItemIcon::create(unlock->first, unlock->second, {175, 175, 175}, {255, 255, 255}, true, true, true, {255, 255, 255});
        iconNode = item;
    }
    if (!iconNode) {
        auto trophy = makeIcon(icon::TROPHY, 26 * k);
        trophy->setColor(e.earned ? theme::rgb(m_scheme.highlight1()) : theme::FOREGROUND1);
        iconNode = trophy;
    }
    auto iconSize = iconNode->getContentSize() * iconNode->getScale();
    float maxSide = std::max(iconSize.width, iconSize.height);
    if (maxSide > 0) iconNode->setScale(iconNode->getScale() * (box * 0.72f) / maxSide);
    iconNode->setPosition({boxX, h / 2});
    card->addChild(iconNode, 2);

    // Title and description.
    float textX = 10 * k + box + 12 * k;
    float textW = w - textX - 44 * k;
    auto title = makeText(e.title, Weight::SemiBold, 17 * k);
    title->setAnchorPoint({0, 1});
    title->setPosition({textX, h - 12 * k});
    title->setColor(e.earned ? theme::CONTENT1 : theme::rgb(m_scheme.content2()));
    fitWidth(title, textW);
    card->addChild(title, 1);

    auto desc = makeWrappedText(e.description, 13 * k, textW, theme::rgb(m_scheme.content2()));
    desc->setPosition({textX, h - 34 * k});
    card->addChild(desc, 1);

    // Right edge: a tick when earned, otherwise progress (or a lock).
    float rightX = w - 22 * k;
    if (e.earned) {
        auto check = makeIcon(icon::CHECK, 16 * k);
        check->setColor(theme::rgb(m_scheme.highlight1()));
        check->setPosition({rightX, h - 22 * k});
        card->addChild(check, 1);
    } else if (e.percent > 0) {
        auto pct = makeText(fmt::format("{}%", e.percent), Weight::SemiBold, 14 * k);
        pct->setColor(theme::rgb(m_scheme.highlight1()));
        pct->setAnchorPoint({1, 0.5f});
        pct->setPosition({w - 12 * k, h - 22 * k});
        card->addChild(pct, 1);

        float barW = w - textX - 12 * k;
        auto track = RoundedBox::create({barW, 4 * k}, 2 * k, m_scheme.background6());
        track->setAnchorPoint({0, 0.5f});
        track->setPosition({textX, 10 * k});
        card->addChild(track, 1);
        auto fill = RoundedBox::create({std::max(4 * k, barW * e.percent / 100.f), 4 * k}, 2 * k, m_scheme.highlight1());
        fill->setAnchorPoint({0, 0.5f});
        fill->setPosition({textX, 10 * k});
        card->addChild(fill, 2);
    } else {
        auto lock = makeIcon(icon::LOCK, 14 * k);
        lock->setColor(theme::FOREGROUND1);
        lock->setPosition({rightX, h - 22 * k});
        card->addChild(lock, 1);
    }

    m_scroll->content()->addChild(card);
    return card;
}

void AchievementsOverlay::layoutVisibleCards() {
    // Only cards within (or near) the visible window exist and draw.
    float viewTop = m_scroll->scroll();
    float viewBottom = viewTop + m_scroll->getContentSize().height;
    float margin = m_cardSize.height * 2;
    float rowH = m_cardSize.height + m_gap;

    for (size_t i = 0; i < m_shown.size(); i++) {
        auto& e = m_entries[m_shown[i]];
        int row = int(i) / m_columns, col = int(i) % m_columns;
        float top = m_gap + row * rowH;
        bool inView = top + m_cardSize.height >= viewTop - margin && top <= viewBottom + margin;
        if (!inView) {
            if (e.card) e.card->setVisible(false);
            continue;
        }
        if (!e.card) e.card = buildCard(e);
        e.card->setVisible(true);
        e.card->setPosition({m_pad + col * (m_cardSize.width + m_gap), -top});
    }
}

// --- interaction ---

void AchievementsOverlay::selectCategory(int category) {
    if (category == m_category) return;
    m_category = category;
    auto node = m_tabs[category].node;
    m_underlineX.to(node->getPositionX(), 500, Easing::OutQuint);
    m_underlineW.to(node->getContentSize().width, 500, Easing::OutQuint);
    applyFilter();
}

void AchievementsOverlay::selectFilter(Filter filter) {
    if (filter == m_filter) return;
    m_filter = filter;
    applyFilter();
}

void AchievementsOverlay::onOpened() {
    // Achievements may have been earned since last time.
    loadEntries();
    applyFilter();
}

void AchievementsOverlay::onUpdate(float dt) {
    auto mouse = geode::cocos::getMousePos();
    bool interactive = isOpen() && !m_drag.dragging();

    for (auto& chip : m_tabs) {
        bool hovered = interactive && nodeContains(chip.node, mouse);
        if (hovered && !chip.hovered) sfx::hover(sfx::sound::DEFAULT_HOVER);
        chip.hovered = hovered;
        bool active = chip.value == m_category;
        chip.label->setColor(active ? theme::CONTENT1 : hovered ? theme::rgb(m_scheme.content2()) : theme::FOREGROUND1);
    }
    for (auto& chip : m_filters) {
        bool hovered = interactive && nodeContains(chip.node, mouse);
        if (hovered && !chip.hovered) sfx::hover(sfx::sound::DEFAULT_HOVER);
        chip.hovered = hovered;
        bool active = chip.value == static_cast<int>(m_filter);
        auto base = active ? m_scheme.colour3() : m_scheme.background6();
        chip.bg->setFillColor(hovered && !active ? theme::lerp(base, {255, 255, 255, 255}, 0.08f) : base);
        chip.label->setColor(active ? theme::CONTENT1 : theme::rgb(m_scheme.content2()));
    }

    m_underlineX.update(dt);
    m_underlineW.update(dt);
    m_tabUnderline->setPositionX(m_underlineX.get());
    m_tabUnderline->setContentSize({m_underlineW.get(), m_tabUnderline->getContentSize().height});

    layoutVisibleCards();

    // Card hover: a slightly lighter card.
    Entry* hovered = nullptr;
    if (interactive && m_scroll->containsWorldPoint(mouse)) {
        for (auto idx : m_shown) {
            auto& e = m_entries[idx];
            if (e.card && e.card->isVisible() && nodeContains(e.card, mouse)) { hovered = &e; break; }
        }
    }
    if (hovered != m_hoveredCard) {
        auto restore = [this](Entry* e) {
            if (e && e->cardBg) e->cardBg->setFillColor(e->earned ? m_scheme.background4() : m_scheme.background5());
        };
        restore(m_hoveredCard);
        if (hovered && hovered->cardBg) hovered->cardBg->setFillColor(m_scheme.dark4());
        m_hoveredCard = hovered;
    }
}

bool AchievementsOverlay::ccTouchBegan(CCTouch* touch, CCEvent* e) {
    auto loc = touch->getLocation();
    // Let the search box take its own touches.
    if (isOpen() && m_search && m_search->getInputNode() && nodeContains(m_search, loc)) return false;
    if (!WaveOverlay::ccTouchBegan(touch, e)) return false;

    m_pressedChip = nullptr;
    for (auto list : {&m_tabs, &m_filters}) {
        for (auto& chip : *list) {
            if (nodeContains(chip.node, loc)) m_pressedChip = &chip;
        }
    }
    if (!m_pressedChip) m_drag.began(m_scroll, loc);
    return true;
}

void AchievementsOverlay::ccTouchMoved(CCTouch* touch, CCEvent*) {
    m_drag.moved(touch->getLocation());
}

void AchievementsOverlay::ccTouchEnded(CCTouch* touch, CCEvent* e) {
    WaveOverlay::ccTouchEnded(touch, e);
    m_drag.ended();
    auto chip = m_pressedChip;
    m_pressedChip = nullptr;
    if (!chip || !nodeContains(chip->node, touch->getLocation())) return;

    sfx::click(sfx::sound::DEFAULT_SELECT);
    bool isTab = chip >= m_tabs.data() && chip < m_tabs.data() + m_tabs.size();
    if (isTab) selectCategory(chip->value);
    else selectFilter(static_cast<Filter>(chip->value));
}

} // namespace lazer
