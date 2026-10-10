#include "SongSelectInternal.hpp"

#include "../../audio/MusicPlayer.hpp"
#include "../../audio/Sfx.hpp"
#include "../core/MenuCursor.hpp"
#include "../core/Quips.hpp"
#include "../core/Theme.hpp"
#include "../menu/MenuBackground.hpp"
#include "../overlays/Dialog.hpp"

#include <algorithm>
#include <random>

using namespace geode::prelude;

namespace lazer {

namespace songselect {
    levels::Kind g_lastKind = levels::Kind::Classic;
    Remembered& remembered() {
        static Remembered r[levels::KIND_COUNT];
        return r[static_cast<int>(g_lastKind)];
    }

    Resume g_resume;
}

bool& SongSelect::returnsHere() {
    static bool value = false;
    return value;
}

bool& SongSelect::openingLevelPage() {
    static bool value = false;
    return value;
}

bool& SongSelect::browsingOnline() {
    static bool value = false;
    return value;
}

std::optional<levels::Kind>& SongSelect::onlineReturn() {
    static std::optional<levels::Kind> value;
    return value;
}

CCScene* SongSelect::onlineScene(browse::Request const& request) {
    onlineReturn().reset();
    // The search page opened again from the menu keeps what it showed (its
    // sort, its filters, its results); a search from elsewhere starts afresh.
    auto const& shown = browse::results();
    bool keep = request.searchPage && shown.request.searchPage && request.query.empty()
        && request.filters == browse::Filters {} && shown.count() > 0;
    browse::open(keep ? shown.request : request);
    return scene(levels::Kind::Online);
}

CCScene* SongSelect::timelyScene(GJTimedLevelType type) {
    onlineReturn().reset();
    // The safe: GD's list of the past ones, newest first (the one shown is
    // kept, with its results).
    SearchType safe = type == GJTimedLevelType::Weekly ? SearchType::WeeklySafe
                    : type == GJTimedLevelType::Event ? SearchType::EventSafe : SearchType::DailySafe;
    browse::open(browse::pageRequest(safe));
    return scene(levels::kindOf(type));
}

CCScene* SongSelect::gauntletScene(int gauntletID) {
    if (gauntletID > 0) {
        g_lastKind = levels::Kind::Gauntlets;
        auto& r = remembered();
        r.expandedPack = gauntletID;
        r.selectedId = gauntletID;
        r.selectedOfficial = false;
        r.selectedHeader = true;
    }
    return scene(levels::Kind::Gauntlets);
}

CCScene* SongSelect::scene(levels::Kind kind) {
    auto scene = CCScene::create();
    scene->addChild(SongSelect::create(kind, true));
    return scene;
}

CCScene* SongSelect::scene() {
    auto scene = CCScene::create();
    scene->addChild(SongSelect::create(g_lastKind));
    return scene;
}

SongSelect* SongSelect::create(levels::Kind kind, bool fromMenu) {
    auto ret = new SongSelect();
    if (ret->init(kind, fromMenu)) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

bool SongSelect::init(levels::Kind kind, bool fromMenu) {
    if (!CCLayer::init()) return false;
    returnsHere() = false; // only subsequent direct navigation should return here
    m_kind = kind;
    g_lastKind = kind;
    browsingOnline() = false;
    m_win = CCDirector::get()->getWinSize();
    m_k = unitScale();
    float k = m_k;
    this->setID("song-select"_spr);

    m_footerH = FOOTER_HEIGHT * k;
    float bonus = std::max(0.f, m_win.width / m_win.height - 2.f);
    m_rightW = std::clamp(m_win.width * 0.5f, 500 * k, (700 + bonus * 300) * k);
    m_rightW = std::min(m_rightW, m_win.width * 0.6f);
    m_leftW = std::min(m_win.width * 0.5f, (700 + bonus * 100) * k);
    m_panelH = PANEL_HEIGHT * k;
    m_spacing = PANEL_SPACING * k;
    m_carouselTop = m_win.height - FILTER_HEIGHT * k - 5 * k;
    m_carouselBottom = m_footerH + 5 * k;

    // Background: the selected level's thumbnail, blurred and dimmed.
    auto source = CCLayerGradient::create({34, 26, 50, 255}, {8, 8, 12, 255});
    source->setVisible(false);
    this->addChild(source, -10);
    m_background = MenuBackground::create(source, BACKGROUND_DIM, true, true);
    this->addChild(m_background, -5);

    // osu!'s side shading: darker behind the wedges and behind the carousel.
    auto leftShade = CCLayerGradient::create({0, 0, 0, 77}, {0, 0, 0, 0}, {1, 0});
    leftShade->setContentSize({m_win.width * 0.6f, m_win.height});
    this->addChild(leftShade, -4);
    auto rightShade = CCLayerGradient::create({0, 0, 0, 0}, {0, 0, 0, 128}, {1, 0});
    rightShade->setContentSize({m_rightW, m_win.height});
    rightShade->setPosition({m_win.width - m_rightW, 0});
    this->addChild(rightShade, -4);

    m_carousel = CCNode::create();
    this->addChild(m_carousel, 1);
    m_bar = RoundedBox::create({SCROLLBAR_WIDTH * k, SCROLLBAR_WIDTH * k}, SCROLLBAR_WIDTH / 2 * k, {136, 136, 136, 255});
    m_bar->setAnchorPoint({1, 0.5f});
    m_bar->setVisible(false);
    this->addChild(m_bar, 1);
    // The label beside the held bar.
    m_barLabel = CCNode::create();
    m_barLabelBg = RoundedBox::create({10, 10}, 8 * k, {24, 23, 28, 235});
    m_barLabelBg->setShadow(8 * k, {0, 0, 0, 90});
    m_barLabelBg->setAnchorPoint({1, 0.5f});
    m_barLabel->addChild(m_barLabelBg);
    m_barLabelText = makeText("", Weight::SemiBold, 22 * k);
    m_barLabelText->setAnchorPoint({1, 0.5f});
    m_barLabel->addChild(m_barLabelText);
    m_barLabel->setVisible(false);
    this->addChild(m_barLabel, 6);
    m_wedge = CCNode::create();
    this->addChild(m_wedge, 2);

    auto& r = remembered();
    if (onlineMode()) {
        // The request the store has (opened by the menu, or kept from before).
        auto const& results = browse::results();
        m_request = results.request;
        m_onlineLists = m_request.lists;
        m_query = m_request.query;
        m_browseGeneration = results.generation;
    } else {
        m_group = static_cast<Group>(std::clamp(r.group, 0, 2));
        m_folder = r.folder;
        m_sort = r.sort;
        m_query = r.query;
    }
    buildFilter();
    buildFooter();
    if (m_search && !m_query.empty()) m_search->setString(m_query);
    if (onlineMode()) {
        // Levels played since have saved copies and progress now.
        browse::refreshProgress();
        if (timelyMode()) {
            // GD's page for the daily (weekly, event) runs hidden under this
            // one: the current level, its timer, claim and skip are its.
            timely::attach(timelyType(), this);
            m_timely = timely::status(timelyType());
        }
        rebuildOnlineEntries();
        log::info("Song select: online, {} so far", browse::results().count());
    } else if (packMode()) {
        // The packs (and any pack's levels) come from the session's store,
        // told here whenever more arrive; GD's cache may fill it in at once.
        if (gauntletMode()) {
            gauntlets::setListener([this] { this->onPacksChanged(); });
            for (auto& p : gauntlets::all()) gauntlets::refresh(p);
        } else {
            packs::setListener([this] { this->onPacksChanged(); });
            for (auto& p : packs::all()) packs::refresh(p);
        }
        loadPackList();
        if (packListState() != packs::State::Loading) rebuildPackEntries();
        log::info("Song select: {} {}s", packList().size(), packWord());
        // Many players dread this page. So does the cursor.
        if (fromMenu && !gauntletMode()) {
            static std::mt19937 rng {std::random_device {}()};
            cursorSay(MAP_PACKS_CURSOR[std::uniform_int_distribution<size_t>(0, MAP_PACKS_CURSOR.size() - 1)(rng)]);
        }
    } else {
        m_entries = levels::all(m_kind);
        log::info("Song select: {} {} levels", m_entries.size(), m_kind == levels::Kind::Platformer ? "platformer" : "classic");
    }
    // Coming from the menu, its song carries on and picks the selection (osu!
    // selects the playing beatmap). Back from a level, the last selection stays.
    auto track = MusicPlayer::get().current();
    int playingID = track ? track->songID : 0;
    std::string playing = fromMenu ? MusicPlayer::get().handOff() : "";
    if (packMode() || onlineMode() || playing.empty() || !selectSong(playing, playingID)) applyFilter();
    if (packMode()) restoreExpandedPack();
    m_scroll = m_scrollTarget;

    this->setTouchEnabled(true);
    this->setKeypadEnabled(true);
    this->setKeyboardEnabled(true);
    this->scheduleUpdate();
    return true;
}

void SongSelect::onEnter() {
    CCLayer::onEnter();
    if (m_pageLaunch) return;
    CCDirector::get()->getMouseDispatcher()->addDelegate(this);
    // Back from a screen pushed over this one (a list's page): the results
    // speak to this again, and a page that page's request cut off is asked for again.
    if (onlineMode()) {
        browse::setListener([this] { this->onBrowseChanged(); });
        browse::resume();
    }
}

void SongSelect::onExit() {
    if (m_pageLaunch) {
        CCLayer::onExit();
        return;
    }
    CCDirector::get()->getMouseDispatcher()->removeDelegate(this);
    if (m_kind == levels::Kind::MapPacks) packs::setListener(nullptr);
    if (gauntletMode()) gauntlets::setListener(nullptr);
    if (onlineMode()) browse::setListener(nullptr);
    if (timelyMode()) timely::detach(timelyType());
    auto glm = GameLevelManager::sharedState();
    if (glm->m_leaderboardManagerDelegate == this) glm->m_leaderboardManagerDelegate = nullptr;
    stopListening();
    // Leaving with a level built but not entered (the push hands it over first).
    dropLevel();
    CCLayer::onExit();
}

void SongSelect::registerWithTouchDispatcher() {
    CCDirector::get()->getTouchDispatcher()->addTargetedDelegate(this, m_pageLaunch ? -500 : 0, true);
}

// --- top right: search, groups, sort ---

void SongSelect::buildFilter() {
    float k = m_k;
    float left = m_win.width - m_rightW;
    auto box = RoundedBox::create({m_rightW + 60 * k, FILTER_HEIGHT * k + 20 * k}, CORNER * k, {0, 0, 0, 170});
    box->setAnchorPoint({0, 0});
    box->setPosition({left, m_win.height - FILTER_HEIGHT * k});
    box->setSkewX(skewDegrees());
    this->addChild(box, 3);
    if (onlineMode()) return buildOnlineFilter();

    // Search field.
    float searchY = m_win.height - 30 * k;
    float searchW = m_rightW - 50 * k;
    auto field = RoundedBox::create({searchW, 36 * k}, 8 * k, {255, 255, 255, 28});
    field->setAnchorPoint({0, 0.5f});
    field->setPosition({left + 30 * k, searchY});
    this->addChild(field, 4);
    auto icon = makeIcon(icon::SEARCH, 16 * k);
    icon->setColor(theme::LIGHT1);
    icon->setPosition({left + 30 * k + 20 * k, searchY});
    this->addChild(icon, 5);

    float inputScale = 0.8f;
    m_search = TextInput::create((searchW - 150 * k) / inputScale, "type to search", "outfit-regular.fnt"_spr);
    // GD's own character filter drops punctuation: allow everything typeable.
    m_search->setCommonFilter(CommonFilter::Any);
    m_search->hideBG();
    m_search->setTextAlign(TextInputAlign::Left);
    m_search->setScale(inputScale);
    m_search->setAnchorPoint({0, 0.5f});
    m_search->setPosition({left + 30 * k + 38 * k, searchY});
    m_search->setCallback([this](std::string const& text) {
        m_query = text;
        remembered().query = text;
        applyFilter();
        m_noResultsMs = 900; // once the typing stops
    });
    this->addChild(m_search, 5);

    m_countLabel = makeText("", Weight::Regular, 15 * k);
    m_countLabel->setColor(theme::LIGHT1);
    m_countLabel->setAnchorPoint({1, 0.5f});
    m_countLabel->setPosition({left + 30 * k + searchW - 14 * k, searchY});
    this->addChild(m_countLabel, 5);

    // Groups (osu!'s collection / grouping), GD's folders and sort.
    float rowY = m_win.height - 72 * k;
    float x = left + 22 * k;
    // Packs: every pack, the ones still to finish, the finished ones.
    bool packs = packMode();
    char const* names[] = {packs ? "all" : "saved", packs ? "unfinished" : "official", packs ? "completed" : "liked"};
    char const* glyphs[] = {nullptr, nullptr, packs ? icon::CHECK : icon::HEART};
    for (int i = 0; i < 3; i++) {
        auto& tab = addButton(m_tabs, this, glyphs[i], names[i], {x, rowY}, 28 * k, TAB, [this, i] {
            m_group = static_cast<Group>(i);
            remembered().group = i;
            closeMenu();
            applyFilter();
        }, 0);
        x += tab.node->getContentSize().width + 6 * k;
    }

    auto& folders = addButton(m_buttons, this, icon::FOLDER, "all folders", {x + 4 * k, rowY}, 28 * k, TAB,
                              [this] { this->toggleFolders(); }, 0);
    m_folderButton = m_buttons.size() - 1;
    for (auto child : CCArrayExt<CCNode*>(folders.node->getChildren())) {
        if (auto label = typeinfo_cast<CCLabelBMFont*>(child); label && label->getTag() == 1) m_folderLabel = label;
    }
    x += 4 * k + folders.node->getContentSize().width + 6 * k;

    auto& sort = addButton(m_buttons, this, icon::SLIDERS, "sort: difficulty", {x, rowY}, 28 * k, TAB, [this] {
        m_sort = static_cast<levels::Sort>((static_cast<int>(m_sort) + 1) % 4);
        remembered().sort = m_sort;
        applyFilter();
    }, 0);
    for (auto child : CCArrayExt<CCNode*>(sort.node->getChildren())) {
        if (auto label = typeinfo_cast<CCLabelBMFont*>(child); label && label->getTag() == 1) m_sortLabel = label;
    }
}

void SongSelect::toggleFolders() {
    if (m_menu) return closeMenu();
    if (m_folderButton >= m_buttons.size()) return;
    float k = m_k;
    std::vector<int> ids;
    for (auto const& e : m_entries) {
        if (!e.official && e.folder > 0 && std::find(ids.begin(), ids.end(), e.folder) == ids.end()) ids.push_back(e.folder);
    }
    std::sort(ids.begin(), ids.end());

    auto& anchor = m_buttons[m_folderButton];
    float itemH = 28 * k, gap = 4 * k, pad = 6 * k;
    float width = std::max(anchor.node->getContentSize().width, 170 * k);
    size_t count = ids.size() + (ids.empty() ? 2 : 1);
    float height = count * itemH + (count - 1) * gap + pad * 2;

    auto menu = openMenu(anchor);
    float y = -pad - itemH / 2;
    auto item = [&](std::string const& name, int folder) {
        std::function<void()> action;
        if (folder >= 0) {
            action = [this, folder] {
                m_folder = folder;
                remembered().folder = folder;
                closeMenu();
                applyFilter();
            };
        }
        auto& b = addButton(m_menuItems, menu, folder > 0 ? icon::FOLDER : nullptr, name, {0, y}, itemH, TAB,
                            std::move(action), 0);
        b.selected = folder == m_folder;
        // Full-width rows.
        b.node->setContentSize({width, itemH});
        b.bg->setContentSize({width, itemH});
        y -= itemH + gap;
    };
    item("all folders", 0);
    for (int id : ids) item(levels::folderName(id), id);
    if (ids.empty()) item("no folders yet", -1);
    finishMenu(width, height);
}

// A dropdown hanging from under its button: what's put in it goes downwards
// from y 0 (x 0 at the button's left edge), and finishMenu draws the box.
CCNode* SongSelect::openMenu(Button& anchor) {
    closeMenu();
    float k = m_k;
    auto node = anchor.node;
    m_menu = CCNode::create();
    m_menu->setPosition({node->getPositionX(), node->getPositionY() - node->getContentSize().height / 2 - 6 * k});
    this->addChild(m_menu, 30);
    m_menuAnchor = &anchor;
    return m_menu;
}

void SongSelect::finishMenu(float width, float height) {
    if (!m_menu) return;
    float k = m_k, pad = 6 * k;
    auto bg = RoundedBox::create({width + pad * 2, height}, 8 * k, {20, 18, 28, 245});
    bg->setShadow(12 * k, {0, 0, 0, 120});
    bg->setAnchorPoint({0, 1});
    bg->setPosition({-pad, 0});
    m_menu->addChild(bg, -1);
    // Off the right edge of the screen: slide it left.
    float right = m_menu->getPositionX() + width + pad;
    if (right > m_win.width - 6 * k) m_menu->setPositionX(m_win.width - 6 * k - width - pad);
}

void SongSelect::closeMenu() {
    if (!m_menu) return;
    if (m_pressed >= m_menuItems.data() && m_pressed < m_menuItems.data() + m_menuItems.size()) m_pressed = nullptr;
    m_menuItems.clear();
    m_menu->removeFromParent();
    m_menu = nullptr;
    m_menuAnchor = nullptr;
}

void SongSelect::confirmDeleteUnhearted() {
    int count = levels::countUnhearted();
    if (count == 0) {
        Dialog::show(icon::CIRCLE_INFO, "Nothing to delete", "Every saved level is hearted or in a folder.",
                     {{"OK", Dialog::Kind::Cancel, nullptr}});
        return;
    }
    Ref<SongSelect> self = this;
    Dialog::show(icon::TRASH, "Delete unhearted levels?",
        fmt::format("{} saved level{} that aren't hearted or in a folder. This can't be undone.",
                    count, count == 1 ? "" : "s"), {
        {"Yes. Go for it.", Dialog::Kind::Dangerous, [self] {
            levels::deleteUnhearted();
            quips::say("delete-unhearted");
            self->reloadEntries();
        }},
        {"No! Abort mission", Dialog::Kind::Cancel, nullptr},
    });
}

// The selected saved level, after asking (osu!'s BeatmapDeleteDialog). The
// selection moves on to the next level (the one before, at the end of the list).
void SongSelect::confirmDeleteLevel() {
    if (!m_hasSelection || m_selected >= m_visible.size()) return;
    auto const& e = m_entries[m_visible[m_selected]];
    if (e.official || e.pack >= 0 || !e.level || levels::specialCopy(e.level)) return;
    Ref<GJGameLevel> level = e.level;
    Ref<SongSelect> self = this;
    Dialog::show(icon::TRASH, "Confirm deletion of", fmt::format("{} by {}", e.name, e.creator), {
        {"Yes. Go for it.", Dialog::Kind::Dangerous, [self, level] {
            auto& visible = self->m_visible;
            auto& entries = self->m_entries;
            auto it = std::find_if(visible.begin(), visible.end(), [&](size_t i) { return entries[i].level == level; });
            if (it == visible.end()) return;
            size_t at = it - visible.begin();
            auto deleted = entries[*it];
            if (visible.size() > 1) {
                // applyFilter keeps the selected level: make that the neighbour.
                self->m_selected = at + 1 < visible.size() ? at + 1 : at - 1;
                self->m_hasSelection = true;
            }
            levels::deleteLevel(deleted);
            self->reloadEntries();
        }},
        {"No! Abort mission", Dialog::Kind::Cancel, nullptr},
    });
}

// --- bottom: footer ---

void SongSelect::buildFooter() {
    float k = m_k;
    auto bar = CCLayerColor::create({0, 0, 0, 150}, m_win.width, m_footerH);
    this->addChild(bar, 4);

    float h = m_footerH - 10 * k;
    float y = m_footerH / 2;
    auto& back = addButton(m_buttons, this, icon::CHEVRON_LEFT, "back", {-12 * k, y}, h, PINK, [this] { this->back(); }, skewDegrees());
    float x = back.node->getPositionX() + back.node->getContentSize().width + 14 * k;
    auto& random = addButton(m_buttons, this, icon::SHUFFLE, "random", {x, y}, h, TAB, [this] { this->selectRandom(); }, skewDegrees());
    x += random.node->getContentSize().width + 10 * k;
    if (!packMode() && !onlineMode()) {
        // Online levels: our search page, with what's typed here already in it.
        auto& browse = addButton(m_buttons, this, icon::GLOBE, "browse", {x, y}, h, TAB, [this] { this->browseOnline(); }, skewDegrees());
        x += browse.node->getContentSize().width + 10 * k;
    }
    auto& page = addButton(m_buttons, this, icon::CIRCLE_INFO, "level page", {x, y}, h, TAB, [this] { this->openLevelPage(); }, skewDegrees());
    m_pageButton = m_buttons.size() - 1;
    x += page.node->getContentSize().width + 10 * k;
    // Packs' levels (and online ones) aren't yours to delete from here.
    if (!packMode() && !onlineMode()) {
        addButton(m_buttons, this, icon::TRASH, "delete unhearted", {x, y}, h, TAB, [this] { this->confirmDeleteUnhearted(); }, skewDegrees());
    }

    auto& play = addButton(m_buttons, this, icon::PLAY, "play", {0, y}, h, PURPLE, [this] { this->start(); }, skewDegrees());
    play.node->setPositionX(m_win.width - play.node->getContentSize().width + 12 * k);
}

SongSelect::Button& SongSelect::addButton(std::vector<Button>& list, CCNode* parent, char const* glyph,
                                          std::string const& label, CCPoint pos, float height, ccColor4B color,
                                          std::function<void()> action, float skew) {
    float k = m_k;
    float pad = height * 0.55f;
    auto node = CCNode::create();
    node->setAnchorPoint({0, 0.5f});

    CCLabelBMFont* iconLabel = nullptr;
    float contentW = 0;
    if (glyph) {
        iconLabel = makeIcon(glyph, height * 0.42f);
        contentW += iconLabel->getScaledContentSize().width + 8 * k;
    }
    auto text = makeText(label, Weight::SemiBold, height * 0.5f);
    text->setTag(1);
    contentW += text->getScaledContentSize().width;
    float w = std::max(contentW + pad * 2, height * 2.2f);
    node->setContentSize({w, height});

    auto bg = RoundedBox::create({w, height}, std::min(8 * k, height / 2), color);
    bg->setAnchorPoint({0, 0});
    if (skew != 0) bg->setSkewX(skew);
    node->addChild(bg);

    float x = (w - contentW) / 2;
    if (iconLabel) {
        iconLabel->setAnchorPoint({0, 0.5f});
        iconLabel->setPosition({x, height / 2});
        node->addChild(iconLabel, 1);
        x += iconLabel->getScaledContentSize().width + 8 * k;
    }
    text->setAnchorPoint({0, 0.5f});
    text->setPosition({x, height / 2});
    node->addChild(text, 1);

    node->setPosition(pos);
    parent->addChild(node, 5);
    list.push_back({node, bg, color, std::move(action)});
    return list.back();
}

SongSelect::Button& SongSelect::addIconButton(std::vector<Button>& list, CCNode* parent, char const* glyph, CCPoint pos,
                                              float height, ccColor4B color, std::function<void()> action) {
    auto& b = addButton(list, parent, glyph, "", pos, height, color, std::move(action), 0);
    CCSize size {height * 1.3f, height};
    b.node->setContentSize(size);
    b.bg->setContentSize(size);
    for (auto child : CCArrayExt<CCNode*>(b.node->getChildren())) {
        if (child == b.bg) continue;
        child->setAnchorPoint({0.5f, 0.5f});
        child->setPosition(size / 2);
    }
    return b;
}

} // namespace lazer
