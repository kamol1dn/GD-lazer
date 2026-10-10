#include "SongSelectInternal.hpp"

#include "../../audio/Sfx.hpp"
#include "../core/MenuCursor.hpp"
#include "../core/Theme.hpp"
#include "../menu/Toolbar.hpp"
#include "../overlays/Dialog.hpp"
#include "../overlays/GameplayButtons.hpp"

#include <Geode/binding/LevelSearchLayer.hpp>

#include <algorithm>
#include <functional>

using namespace geode::prelude;

namespace lazer {

// --- online (Kind::Online): GD's lists as song select ---
//
// The request shown is m_request, edited by the controls above the carousel
// (the search box, levels or lists, the sort, the filters); the browse store
// fetches its pages and tells onBrowseChanged. The pager beside the count
// moves between its pages.

namespace {
    // Buttons other mods add to GD's search screen (Level Grind's, ...): GD's
    // screen is never shown, so they move to the search page. Found in a
    // fresh hidden LevelSearchLayer by their path of child indices, which is
    // the same in every fresh one (see the creator hub's mod buttons).
    struct SearchModButton {
        std::vector<unsigned> path;
        std::string id;
        CCSprite* image = nullptr; // a copy of the button's own picture
    };

    std::vector<SearchModButton> scanSearchModButtons(CCNode* layer) {
        std::vector<SearchModButton> found;
        std::vector<unsigned> path;
        std::function<void(CCNode*, bool)> walk = [&](CCNode* node, bool inMenu) {
            unsigned i = 0;
            for (auto child : CCArrayExt<CCNode*>(node->getChildren())) {
                path.push_back(i++);
                if (auto item = typeinfo_cast<CCMenuItem*>(child); item && inMenu) {
                    if (item->isVisible() && isModButton(item)) {
                        CCNode* image = nullptr;
                        if (auto sprite = typeinfo_cast<CCMenuItemSprite*>(item)) image = sprite->getNormalImage();
                        found.push_back({path, item->getID(), image ? snapshotNode(image) : nullptr});
                    }
                } else if (child->isVisible()) {
                    walk(child, inMenu || typeinfo_cast<CCMenu*>(child));
                }
                path.pop_back();
            }
        };
        walk(layer, false);
        return found;
    }

    // Presses a mod's search button in a fresh hidden search screen (its
    // handler may use the screen).
    void searchModAction(std::vector<unsigned> const& path) {
        static Ref<LevelSearchLayer> layer;
        layer = LevelSearchLayer::create(0);
        if (!layer) return;
        CCNode* node = layer;
        for (auto i : path) {
            auto children = node->getChildren();
            if (!children || i >= children->count()) return;
            node = static_cast<CCNode*>(children->objectAtIndex(i));
        }
        if (auto item = typeinfo_cast<CCMenuItem*>(node)) item->activate();
    }

    struct PageText {
        char const* icon;
        char const* title;
    };
    // A fixed list's name (the search page has the search box instead).
    PageText pageText(browse::Request const& r) {
        bool lists = r.lists;
        switch (r.type) {
            case SearchType::Featured: return lists ? PageText {icon::LAYERS, "lists"} : PageText {icon::STAR, "featured"};
            case SearchType::HallOfFame: return {icon::AWARD, "hall of fame"};
            case SearchType::Magic: return {icon::WAND_MAGIC, "magic"};
            case SearchType::Recent: return {icon::CLOCK, "recent"};
            case SearchType::Sent: return {icon::PAPER_PLANE, "sent"};
            case SearchType::Followed: return {icon::USER_CHECK, "followed"};
            case SearchType::Friends: return {icon::USERS, "friends"};
            case SearchType::Downloaded: return {icon::CLOUD_DOWN, "most downloaded"};
            case SearchType::MostLiked: return {icon::THUMBS_UP, "most liked"};
            case SearchType::Trending: return {icon::BOLT, "trending"};
            case SearchType::Awarded: return {icon::MEDAL, "awarded"};
            case SearchType::StarAward: return {icon::STAR, "star awarded"};
            case SearchType::DailySafe: return {icon::CALENDAR_DAY, "daily"};
            case SearchType::WeeklySafe: return {icon::CALENDAR_WEEK, "weekly"};
            case SearchType::EventSafe: return {icon::BOLT, "event"};
            default: return lists ? PageText {icon::LAYERS, "lists"} : PageText {icon::LIST, "levels"};
        }
    }

    std::string commas(long long v) {
        std::string s = std::to_string(v);
        for (int i = static_cast<int>(s.size()) - 3; i > 0; i -= 3) s.insert(static_cast<size_t>(i), ",");
        return s;
    }

    std::string trim(std::string s) {
        return utils::string::trim(s);
    }

    // A button's text (addButton tags it).
    CCLabelBMFont* labelOf(CCNode* button) {
        for (auto child : CCArrayExt<CCNode*>(button->getChildren())) {
            if (auto label = typeinfo_cast<CCLabelBMFont*>(child); label && label->getTag() == 1) return label;
        }
        return nullptr;
    }

    bool optionActive(browse::Filters const& f, int row, int option) {
        switch (row) {
            case FILTER_DIFFICULTY: return (f.difficulty >> option) & 1;
            case FILTER_DEMON: return f.demon == option;
            case FILTER_LENGTH: return (f.length >> option) & 1;
            case FILTER_RATING: return (f.general >> option) & 1;
            case FILTER_EXTRAS: return (f.general >> (browse::EXTRAS_FIRST + option)) & 1;
            case FILTER_PLAYED: return f.played == option;
            default: return false;
        }
    }
}

std::vector<packs::Pack>& SongSelect::packList() {
    if (onlineMode()) return browse::lists();
    return gauntletMode() ? gauntlets::all() : packs::all();
}

packs::State SongSelect::packListState() const {
    if (onlineMode()) return browse::results().state == browse::State::Loading ? packs::State::Loading : packs::State::Loaded;
    return gauntletMode() ? gauntlets::state() : packs::state();
}

void SongSelect::loadPackList() {
    if (onlineMode()) return browse::refresh();
    if (gauntletMode()) gauntlets::load();
    else packs::load();
}

void SongSelect::loadAllPackLevels() {
    if (onlineMode()) return;
    if (gauntletMode()) gauntlets::loadAllLevels();
    else packs::loadAllLevels();
}

float SongSelect::packLevelsProgress() const {
    if (onlineMode()) return 1.f;
    return gauntletMode() ? gauntlets::levelsProgress() : packs::levelsProgress();
}

char const* SongSelect::packWord() const {
    if (onlineMode()) return "list";
    return gauntletMode() ? "gauntlet" : "map pack";
}

packs::Pack* SongSelect::packOf(levels::Entry const& e) {
    auto& packs = packList();
    if (e.pack < 0 || static_cast<size_t>(e.pack) >= packs.size()) return nullptr;
    return &packs[e.pack];
}

void SongSelect::loadPackLevels(int index) {
    if (index < 0) return;
    if (onlineMode()) browse::loadListLevels(static_cast<size_t>(index));
    else if (gauntletMode()) gauntlets::loadLevels(static_cast<size_t>(index));
    else packs::loadLevels(static_cast<size_t>(index));
}

bool SongSelect::packLevelsLoading() const {
    if (onlineMode()) return browse::loadingListLevels();
    return gauntletMode() ? gauntlets::loadingLevels() : packs::loadingLevels();
}

bool SongSelect::canClaimPack(packs::Pack const& p) const {
    // A list's reward comes from GD's own list page.
    if (onlineMode()) return false;
    return gauntletMode() ? gauntlets::canClaim(p) : packs::canClaim(p);
}

// --- the controls above the carousel ---

void SongSelect::buildOnlineFilter() {
    float k = m_k;
    float left = m_win.width - m_rightW;
    float H = m_win.height;
    float searchY = H - 30 * k;
    float x0 = left + 30 * k;
    float rowW = m_rightW - 50 * k;
    float xRight = x0 + rowW;

    // How many there are, at the right, with a spinner while a page is on its way.
    float countW = 100 * k;
    m_countLabel = makeText("", Weight::Regular, 15 * k);
    m_countLabel->setColor(theme::LIGHT1);
    m_countLabel->setAnchorPoint({1, 0.5f});
    m_countLabel->setPosition({xRight - 4 * k, searchY});
    this->addChild(m_countLabel, 5);
    {
        auto holder = CCNode::create();
        auto glyph = makeIcon(icon::ROTATE, 13 * k);
        glyph->setColor(theme::LIGHT1);
        glyph->setAnchorPoint({0, 0});
        if (auto letter = glyph->getChildByType<CCSprite>(0)) glyph->setPosition(-letter->getPosition() * glyph->getScale());
        holder->addChild(glyph);
        holder->setPosition({xRight - countW - 8 * k, searchY});
        holder->setVisible(false);
        this->addChild(holder, 5);
        m_countSpinner = holder;
    }

    // The pager: < [page] / pages >
    float ph = 26 * k;
    float px = xRight - countW - 22 * k; // the right edge of what's placed next
    auto& next = addIconButton(m_buttons, this, icon::CHEVRON_RIGHT, {px - ph * 1.3f, searchY}, ph, TAB,
                               [this] { this->goToPage(this->currentPage() + 1); });
    px -= next.node->getContentSize().width + 4 * k;
    m_pageTotal = makeText("/ 1", Weight::Regular, 12 * k);
    m_pageTotal->setColor(theme::LIGHT1);
    m_pageTotal->setAnchorPoint({1, 0.5f});
    m_pageTotal->setPosition({px, searchY});
    this->addChild(m_pageTotal, 5);
    px -= 36 * k + 4 * k;
    float boxW = 42 * k;
    auto box = RoundedBox::create({boxW, ph}, 6 * k, {255, 255, 255, 28});
    box->setAnchorPoint({1, 0.5f});
    box->setPosition({px, searchY});
    this->addChild(box, 4);
    float inputScale = 0.75f;
    m_pageInput = TextInput::create(boxW / inputScale, "1", "outfit-regular.fnt"_spr);
    m_pageInput->hideBG();
    m_pageInput->setTextAlign(TextInputAlign::Center);
    m_pageInput->setCommonFilter(CommonFilter::Uint);
    m_pageInput->setMaxCharCount(4);
    m_pageInput->setScale(inputScale);
    m_pageInput->setAnchorPoint({1, 0.5f});
    m_pageInput->setPosition({px, searchY});
    m_pageInput->setDelegate(this);
    this->addChild(m_pageInput, 5);
    px -= boxW + 4 * k;
    auto& prev = addIconButton(m_buttons, this, icon::CHEVRON_LEFT, {px - ph * 1.3f, searchY}, ph, TAB,
                               [this] { this->goToPage(this->currentPage() - 1); });
    float fieldRight = prev.node->getPositionX() - 12 * k;

    if (m_request.searchPage) {
        // The search box.
        float searchW = fieldRight - x0;
        auto field = RoundedBox::create({searchW, 36 * k}, 8 * k, {255, 255, 255, 28});
        field->setAnchorPoint({0, 0.5f});
        field->setPosition({x0, searchY});
        this->addChild(field, 4);
        auto magnifier = makeIcon(icon::SEARCH, 16 * k);
        magnifier->setColor(theme::LIGHT1);
        magnifier->setPosition({x0 + 20 * k, searchY});
        this->addChild(magnifier, 5);
        float scale = 0.8f;
        m_search = TextInput::create((searchW - 50 * k) / scale, "type to search, or a level ID", "outfit-regular.fnt"_spr);
        // GD's own character filter drops punctuation: allow everything typeable.
        m_search->setCommonFilter(CommonFilter::Any);
        m_search->hideBG();
        m_search->setTextAlign(TextInputAlign::Left);
        m_search->setScale(scale);
        m_search->setAnchorPoint({0, 0.5f});
        m_search->setPosition({x0 + 38 * k, searchY});
        m_search->setCallback([this](std::string const& text) {
            auto query = trim(text);
            if (query == m_request.query) return;
            m_request.query = query;
            m_query = query;
            // Typing waits a little before asking GD (osu!'s search debounce).
            queueOnlineSearch(ONLINE_QUERY_DEBOUNCE);
            m_noResultsMs = 900;
        });
        this->addChild(m_search, 5);
    } else {
        // A fixed list: its name where the search box would be.
        auto text = pageText(m_request);
        auto glyph = makeIcon(text.icon, 18 * k);
        glyph->setColor(theme::CONTENT2);
        glyph->setPosition({x0 + 12 * k, searchY});
        this->addChild(glyph, 5);
        auto title = makeText(text.title, Weight::SemiBold, 22 * k);
        title->setAnchorPoint({0, 0.5f});
        title->setPosition({x0 + 34 * k, searchY});
        fit(title, timelyMode() ? 120 * k : fieldRight - x0 - 40 * k);
        this->addChild(title, 5);
        if (timelyMode()) {
            // The countdown to the next one, kept up to date in updateTimely.
            m_timerLabel = makeText("", Weight::Regular, 15 * k);
            m_timerLabel->setColor(theme::LIGHT1);
            m_timerLabel->setAnchorPoint({0, 0.5f});
            m_timerLabel->setPosition({x0 + 40 * k + title->getScaledContentSize().width, searchY});
            this->addChild(m_timerLabel, 5);
            m_timerText.clear();
        }
    }

    // Row two: levels or lists, the sort, the filters, refresh.
    float rowY = H - 72 * k;
    float x = left + 22 * k;
    if (timelyMode()) {
        // The safe under the current level: refresh, and what the rows are.
        auto& refresh = addIconButton(m_buttons, this, icon::ROTATE, {x, rowY}, 28 * k, TAB, [this] {
            closeMenu();
            timely::refresh(timelyType());
            browse::refresh();
        });
        x += refresh.node->getContentSize().width + 10 * k;
        auto name = levels::timelyName(timelyType());
        auto hint = makeText(fmt::format("the current {} level, then the safe: every past one, newest first", name), Weight::Regular, 13 * k);
        hint->setColor(theme::LIGHT1);
        hint->setAnchorPoint({0, 0.5f});
        hint->setPosition({x, rowY});
        fit(hint, xRight - x - 10 * k);
        this->addChild(hint, 5);
        updateOnlineLabels();
        return;
    }
    bool canLists = m_request.searchPage || m_request.type == SearchType::Featured;
    if (canLists) {
        for (int i = 0; i < 2; i++) {
            bool lists = i == 1;
            auto& tab = addButton(m_tabs, this, lists ? icon::LAYERS : icon::CUBE, lists ? "lists" : "levels", {x, rowY}, 28 * k, TAB,
                                  [this, lists] {
                closeMenu();
                if (m_request.lists == lists) return;
                m_request.lists = lists;
                applyOnlineRequest();
            }, 0);
            x += tab.node->getContentSize().width + 6 * k;
        }
        x += 4 * k;
    }
    if (m_request.searchPage) {
        auto& sort = addButton(m_buttons, this, icon::ARROW_DOWN_WIDE_SHORT, "sort: most downloaded", {x, rowY}, 28 * k, TAB,
                               [this] { this->openSortMenu(); }, 0);
        m_sortButton = m_buttons.size() - 1;
        m_sortLabel = labelOf(sort.node);
        x += sort.node->getContentSize().width + 6 * k;
    }
    auto& filters = addButton(m_buttons, this, icon::FILTER, "filters", {x, rowY}, 28 * k, TAB, [this] {
        // A second tap closes it.
        if (m_menu && m_filtersButton < m_buttons.size() && m_menuAnchor == &m_buttons[m_filtersButton]) closeMenu();
        else openFilterMenu();
    }, 0);
    m_filtersButton = m_buttons.size() - 1;
    m_filtersLabel = labelOf(filters.node);
    x += filters.node->getContentSize().width + 6 * k;
    auto& refresh = addIconButton(m_buttons, this, icon::ROTATE, {x, rowY}, 28 * k, TAB, [this] {
        closeMenu();
        browse::refresh();
    });
    x += refresh.node->getContentSize().width + 6 * k;

    // Other mods' buttons on GD's search screen, after GD's own controls.
    if (m_request.searchPage) {
        if (auto scanned = Ref(LevelSearchLayer::create(0))) {
            for (auto& button : scanSearchModButtons(scanned)) {
                log::debug("Search page mod button: {}", button.id);
                float h = 28 * k;
                if (x + h * 1.3f > xRight) break; // out of room on a narrow screen
                auto& b = addIconButton(m_buttons, this, nullptr, {x, rowY}, h, TAB, [this, path = button.path] {
                    closeMenu();
                    searchModAction(path);
                });
                if (button.image) {
                    // The mod's own picture in place of a glyph, fitted in.
                    auto bounds = button.image->getScaledContentSize();
                    float fit = std::max(bounds.width, bounds.height);
                    if (fit > 0) button.image->setScale(button.image->getScale() * h * 0.72f / fit);
                    button.image->setPosition(b.node->getContentSize() / 2);
                    b.node->addChild(button.image, 1);
                } else {
                    auto glyph = makeIcon(icon::PUZZLE, h * 0.42f);
                    glyph->setPosition(b.node->getContentSize() / 2);
                    b.node->addChild(glyph, 1);
                }
                x += b.node->getContentSize().width + 6 * k;
            }
        }
    }
    updateOnlineLabels();
}

void SongSelect::updateOnlineLabels() {
    auto const& r = browse::results();
    if (m_sortLabel) {
        std::string text = "sort: relevance";
        if (m_request.query.empty()) {
            for (auto const& s : SORTS) if (s.type == m_request.type) text = fmt::format("sort: {}", s.label);
        }
        if (text != m_sortLabel->getString()) {
            m_sortLabel->setString(text.c_str());
            fitLabel(m_sortLabel, m_sortLabel->getParent());
        }
    }
    if (m_filtersLabel) {
        int n = m_request.filters.count();
        std::string text = n > 0 ? fmt::format("filters ({})", n) : "filters";
        if (text != m_filtersLabel->getString()) {
            m_filtersLabel->setString(text.c_str());
            fitLabel(m_filtersLabel, m_filtersLabel->getParent());
        }
    }
    if (m_countLabel) {
        std::string what = m_onlineLists ? "list" : "level";
        std::string text;
        if (r.total >= 0) text = fmt::format("{} {}{}", commas(r.total), what, r.total == 1 ? "" : "s");
        else if (r.state == browse::State::Loading && r.count() == 0) text = m_request.query.empty() ? "loading..." : "searching...";
        else text = fmt::format("{} {}s so far", r.count(), what);
        if (timelyMode() && r.total >= 0) text = fmt::format("{} in the safe", commas(r.total));
        m_countLabel->setString(text.c_str());
    }
    if (m_pageTotal) {
        int pages = r.total > 0 ? (r.total + browse::PER_PAGE - 1) / browse::PER_PAGE : std::max(1, r.lastPage() + 1);
        m_pageTotal->setString(fmt::format("/ {}", pages).c_str());
    }
    for (size_t i = 0; i < m_tabs.size(); i++) m_tabs[i].selected = (i == 1) == m_onlineLists;
}

void SongSelect::openSortMenu() {
    if (m_sortButton >= m_buttons.size()) return;
    auto& anchor = m_buttons[m_sortButton];
    if (m_menu && m_menuAnchor == &anchor) return closeMenu();
    if (!m_request.query.empty()) {
        closeMenu();
        cursorSay("with a search, GD sorts by relevance");
        return;
    }
    float k = m_k;
    float itemH = 28 * k, gap = 4 * k, pad = 6 * k;
    float width = std::max(anchor.node->getContentSize().width, 200 * k);
    size_t count = std::size(SORTS);
    float height = count * itemH + (count - 1) * gap + pad * 2;
    auto menu = openMenu(anchor);
    float y = -pad - itemH / 2;
    for (auto const& s : SORTS) {
        auto type = s.type;
        auto& b = addButton(m_menuItems, menu, s.icon, s.label, {0, y}, itemH, TAB, [this, type] {
            closeMenu();
            if (m_request.type == type) return;
            m_request.type = type;
            applyOnlineRequest();
        }, 0);
        b.selected = type == m_request.type;
        b.node->setContentSize({width, itemH});
        b.bg->setContentSize({width, itemH});
        y -= itemH + gap;
    }
    finishMenu(width, height);
}

void SongSelect::openFilterMenu() {
    if (m_filtersButton >= m_buttons.size()) return;
    auto& anchor = m_buttons[m_filtersButton];
    bool rebuild = m_menu && m_menuAnchor == &anchor;
    float k = m_k;
    float itemH = 24 * k, gap = 5 * k, pad = 8 * k, labelW = 76 * k;
    float width = m_rightW - 20 * k;
    auto menu = openMenu(anchor);
    auto const& f = m_request.filters;
    float y = -pad;
    for (int row = 0; row < FILTER_ROW_COUNT; row++) {
        // The demon kind only matters with demons chosen (GD's search screen greys it out).
        if (row == FILTER_DEMON && !((f.difficulty >> browse::DIFF_DEMON) & 1)) continue;
        auto const& def = FILTER_ROWS[row];
        y -= itemH / 2;
        auto label = makeText(def.label, Weight::SemiBold, 12 * k);
        label->setColor(theme::LIGHT1);
        label->setAnchorPoint({0, 0.5f});
        label->setPosition({0, y});
        menu->addChild(label, 1);
        float x = labelW;
        for (int i = 0; i < def.count; i++) {
            auto& b = addButton(m_menuItems, menu, nullptr, def.options[i], {x, y}, itemH, TAB,
                                [this, row, i] { this->toggleFilterOption(row, i); }, 0);
            float w = b.node->getContentSize().width;
            if (x > labelW && x + w > width) {
                // Onto the next line.
                x = labelW;
                y -= itemH + gap;
                b.node->setPosition({x, y});
            }
            b.selected = optionActive(f, row, i);
            x += w + 5 * k;
        }
        y -= itemH / 2 + gap + 4 * k;
    }
    if (f.count() > 0) {
        y -= 15 * k;
        addButton(m_menuItems, menu, icon::XMARK, "clear filters", {0, y}, 26 * k, TAB, [this] {
            m_request.filters.clear();
            updateOnlineLabels();
            queueOnlineSearch(ONLINE_FILTER_DEBOUNCE);
            openFilterMenu();
        }, 0);
        y -= 15 * k;
    }
    finishMenu(width, -y + pad - gap);
    if (rebuild) m_pressed = nullptr;
}

void SongSelect::toggleFilterOption(int row, int option) {
    auto& f = m_request.filters;
    uint32_t bit = 1u << option;
    switch (row) {
        case FILTER_DIFFICULTY:
            f.difficulty ^= bit;
            // A demon kind goes with demons alone.
            if (option != browse::DIFF_DEMON || !(f.difficulty & bit)) f.demon = 0;
            break;
        case FILTER_DEMON:
            if (f.demon == option) return;
            f.demon = option;
            if (option > 0) f.difficulty = 1u << browse::DIFF_DEMON;
            break;
        case FILTER_LENGTH:
            f.length ^= bit;
            break;
        case FILTER_RATING:
            f.general ^= bit;
            // Rated and unrated rule each other out.
            if (option == browse::RATED && (f.general & bit)) f.general &= ~(1u << browse::UNRATED);
            if (option == browse::UNRATED && (f.general & bit)) f.general &= ~(1u << browse::RATED);
            break;
        case FILTER_EXTRAS:
            f.general ^= 1u << (browse::EXTRAS_FIRST + option);
            break;
        case FILTER_PLAYED:
            if (f.played == option) return;
            f.played = option;
            break;
        default:
            return;
    }
    updateOnlineLabels();
    queueOnlineSearch(ONLINE_FILTER_DEBOUNCE);
    // The chips' states (and the demon row) follow.
    openFilterMenu();
}

// --- the results ---

void SongSelect::queueOnlineSearch(float delayMs) {
    m_onlineSearchDelay = delayMs;
}

void SongSelect::applyOnlineRequest() {
    m_onlineSearchDelay = -1;
    m_pendingPage = -1;
    updateOnlineLabels();
    // The same request as the results shown is kept; anything else starts
    // afresh (onBrowseChanged hears about it).
    browse::open(m_request);
}

void SongSelect::rebuildOnlineEntries() {
    auto const& r = browse::results();
    m_onlineLists = r.request.lists;
    if (packMode()) {
        rebuildPackEntries();
        return;
    }
    // Pages only ever add rows at the end: the panels' entry indices hold.
    m_entries = r.levels;
    if (!timelyMode()) return;
    // The current level on top (GD's own copy of it, with its daily ID and
    // progress). The safe's newest is usually the same level: that row is
    // then the current one, so it doesn't show twice.
    bool hadRow = m_timelyRow;
    size_t hadShift = m_timelyShift;
    m_timelyRow = false;
    m_timelyShift = 0;
    if (m_timely.state == timely::State::Ready && m_timely.level) {
        auto current = levels::fromLevel(m_timely.level, false);
        if (current.dailyID <= 0) current.dailyID = m_timely.dailyID;
        bool same = !m_entries.empty() && m_entries.front().dailyID > 0 && m_entries.front().dailyID == current.dailyID;
        if (same) m_entries.front() = current;
        else {
            m_entries.insert(m_entries.begin(), current);
            m_timelyShift = 1;
        }
        m_timelyRow = true;
    }
    if (hadRow != m_timelyRow || hadShift != m_timelyShift) {
        // The rows moved: no panel can be reused.
        for (auto& [index, panel] : m_panels) panel.root->removeFromParent();
        m_panels.clear();
    }
}

// --- the daily, weekly and event pages ---

void SongSelect::updateTimely(float dt) {
    m_timelyPollMs += dt * 1000.f;
    if (m_timelyPollMs < 100) return;
    m_timelyPollMs = 0;
    auto st = timely::status(timelyType());
    // The countdown.
    if (m_timerLabel) {
        std::string text;
        bool event = timelyType() == GJTimedLevelType::Event;
        if (st.state == timely::State::Failed) text = "couldn't reach GD's servers";
        else if (st.activeID > st.dailyID && st.dailyID > 0) text = "a newer one is up";
        // Event levels come whenever RobTop sets one: no countdown for those.
        else if (event) text = st.state == timely::State::Waiting ? "the next one comes when it comes" : "";
        else if (st.secondsLeft > 0) {
            text = (st.state == timely::State::Waiting ? "next one in " : "new one in ") + timely::timeLeft(st.secondsLeft);
        }
        if (text != m_timerText) {
            m_timerText = text;
            m_timerLabel->setString(text.c_str());
        }
    }
    if (st.version == m_timely.version) return;
    bool rowsChange = st.state != m_timely.state || st.level != m_timely.level || st.dailyID != m_timely.dailyID;
    m_timely = st;
    if (m_starting) {
        m_browseDirty = true;
        return;
    }
    if (rowsChange) onBrowseChanged();
    else refreshDetails();
}

void SongSelect::claimTimely() {
    if (!m_timely.claimable) return;
    sfx::play(sfx::sound::DIALOG_OK_SELECT);
    timely::claim(timelyType());
}

void SongSelect::skipTimely() {
    if (!m_timely.skippable) return;
    auto type = timelyType();
    auto name = levels::timelyName(type);
    Dialog::show(icon::CHEVRON_RIGHT, fmt::format("Skip this {} level?", name),
        fmt::format("A newer {} level is up. Skipping takes you to it; this one stays in the safe, where it can still be beaten as {} #{}.",
                    name, name, levels::timelyNumber(m_timely.dailyID)), {
        {"Skip it", Dialog::Kind::Ok, [type] { timely::skip(type); }},
        {"Keep this one", Dialog::Kind::Cancel, nullptr},
    });
}

void SongSelect::onBrowseChanged() {
    if (!onlineMode()) return;
    m_browseDirty = false;
    auto const& r = browse::results();
    if (r.generation != m_browseGeneration) {
        // A fresh request: the rows go, and the controls follow it (GD's
        // own screens may have opened one).
        m_browseGeneration = r.generation;
        for (auto& [index, panel] : m_panels) panel.root->removeFromParent();
        m_panels.clear();
        m_expandedPack = -1;
        m_hasSelection = false;
        m_lastSelection = {};
        m_scroll = m_scrollTarget = 0;
        m_request = r.request;
        m_query = m_request.query;
        if (m_search && std::string(m_search->getString()) != m_query) m_search->setString(m_query);
    }
    // The loader is up: the rows wait (the scene is rebuilt after a play;
    // a cancelled one picks them up).
    if (m_starting) {
        m_browseDirty = true;
        return;
    }
    rebuildOnlineEntries();
    updateOnlineLabels();

    auto before = m_lastSelection;
    float scroll = m_scrollTarget;
    m_hasSelection = false;
    applyFilter();
    if (m_hasSelection && m_lastSelection == before) {
        // The same level stays selected: no jump back to it, and its details
        // (a list's levels arriving) are redrawn in place.
        m_scrollTarget = scroll;
        refreshDetails();
    }
    if (packMode()) restoreExpandedPack();
    if (m_pendingPage >= 0) {
        if (scrollToPageIfLoaded(m_pendingPage)) m_pendingPage = -1;
        else if (!r.more || r.state == browse::State::Failed) m_pendingPage = -1;
    }
    if (r.state == browse::State::Failed && !m_visible.empty()) cursorSay("the servers said no");
}

void SongSelect::updateOnline(float dt) {
    float ms = dt * 1000.f;
    if (m_onlineSearchDelay >= 0) {
        m_onlineSearchDelay -= ms;
        if (m_onlineSearchDelay < 0) applyOnlineRequest();
    }
    auto const& r = browse::results();
    // The next page as the end of this one nears the view (osu! pages on scroll).
    if (!m_starting && r.state == browse::State::Loaded && r.more && !m_visible.empty() && !m_rowTops.empty()) {
        if (m_scroll + viewHeight() * 2.5f > m_rowTops.back()) browse::loadMore();
    }
    // The pager's box follows the view.
    if (m_pageInput && !m_pageInputOpen) {
        int page = currentPage();
        if (page != m_shownPage) {
            m_shownPage = page;
            m_pageInput->setString(std::to_string(page + 1));
        }
    }
    if (m_countSpinner) {
        bool on = r.state == browse::State::Loading && !m_visible.empty();
        m_countSpinner->setVisible(on);
        if (on) m_countSpinner->setRotation(m_countSpinner->getRotation() + dt * 300.f);
    }
}

// --- the pager ---

int SongSelect::currentPage() const {
    auto const& r = browse::results();
    if (m_visible.empty()) return r.firstPage;
    size_t index = 0;
    if (m_rowTops.size() == m_visible.size() + 1) {
        auto it = std::upper_bound(m_rowTops.begin(), m_rowTops.end() - 1, m_scroll + viewHeight() / 2);
        index = static_cast<size_t>(std::max<std::ptrdiff_t>(0, (it - m_rowTops.begin()) - 1));
        index = std::min(index, m_visible.size() - 1);
    }
    auto const& e = m_entries[m_visible[index]];
    // A list's level counts as its list; the current daily on top is on the first page.
    size_t item = packMode() ? static_cast<size_t>(std::max(0, e.pack)) : m_visible[index];
    if (!packMode() && item < onlineShift()) return r.firstPage;
    return browse::pageOf(item - (packMode() ? 0 : onlineShift()));
}

void SongSelect::goToPage(int page) {
    auto const& r = browse::results();
    page = std::max(0, page);
    if (r.total > 0) page = std::min(page, (r.total - 1) / browse::PER_PAGE);
    closeMenu();
    if (scrollToPageIfLoaded(page)) return;
    if (page == r.lastPage() + 1 && (r.more || r.state == browse::State::Loading)) {
        // The next one: it scrolls into view once it's here.
        m_pendingPage = page;
        browse::loadMore();
        return;
    }
    // Further away: the results start again from there.
    m_pendingPage = -1;
    browse::open(m_request, page, true);
}

bool SongSelect::scrollToPageIfLoaded(int page) {
    auto const& r = browse::results();
    if (r.pagesLoaded == 0 || page < r.firstPage || page > r.lastPage()) return false;
    size_t item = static_cast<size_t>(page - r.firstPage) * browse::PER_PAGE + (packMode() ? 0 : onlineShift());
    for (size_t v = 0; v < m_visible.size(); v++) {
        auto const& e = m_entries[m_visible[v]];
        size_t mine = packMode() ? (e.packHeader ? static_cast<size_t>(e.pack) : SIZE_MAX) : m_visible[v];
        if (mine != item) continue;
        select(v);
        return true;
    }
    return false;
}

void SongSelect::textInputOpened(CCTextInputNode*) {
    m_pageInputOpen = true;
    m_pageCommitted = false;
}

void SongSelect::textInputClosed(CCTextInputNode*) {
    m_pageInputOpen = false;
    if (m_pageCommitted) return;
    m_pageCommitted = true;
    commitPageInput();
}

void SongSelect::enterPressed(CCTextInputNode*) {
    // GD doesn't call this for its inputs (keyDown handles Enter); kept in
    // case a build does.
    if (!m_pageCommitted) {
        m_pageCommitted = true;
        commitPageInput();
    }
    if (m_pageInput) m_pageInput->defocus();
}

void SongSelect::commitPageInput() {
    if (!m_pageInput) return;
    auto n = utils::numFromString<int>(std::string(m_pageInput->getString()));
    if (n && *n >= 1) {
        if (*n - 1 != currentPage()) goToPage(*n - 1);
    }
    m_shownPage = -1; // the box shows the page it landed on
}

} // namespace lazer
