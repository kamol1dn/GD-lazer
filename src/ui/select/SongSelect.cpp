#include "SongSelect.hpp"

#include "../../audio/Sfx.hpp"
#include "../../integrations/LevelThumbnails.hpp"
#include "../core/Text.hpp"
#include "../core/Theme.hpp"
#include "../menu/MenuBackground.hpp"

#include <Geode/modify/GameManager.hpp>
#include <Geode/modify/LevelInfoLayer.hpp>
#include <Geode/utils/base64.hpp>
#include <algorithm>
#include <cmath>
#include <random>

using namespace geode::prelude;

namespace lazer {

namespace {
    // osu! sizes (768 px tall screen), scaled by m_k.
    constexpr float PANEL_HEIGHT = 72;   // PanelBeatmapStandalone.HEIGHT
    constexpr float PANEL_SPACING = 3;   // BeatmapCarousel.SPACING
    constexpr float ACTIVE_X = 25;       // Panel.active_x_offset
    constexpr float CORNER = 10;         // Panel.CORNER_RADIUS
    constexpr float FOOTER_HEIGHT = 50;  // ScreenFooter.HEIGHT
    constexpr float FILTER_HEIGHT = 96;
    constexpr float STRIP_WIDTH = 64;
    constexpr float SHEAR = 0.2f;        // OsuGame.SHEAR
    constexpr float PREVIEW_DELAY = 150; // SongSelect.SELECTION_DEBOUNCE
    constexpr float THUMB_DELAY = 150;
    constexpr double SCROLL_DECAY = 0.989;

    constexpr ccColor4B PANEL_BG {36, 34, 44, 235};
    constexpr ccColor4B PANEL_HOVER {58, 54, 72, 245};
    constexpr ccColor4B PINK {238, 51, 153, 255};
    constexpr ccColor4B PURPLE {102, 68, 204, 255};
    constexpr ccColor4B TAB {60, 56, 76, 255};

    // Kept between visits (and across a round trip into gameplay), per kind.
    struct Remembered {
        int group = 0;
        int folder = 0;
        levels::Sort sort = levels::Sort::Default;
        std::string query;
        int selectedId = -1;
        bool selectedOfficial = false;
    };
    levels::Kind g_lastKind = levels::Kind::Classic;
    Remembered& remembered() {
        static Remembered r[2];
        return r[static_cast<int>(g_lastKind)];
    }

    // Platformer times, GD style: 1:23.456 (or 23.456 under a minute).
    std::string formatTime(int ms) {
        int minutes = ms / 60000;
        int seconds = ms / 1000 % 60;
        if (minutes > 0) return fmt::format("{}:{:02}.{:03}", minutes, seconds, ms % 1000);
        return fmt::format("{}.{:03}", seconds, ms % 1000);
    }

    // Our popups get the Lazer look (see PopupStyle).
    void showStyled(FLAlertLayer* popup) {
        popup->setUserObject("restyle"_spr, CCBool::create(true));
        popup->show();
    }

    // Stars for classic levels, moons for platformers.
    char const* rewardIcon(levels::Entry const& e) { return e.platformer ? icon::MOON : icon::STAR; }

    float skewDegrees() { return CC_RADIANS_TO_DEGREES(std::atan(SHEAR)); }

    std::string lower(std::string s) {
        for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
    }

    // A button's label changed: keep it inside the button's (fixed-size) background.
    void fitLabel(CCLabelBMFont* label, CCNode* button) {
        auto base = static_cast<CCFloat*>(label->getUserObject("base-scale"_spr));
        if (!base) {
            base = CCFloat::create(label->getScale());
            label->setUserObject("base-scale"_spr, base);
        }
        label->setScale(base->getValue());
        float room = button->getContentSize().width - button->getContentSize().height * 0.45f - label->getPositionX();
        float w = label->getScaledContentSize().width;
        if (w > room && w > 0) label->setScale(base->getValue() * room / w);
    }

    // Shrinks a label to fit `maxWidth`, cutting it with an ellipsis if it would get too small.
    void fit(CCLabelBMFont* label, float maxWidth) {
        float base = label->getScale();
        float w = label->getScaledContentSize().width;
        if (w <= maxWidth) return;
        if (w * 0.85f <= maxWidth) {
            label->setScale(base * maxWidth / w);
            return;
        }
        std::string text = label->getString();
        while (text.size() > 1 && label->getScaledContentSize().width > maxWidth) {
            text.pop_back();
            label->setString((text + "...").c_str());
        }
    }

    int difficultyRank(int frame) {
        switch (frame) {
            case 0: return 0;   // N/A
            case -1: return 1;  // auto
            case 7: return 7;   // easy demon
            case 8: return 8;
            case 6: return 9;   // hard demon
            case 9: return 10;
            case 10: return 11;
            default: return frame + 1; // 1-5 -> 2-6
        }
    }

    // A small horizontal run of icon + text pairs.
    CCNode* infoRow(std::vector<std::pair<char const*, std::string>> const& items, float size, ccColor3B color) {
        auto row = CCNode::create();
        float x = 0;
        for (auto const& [glyph, text] : items) {
            if (glyph) {
                auto icon = makeIcon(glyph, size * 0.85f);
                icon->setColor(color);
                icon->setAnchorPoint({0, 0.5f});
                icon->setPosition({x, 0});
                row->addChild(icon);
                x += icon->getScaledContentSize().width + size * 0.3f;
            }
            auto label = makeText(text, Weight::SemiBold, size);
            label->setColor(color);
            label->setAnchorPoint({0, 0.5f});
            label->setPosition({x, 0});
            row->addChild(label);
            x += label->getScaledContentSize().width + size * 0.9f;
        }
        row->setContentSize({x, size});
        return row;
    }

    GJFeatureState featureState(GJGameLevel* level) {
        switch (level->m_isEpic) {
            case 1: return GJFeatureState::Epic;
            case 2: return GJFeatureState::Legendary;
            case 3: return GJFeatureState::Mythic;
            default: return level->m_featured > 0 ? GJFeatureState::Featured : GJFeatureState::None;
        }
    }

    CCNode* difficultyFace(levels::Entry const& e, float size) {
        auto face = GJDifficultySprite::create(e.difficulty, GJDifficultyName::Short);
        if (!e.official) face->updateFeatureState(featureState(e.level));
        auto s = face->getContentSize();
        face->setScale(size / std::max(1.f, std::max(s.width, s.height)));
        return face;
    }

    // RobTop's levels have bundled screenshots (their IDs mean other levels
    // online); saved levels come from the Level Thumbnails server.
    void levelThumbnail(levels::Entry const& e, std::function<void(CCTexture2D*)> callback) {
        if (e.official) thumbnails::fetchOfficial(e.id, std::move(callback));
        else thumbnails::fetch(e.id, std::move(callback));
    }

    bool containsWorld(CCNode* node, CCPoint world) {
        auto local = node->convertToNodeSpace(world);
        auto size = node->getContentSize();
        return local.x >= 0 && local.y >= 0 && local.x <= size.width && local.y <= size.height;
    }
}

bool& SongSelect::returnsHere() {
    static bool value = false;
    return value;
}

CCScene* SongSelect::scene(levels::Kind kind) {
    auto scene = CCScene::create();
    scene->addChild(SongSelect::create(kind));
    return scene;
}

CCScene* SongSelect::scene() {
    return scene(g_lastKind);
}

SongSelect* SongSelect::create(levels::Kind kind) {
    auto ret = new SongSelect();
    if (ret->init(kind)) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

bool SongSelect::init(levels::Kind kind) {
    if (!CCLayer::init()) return false;
    m_kind = kind;
    g_lastKind = kind;
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
    m_background = MenuBackground::create(source, 0.55f, true, true);
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
    m_wedge = CCNode::create();
    this->addChild(m_wedge, 2);

    buildFilter();
    buildFooter();

    auto& r = remembered();
    m_group = static_cast<Group>(std::clamp(r.group, 0, 2));
    m_folder = r.folder;
    m_sort = r.sort;
    m_query = r.query;
    if (m_search && !m_query.empty()) m_search->setString(m_query);
    m_entries = levels::all(m_kind);
    log::info("Song select: {} {} levels", m_entries.size(), m_kind == levels::Kind::Platformer ? "platformer" : "classic");
    applyFilter();
    m_scroll = m_scrollTarget;

    this->setTouchEnabled(true);
    this->setKeypadEnabled(true);
    this->setKeyboardEnabled(true);
    this->scheduleUpdate();
    return true;
}

void SongSelect::onEnter() {
    CCLayer::onEnter();
    CCDirector::get()->getMouseDispatcher()->addDelegate(this);
}

void SongSelect::onExit() {
    CCDirector::get()->getMouseDispatcher()->removeDelegate(this);
    auto glm = GameLevelManager::sharedState();
    if (glm->m_leaderboardManagerDelegate == this) glm->m_leaderboardManagerDelegate = nullptr;
    CCLayer::onExit();
}

void SongSelect::registerWithTouchDispatcher() {
    CCDirector::get()->getTouchDispatcher()->addTargetedDelegate(this, 0, true);
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
    m_search->hideBG();
    m_search->setTextAlign(TextInputAlign::Left);
    m_search->setScale(inputScale);
    m_search->setAnchorPoint({0, 0.5f});
    m_search->setPosition({left + 30 * k + 38 * k, searchY});
    m_search->setCallback([this](std::string const& text) {
        m_query = text;
        remembered().query = text;
        applyFilter();
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
    char const* names[] = {"saved", "official", "liked"};
    char const* glyphs[] = {nullptr, nullptr, icon::HEART};
    for (int i = 0; i < 3; i++) {
        auto& tab = addButton(m_tabs, this, glyphs[i], names[i], {x, rowY}, 28 * k, TAB, [this, i] {
            m_group = static_cast<Group>(i);
            remembered().group = i;
            closeFolders();
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
    if (m_folderMenu) return closeFolders();
    float k = m_k;
    std::vector<int> ids;
    for (auto const& e : m_entries) {
        if (!e.official && e.folder > 0 && std::find(ids.begin(), ids.end(), e.folder) == ids.end()) ids.push_back(e.folder);
    }
    std::sort(ids.begin(), ids.end());

    auto anchor = m_buttons[m_folderButton].node;
    float itemH = 28 * k, gap = 4 * k, pad = 6 * k;
    float width = std::max(anchor->getContentSize().width, 170 * k);
    size_t count = ids.size() + (ids.empty() ? 2 : 1);
    float height = count * itemH + (count - 1) * gap + pad * 2;

    m_folderMenu = CCNode::create();
    m_folderMenu->setPosition({anchor->getPositionX(), anchor->getPositionY() - itemH / 2 - 6 * k - height});
    this->addChild(m_folderMenu, 30);
    auto bg = RoundedBox::create({width + pad * 2, height}, 8 * k, {20, 18, 28, 245});
    bg->setShadow(12 * k, {0, 0, 0, 120});
    bg->setAnchorPoint({0, 0});
    bg->setPosition({-pad, 0});
    m_folderMenu->addChild(bg);

    float y = height - pad - itemH / 2;
    auto item = [&](std::string const& name, int folder) {
        std::function<void()> action;
        if (folder >= 0) {
            action = [this, folder] {
                m_folder = folder;
                remembered().folder = folder;
                closeFolders();
                applyFilter();
            };
        }
        auto& b = addButton(m_folderItems, m_folderMenu, folder > 0 ? icon::FOLDER : nullptr, name, {0, y}, itemH, TAB,
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
}

void SongSelect::closeFolders() {
    if (!m_folderMenu) return;
    if (m_pressed >= m_folderItems.data() && m_pressed < m_folderItems.data() + m_folderItems.size()) m_pressed = nullptr;
    m_folderItems.clear();
    m_folderMenu->removeFromParent();
    m_folderMenu = nullptr;
}

void SongSelect::confirmDeleteUnhearted() {
    int count = levels::countUnhearted();
    if (count == 0) {
        showStyled(createQuickPopup("Nothing to delete", "Every saved level is hearted or in a folder.", "OK", nullptr,
                                    [](auto, bool) {}, false));
        return;
    }
    Ref<SongSelect> self = this;
    showStyled(createQuickPopup(
        "Delete unhearted levels",
        fmt::format("Delete <cr>{}</c> saved level{} that aren't hearted or in a folder? This can't be undone.",
                    count, count == 1 ? "" : "s"),
        "Cancel", "Delete",
        [self](auto, bool yes) {
            if (!yes) return;
            levels::deleteUnhearted();
            self->m_entries = levels::all(self->m_kind);
            self->applyFilter();
        },
        false
    ));
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
    auto& page = addButton(m_buttons, this, icon::CIRCLE_INFO, "level page", {x, y}, h, TAB, [this] { this->openLevelPage(); }, skewDegrees());
    x += page.node->getContentSize().width + 10 * k;
    addButton(m_buttons, this, icon::TRASH, "delete unhearted", {x, y}, h, TAB, [this] { this->confirmDeleteUnhearted(); }, skewDegrees());

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

// --- data ---

void SongSelect::applyFilter() {
    int keepId = -1;
    bool keepOfficial = false;
    if (m_hasSelection && m_selected < m_visible.size()) {
        auto const& e = m_entries[m_visible[m_selected]];
        keepId = e.id;
        keepOfficial = e.official;
    } else {
        keepId = remembered().selectedId;
        keepOfficial = remembered().selectedOfficial;
    }

    std::string query = lower(m_query);
    m_visible.clear();
    for (size_t i = 0; i < m_entries.size(); i++) {
        auto const& e = m_entries[i];
        if ((m_group == Group::Official) != e.official) continue;
        if (m_group == Group::Liked && !levels::favorited(e)) continue;
        if (m_group != Group::Official && m_folder != 0 && e.folder != m_folder) continue;
        if (!query.empty() && e.search.find(query) == std::string::npos) continue;
        m_visible.push_back(i);
    }

    auto& entries = m_entries;
    switch (m_sort) {
        case levels::Sort::Title:
            std::stable_sort(m_visible.begin(), m_visible.end(), [&](size_t a, size_t b) {
                return lower(entries[a].name) < lower(entries[b].name);
            });
            break;
        case levels::Sort::Difficulty:
            std::stable_sort(m_visible.begin(), m_visible.end(), [&](size_t a, size_t b) {
                int ra = difficultyRank(entries[a].difficulty), rb = difficultyRank(entries[b].difficulty);
                if (ra != rb) return ra < rb;
                return entries[a].stars < entries[b].stars;
            });
            break;
        case levels::Sort::Progress:
            std::stable_sort(m_visible.begin(), m_visible.end(), [&](size_t a, size_t b) {
                return entries[a].normalPercent > entries[b].normalPercent;
            });
            break;
        case levels::Sort::Default:
            break;
    }

    // Indices changed: rebuild the visible panels.
    for (auto& [index, panel] : m_panels) panel.root->removeFromParent();
    m_panels.clear();

    for (size_t i = 0; i < m_tabs.size(); i++) m_tabs[i].selected = static_cast<int>(m_group) == static_cast<int>(i);
    // RobTop's levels aren't in folders.
    auto& folderButton = m_buttons[m_folderButton];
    folderButton.node->setVisible(m_group != Group::Official);
    folderButton.selected = m_folder != 0;
    if (m_folderLabel) {
        m_folderLabel->setString(m_folder == 0 ? "all folders" : levels::folderName(m_folder).c_str());
        fitLabel(m_folderLabel, folderButton.node);
    }
    static char const* SORT_NAMES[] = {"sort: default", "sort: title", "sort: difficulty", "sort: progress"};
    if (m_sortLabel) {
        m_sortLabel->setString(SORT_NAMES[static_cast<int>(m_sort)]);
        fitLabel(m_sortLabel, m_sortLabel->getParent());
    }
    if (m_countLabel) {
        m_countLabel->setString(fmt::format("{} {} level{}", m_visible.size(),
            m_kind == levels::Kind::Platformer ? "platformer" : "classic", m_visible.size() == 1 ? "" : "s").c_str());
    }

    if (m_visible.empty()) {
        m_hasSelection = false;
        updateWedge();
        return;
    }
    size_t index = 0;
    for (size_t i = 0; i < m_visible.size(); i++) {
        auto const& e = m_entries[m_visible[i]];
        if (e.id == keepId && e.official == keepOfficial) {
            index = i;
            break;
        }
    }
    m_hasSelection = false; // force the selection to refresh
    select(index);
}

float SongSelect::itemTop(size_t visibleIndex) const {
    return visibleIndex * (m_panelH + m_spacing);
}

float SongSelect::viewHeight() const {
    return m_carouselTop - m_carouselBottom;
}

void SongSelect::select(size_t visibleIndex, bool scroll) {
    if (m_visible.empty()) return;
    visibleIndex = std::min(visibleIndex, m_visible.size() - 1);
    bool changed = !m_hasSelection || visibleIndex != m_selected;
    m_selected = visibleIndex;
    m_hasSelection = true;

    if (scroll) m_scrollTarget = itemTop(visibleIndex) + m_panelH / 2 - viewHeight() / 2;
    if (!changed) return;

    auto const& e = m_entries[m_visible[visibleIndex]];
    remembered().selectedId = e.id;
    remembered().selectedOfficial = e.official;
    updateWedge();
    m_previewDelay = PREVIEW_DELAY;

    // Background: the level's thumbnail.
    int request = ++m_backgroundRequest;
    Ref<SongSelect> self = this;
    levelThumbnail(e, [self, request](CCTexture2D* texture) {
        if (self->m_backgroundRequest != request) return;
        self->m_background->setImage(texture);
    });
}

void SongSelect::selectRandom() {
    if (m_visible.size() < 2) return;
    static std::mt19937 rng {std::random_device {}()};
    size_t index = std::uniform_int_distribution<size_t>(0, m_visible.size() - 2)(rng);
    if (index >= m_selected) index++;
    sfx::play(sfx::sound::DEFAULT_SELECT);
    select(index);
}

void SongSelect::start() {
    if (!m_hasSelection || m_starting) return;
    auto const& e = m_entries[m_visible[m_selected]];
    if (!levels::readyToPlay(e)) {
        openLevelPage();
        return;
    }
    // Like GD's level page: the preview song stops now and the level starts its
    // own music when it begins. (Left playing, the preview ran on into the level.)
    m_starting = true;
    m_previewDelay = -1;
    FMODAudioEngine::sharedEngine()->stopAllMusic(true);
    sfx::play(sfx::sound::MENU_PLAY_SELECT);
    returnsHere() = true;
    CCDirector::get()->replaceScene(CCTransitionFade::create(0.5f, PlayLayer::scene(e.level, false, false)));
}

void SongSelect::openLevelPage() {
    if (!m_hasSelection) return;
    auto const& e = m_entries[m_visible[m_selected]];
    // RobTop's levels have no level page: straight into the level.
    if (e.official) {
        start();
        return;
    }
    sfx::play(sfx::sound::DEFAULT_SELECT);
    returnsHere() = true;
    CCDirector::get()->replaceScene(CCTransitionFade::create(0.5f, LevelInfoLayer::scene(e.level, false)));
}

void SongSelect::back() {
    sfx::play(sfx::sound::DEFAULT_SELECT);
    returnsHere() = false;
    CCDirector::get()->replaceScene(CCTransitionFade::create(0.5f, MenuLayer::scene(false)));
}

void SongSelect::previewSong() {
    if (!m_hasSelection) return;
    auto const& e = m_entries[m_visible[m_selected]];
    if (e.songPath.empty() || e.songPath == m_previewPath) return;
    m_previewPath = e.songPath;
    auto engine = FMODAudioEngine::sharedEngine();
    engine->playMusic(e.songPath, true, 0.5f, 0);
    // No preview points in GD: start a little way in, where most songs have got going.
    unsigned length = engine->getMusicLengthMS(0);
    if (length > 0) engine->setMusicTimeMS(static_cast<unsigned>(length * 0.35f), true, 0);
}

// --- left: title wedge + details ---

void SongSelect::updateWedge(bool animate) {
    // Rebuilt from scratch: drop the old level's buttons (and any press on them).
    if (m_pressed >= m_wedgeButtons.data() && m_pressed < m_wedgeButtons.data() + m_wedgeButtons.size()) m_pressed = nullptr;
    m_wedgeButtons.clear();
    m_details = nullptr;
    m_songWidget = nullptr;
    m_wedge->removeAllChildren();
    if (animate) {
        m_wedge->setPositionX(-24 * m_k);
        m_wedgeAlpha.set(0);
        m_wedgeAlpha.to(1, 300, Easing::OutQuint);
    }
    float k = m_k;
    float H = m_win.height;
    float w = m_leftW;

    if (!m_hasSelection) {
        std::string text = "no levels match your search";
        if (m_entries.empty()) text = "no levels yet";
        else if (m_query.empty() && m_group == Group::Liked) text = "no hearted levels yet";
        else if (m_query.empty() && m_folder != 0) text = "nothing in this folder";
        auto none = makeText(text, Weight::SemiBold, 26 * k);
        none->setColor(theme::LIGHT1);
        none->setAnchorPoint({0, 0.5f});
        none->setPosition({40 * k, H - 60 * k});
        m_wedge->addChild(none);
        if (m_group == Group::Liked && m_query.empty()) {
            auto hint = makeText("heart a level with the heart next to its title", Weight::Regular, 17 * k);
            hint->setColor(theme::CONTENT2);
            hint->setAnchorPoint({0, 0.5f});
            hint->setPosition({40 * k, H - 94 * k});
            m_wedge->addChild(hint);
        }
        return;
    }
    auto const& e = m_entries[m_visible[m_selected]];

    // Title wedge.
    float titleH = 170 * k;
    auto titleBox = RoundedBox::create({w + 60 * k, titleH + 20 * k}, CORNER * k, {0, 0, 0, 165});
    titleBox->setAnchorPoint({0, 0});
    titleBox->setPosition({-60 * k, H - titleH});
    titleBox->setSkewX(skewDegrees());
    m_wedge->addChild(titleBox);

    float x0 = 36 * k;
    float maxW = w - x0 - 40 * k;
    float titleW = e.official ? maxW : maxW - 56 * k;
    auto title = makeText(e.name, Weight::SemiBold, 36 * k);
    title->setAnchorPoint({0, 0.5f});
    title->setPosition({x0, H - 38 * k});
    fit(title, titleW);
    m_wedge->addChild(title, 1);

    // Heart (GD's favourite): saved levels only, like the level page.
    if (!e.official) {
        size_t index = m_wedgeButtons.size();
        float heartH = 30 * k;
        auto& heart = addButton(m_wedgeButtons, m_wedge, icon::HEART, "", {x0 + title->getScaledContentSize().width + 14 * k, H - 38 * k}, heartH,
                                levels::favorited(e) ? PINK : TAB, [this, index] {
            if (!m_hasSelection || index >= m_wedgeButtons.size()) return;
            auto const& entry = m_entries[m_visible[m_selected]];
            bool on = !levels::favorited(entry);
            levels::setFavorited(entry, on);
            m_wedgeButtons[index].color = on ? PINK : TAB;
            sfx::play(on ? sfx::sound::CHECK_ON : sfx::sound::CHECK_OFF);
        }, 0);
        // Icon only: a small square-ish pill, icon centred.
        CCSize size {heartH * 1.4f, heartH};
        heart.node->setContentSize(size);
        heart.bg->setContentSize(size);
        for (auto child : CCArrayExt<CCNode*>(heart.node->getChildren())) {
            if (child == heart.bg) continue;
            child->setAnchorPoint({0.5f, 0.5f});
            child->setPosition(size / 2);
        }
    }

    auto song = infoRow({{icon::MUSIC, e.songArtist.empty() ? e.songTitle : e.songTitle + "  -  " + e.songArtist}},
                        17 * k, theme::CONTENT2);
    song->setPosition({x0 + 2 * k, H - 76 * k});
    if (song->getContentSize().width > maxW) song->setScale(maxW / song->getContentSize().width);
    m_wedge->addChild(song, 1);

    auto creator = makeText("by " + e.creator, Weight::Regular, 17 * k);
    creator->setColor(theme::LIGHT1);
    creator->setAnchorPoint({0, 0.5f});
    creator->setPosition({x0, H - 102 * k});
    m_wedge->addChild(creator, 1);

    // Difficulty and stats.
    float statsY = H - 140 * k;
    auto face = difficultyFace(e, 34 * k);
    face->setPosition({x0 + 17 * k, statsY});
    m_wedge->addChild(face, 1);
    std::vector<std::pair<char const*, std::string>> stats;
    if (e.stars > 0) stats.push_back({rewardIcon(e), std::to_string(e.stars)});
    if (!e.platformer) stats.push_back({icon::CLOCK, levels::lengthName(e.length)});
    if (e.coins > 0) stats.push_back({icon::COINS, fmt::format("{}/{}", e.coinsCollected, e.coins)});
    if (!e.official) stats.push_back({icon::ID_CARD, std::to_string(e.id)});
    auto statsRow = infoRow(stats, 17 * k, theme::CONTENT1);
    statsRow->setPosition({x0 + 44 * k, statsY});
    m_wedge->addChild(statsRow, 1);

    // Details panel, scrollable: there's more than fits on short screens.
    float top = H - titleH - 8 * k;
    float bottom = m_footerH + 10 * k;
    auto details = RoundedBox::create({w + 60 * k, top - bottom}, CORNER * k, {0, 0, 0, 130});
    details->setAnchorPoint({0, 0});
    // Sheared from the bottom: shift left so its top edge lines up under the title wedge.
    details->setPosition({-60 * k - (top - bottom) * SHEAR, bottom});
    details->setSkewX(skewDegrees());
    m_wedge->addChild(details);
    buildDetails(top, bottom);
}

void SongSelect::buildDetails(float top, float bottom) {
    auto const& e = m_entries[m_visible[m_selected]];
    auto level = e.level.data();
    auto accent = levels::difficultyColor(e.difficulty);
    float k = m_k;
    float x0 = 36 * k;
    // The panel is sheared: its right edge slants left towards the bottom by
    // (height x shear). Keep everything inside the narrowest part.
    float maxW = m_leftW - x0 - 40 * k - (top - bottom) * SHEAR;

    // Content hangs from the top of the scroll area: y is negative, downwards.
    float inset = 10 * k;
    m_details = ScrollArea::create({maxW + inset * 2, top - bottom - 12 * k});
    m_details->setOwnsWheel(false); // scrollWheel below routes it
    m_details->setPosition({x0 - inset, bottom + 6 * k});
    m_wedge->addChild(m_details, 1);
    auto content = m_details->content();
    float y = -22 * k;

    auto add = [&](CCNode* node, float x, float atY) {
        node->setPosition({inset + x, atY});
        content->addChild(node);
    };
    auto section = [&](char const* text) {
        y -= 6 * k;
        auto label = makeText(text, Weight::SemiBold, 15 * k);
        label->setColor(theme::LIGHT1);
        label->setAnchorPoint({0, 0.5f});
        add(label, 0, y);
        y -= 28 * k;
    };
    auto bar = [&](char const* name, int percent, ccColor3B color) {
        float barW = maxW - 130 * k;
        auto label = makeText(name, Weight::Regular, 15 * k);
        label->setAnchorPoint({0, 0.5f});
        add(label, 0, y);
        auto track = RoundedBox::create({barW, 8 * k}, 4 * k, {255, 255, 255, 30});
        track->setAnchorPoint({0, 0.5f});
        add(track, 80 * k, y);
        if (percent > 0) {
            auto fill = RoundedBox::create({barW * std::clamp(percent, 0, 100) / 100.f, 8 * k}, 4 * k,
                                           {color.r, color.g, color.b, 255});
            fill->setAnchorPoint({0, 0.5f});
            add(fill, 80 * k, y);
        }
        auto value = makeText(fmt::format("{}%", percent), Weight::SemiBold, 15 * k);
        value->setAnchorPoint({0, 0.5f});
        add(value, 90 * k + barW, y);
        y -= 26 * k;
    };
    auto button = [&](char const* glyph, std::string const& label, ccColor4B color, std::function<void()> action) -> Button& {
        auto& b = addButton(m_wedgeButtons, content, glyph, label, {inset, y}, 30 * k, color, std::move(action), 0);
        b.clip = m_details;
        return b;
    };

    // Progress.
    section("progress");
    if (e.platformer) {
        // Platformers have no percentage: beaten or not, and the best time.
        auto best = infoRow({
            {e.normalPercent >= 100 ? icon::CHECK : icon::XMARK, e.normalPercent >= 100 ? "completed" : "not completed"},
            {icon::CLOCK, e.bestTime > 0 ? "best " + formatTime(e.bestTime) : "no best time"},
        }, 15 * k, theme::CONTENT1);
        add(best, 0, y);
        y -= 26 * k;
    } else {
        bar("normal", e.normalPercent, accent);
        bar("practice", e.practicePercent, {100, 200, 255});
    }

    // Counts in one line.
    std::vector<std::pair<char const*, std::string>> counts {
        {icon::ROTATE, fmt::format("{} attempts", level->m_attempts.value())},
        {icon::ARROW_UP, fmt::format("{} jumps", level->m_jumps.value())},
    };
    if (!e.official) {
        counts.push_back({icon::CLOUD_DOWN, fmt::format("{}", level->m_downloads)});
        counts.push_back({icon::THUMBS_UP, fmt::format("{}", level->m_likes)});
    }
    auto countRow = infoRow(counts, 15 * k, theme::CONTENT2);
    if (countRow->getContentSize().width > maxW) countRow->setScale(maxW / countRow->getContentSize().width);
    add(countRow, 0, y - 4 * k);
    y -= 34 * k;

    // Description.
    if (!e.official) {
        std::string desc = level->m_levelDesc;
        if (auto decoded = utils::base64::decodeString(desc)) desc = *decoded;
        if (!desc.empty()) {
            section("description");
            // The lines hang down from the node's origin.
            auto text = makeWrappedText(desc, 15 * k, maxW, theme::CONTENT2);
            add(text, 0, y + 9 * k);
            y -= text->getScaledContentSize().height + 16 * k;
        }
    }

    // Songs: GD's own song widget (the level page's) runs hidden and does the
    // work: downloads, a level's extra songs and SFX, and whatever other mods
    // add to it (Jukebox's song swapping). The card below mirrors it and
    // presses its buttons (see updateSongCard).
    section(level->m_songIDs.empty() ? "song" : "songs");
    SongInfoObject* info = nullptr;
    if (level->m_songID > 0) {
        info = MusicDownloadManager::sharedState()->getSongInfoObject(level->m_songID);
        if (!info) info = SongInfoObject::create(level->m_songID);
    } else {
        info = LevelTools::getSongObject(level->m_audioTrack);
    }
    m_songCard = {};
    if (auto widget = info ? CustomSongWidget::create(info, this, false, false, true, level->m_songID <= 0, false, false, 0) : nullptr) {
        widget->updateWithMultiAssets(level->m_songIDs, level->m_sfxIDs, 0);
        // In the scene (so it keeps its download timers) but never drawn or touched.
        widget->setVisible(false);
        m_wedge->addChild(widget);
        m_songWidget = widget;

        float cardH = 124 * k;
        auto card = RoundedBox::create({maxW + 8 * k, cardH}, 10 * k, {255, 255, 255, 14});
        card->setAnchorPoint({0, 1});
        add(card, -4 * k, y + 14 * k);
        float cx = 12 * k;
        float row = y;

        auto tile = RoundedBox::create({44 * k, 44 * k}, 10 * k, theme::COLOUR3);
        tile->setAnchorPoint({0, 0.5f});
        add(tile, cx, row - 16 * k);
        auto note = makeIcon(icon::MUSIC, 20 * k);
        add(note, cx + 22 * k, row - 16 * k);

        float tx = cx + 58 * k;
        float textW = maxW - tx - 4 * k;
        auto label = [&](Weight weight, float size, ccColor3B color, float atY) {
            auto l = makeText(" ", weight, size);
            l->setColor(color);
            l->setAnchorPoint({0, 0.5f});
            add(l, tx, atY);
            return l;
        };
        m_songCard.title = label(Weight::SemiBold, 18 * k, {255, 255, 255}, row - 2 * k);
        m_songCard.artist = label(Weight::Regular, 15 * k, theme::CONTENT2, row - 24 * k);
        m_songCard.info = label(Weight::Regular, 13 * k, theme::LIGHT1, row - 44 * k);
        m_songCard.textW = textW;

        // Download progress, over the info line while downloading.
        m_songCard.barW = textW;
        m_songCard.track = RoundedBox::create({textW, 6 * k}, 3 * k, {255, 255, 255, 30});
        m_songCard.track->setAnchorPoint({0, 0.5f});
        add(m_songCard.track, tx, row - 44 * k);
        m_songCard.fill = RoundedBox::create({6 * k, 6 * k}, 3 * k, theme::COLOUR3);
        m_songCard.fill->setAnchorPoint({0, 0.5f});
        add(m_songCard.fill, tx, row - 44 * k);

        // Buttons: one per widget button, shown while GD shows it.
        y = row - 84 * k;
        auto forward = [this](char const* glyph, std::string const& text, ccColor4B color, std::function<void()> press) {
            auto& b = addButton(m_wedgeButtons, m_details->content(), glyph, text, {0, 0}, 28 * m_k, color, std::move(press), 0);
            b.clip = m_details;
            m_songCard.buttons.push_back(m_wedgeButtons.size() - 1);
            return m_wedgeButtons.size() - 1;
        };
        Ref<CustomSongWidget> w = widget;
        m_songCard.download = forward(icon::CLOUD_DOWN, "download", theme::COLOUR3, [w] {
            if (w->m_downloadBtn) w->onDownload(w->m_downloadBtn);
        });
        m_songCard.cancel = forward(icon::XMARK, "cancel", TAB, [w] {
            if (w->m_cancelDownloadBtn) w->onCancelDownload(w->m_cancelDownloadBtn);
        });
        m_songCard.getInfo = forward(icon::ROTATE, "get info", TAB, [w] {
            if (w->m_getSongInfoBtn) w->onGetSongInfo(w->m_getSongInfoBtn);
        });
        m_songCard.jukebox = forward(icon::MUSIC, "switch song", TAB, [w] {
            if (auto disc = typeinfo_cast<CCMenuItem*>(w->querySelector("fleym.nongd/nong-button"))) disc->activate();
        });
        m_songCard.more = forward(icon::LIST, "all assets", TAB, [w] {
            if (w->m_moreBtn) w->onMore(w->m_moreBtn);
        });
        m_songCard.infoBtn = forward(icon::CIRCLE_INFO, "info", TAB, [w] {
            if (w->m_infoBtn) w->onInfo(w->m_infoBtn);
        });
        m_songCard.remove = forward(icon::TRASH, "delete", TAB, [w] {
            if (w->m_deleteBtn) w->onDelete(w->m_deleteBtn);
        });
        m_songCard.buttonY = y;
        m_songCard.buttonX = inset + cx;
        y = row - cardH - 4 * k;
        updateSongCard();
    }

    // Level options (GD's per-level settings from the level page).
    section("options");
    float x = 0;
    auto toggle = [&](char const* label, bool on, std::function<bool()> flip) {
        size_t index = m_wedgeButtons.size();
        auto& b = button(nullptr, label, TAB, [this, index, flip] {
            if (index >= m_wedgeButtons.size()) return;
            bool now = flip();
            m_wedgeButtons[index].selected = now;
            sfx::play(now ? sfx::sound::CHECK_ON : sfx::sound::CHECK_OFF);
        });
        b.selected = on;
        b.node->setPositionX(inset + x);
        x += b.node->getContentSize().width + 8 * k;
    };
    Ref<GJGameLevel> ref = level;
    if (level->m_lowDetailMode) {
        toggle("low detail mode", level->m_lowDetailModeToggled, [ref] {
            ref->m_lowDetailModeToggled = !ref->m_lowDetailModeToggled;
            return ref->m_lowDetailModeToggled;
        });
    }
    toggle("disable shake", level->m_disableShakeToggled, [ref] {
        ref->m_disableShakeToggled = !ref->m_disableShakeToggled;
        return ref->m_disableShakeToggled;
    });
    y -= 40 * k;

    // Leaderboard: loading it uploads your best to GD's servers, so only on request.
    if (!e.official) {
        section("leaderboard");
        bool here = m_boardLevel == e.id;
        if (!here || m_board == Board::Hidden) {
            button(icon::RANKING_STAR, "show leaderboard (syncs your progress)", TAB, [this] { this->loadLeaderboard(); });
            y -= 40 * k;
        } else if (m_board == Board::Loading) {
            auto label = makeText("loading...", Weight::Regular, 15 * k);
            label->setColor(theme::CONTENT2);
            label->setAnchorPoint({0, 0.5f});
            add(label, 0, y);
            y -= 30 * k;
        } else if (m_board == Board::Failed || !m_boardScores || m_boardScores->count() == 0) {
            auto label = makeText(m_board == Board::Failed ? "couldn't load the leaderboard" : "no scores yet", Weight::Regular, 15 * k);
            label->setColor(theme::CONTENT2);
            label->setAnchorPoint({0, 0.5f});
            add(label, 0, y);
            y -= 34 * k;
            button(icon::ROTATE, "try again", TAB, [this] { this->loadLeaderboard(); });
            y -= 40 * k;
        } else {
            int me = GJAccountManager::get()->m_accountID;
            int shown = 0;
            for (auto score : CCArrayExt<GJUserScore*>(m_boardScores)) {
                if (shown++ >= 15) break;
                bool mine = me > 0 && score->m_accountID == me;
                auto color = mine ? theme::rgb(theme::COLOUR3) : theme::CONTENT1;
                if (mine) {
                    auto hl = RoundedBox::create({maxW + 8 * k, 26 * k}, 6 * k, {255, 255, 255, 20});
                    hl->setAnchorPoint({0, 0.5f});
                    add(hl, -4 * k, y);
                }
                auto rank = makeText(fmt::format("#{}", score->m_playerRank), Weight::SemiBold, 15 * k);
                rank->setColor(color);
                rank->setAnchorPoint({0, 0.5f});
                add(rank, 0, y);
                auto name = makeText(score->m_userName, Weight::Regular, 15 * k);
                name->setColor(color);
                name->setAnchorPoint({0, 0.5f});
                fit(name, maxW - 190 * k);
                add(name, 50 * k, y);
                // Level scores reuse the profile fields: percent (or time) in stars, coins in coins.
                std::string value = e.platformer ? formatTime(score->m_stars) : fmt::format("{}%", score->m_stars);
                auto valueLabel = makeText(value, Weight::SemiBold, 15 * k);
                valueLabel->setColor(color);
                valueLabel->setAnchorPoint({1, 0.5f});
                add(valueLabel, maxW - 40 * k, y);
                if (score->m_secretCoins > 0) {
                    auto coins = infoRow({{icon::COINS, std::to_string(score->m_secretCoins)}}, 13 * k, theme::CONTENT2);
                    add(coins, maxW - 32 * k, y);
                }
                y -= 26 * k;
            }
            y -= 8 * k;
            button(icon::ROTATE, "refresh (syncs your progress)", TAB, [this] { this->loadLeaderboard(); });
            y -= 40 * k;
        }
    }

    m_details->setContentHeight(-y + 10 * k);
    updateSongCard();
}

void SongSelect::refreshDetails() {
    if (!m_hasSelection) return;
    // Don't swap the scroll area out from under a drag.
    if (m_touchDown) {
        m_refreshPending = true;
        return;
    }
    m_refreshPending = false;
    float scroll = m_details ? m_details->scroll() : 0;
    updateWedge(false);
    if (m_details) m_details->scrollTo(scroll, false);
}

void SongSelect::updateSongCard() {
    auto w = m_songWidget;
    auto& c = m_songCard;
    if (!w || !c.title) return;

    auto text = [](CCLabelBMFont* label) -> std::string {
        return label && label->isVisible() ? label->getString() : "";
    };
    auto shown = [](CCNode* node) {
        for (auto n = node; n; n = n->getParent()) {
            // The widget itself is always hidden: look at its own parts only.
            if (typeinfo_cast<CustomSongWidget*>(n)) return true;
            if (!n->isVisible()) return false;
        }
        return false;
    };

    // Texts: GD's (and Jukebox's, which renames the song) labels.
    std::string title = w->m_songLabel ? std::string(w->m_songLabel->getString()) : "";
    if (auto jb = typeinfo_cast<CCLabelBMFont*>(w->querySelector("fleym.nongd/song-name-label"))) title = jb->getString();
    if (title.empty() && w->m_songInfoObject) title = w->m_songInfoObject->m_songName;
    std::string artist = text(w->m_artistLabel);
    std::string error = text(w->m_errorLabel);
    std::string info = error.empty() ? text(w->m_songIDLabel) : error;
    bool downloading = w->m_sliderGroove && shown(w->m_sliderGroove);

    // New text: set it and shrink it to the card's width.
    auto set = [&](CCLabelBMFont* label, std::string const& value) {
        auto base = static_cast<CCFloat*>(label->getUserObject("base"_spr));
        if (!base) {
            base = CCFloat::create(label->getScale());
            label->setUserObject("base"_spr, base);
        }
        std::string shownText = value.empty() ? " " : value;
        if (label->getString() == shownText) return;
        label->setString(shownText.c_str());
        label->setScale(base->getValue());
        float width = label->getScaledContentSize().width;
        if (width > c.textW) label->setScale(base->getValue() * c.textW / width);
    };
    set(c.title, title);
    set(c.artist, artist);
    set(c.info, downloading ? "" : info);
    c.info->setColor(error.empty() ? theme::LIGHT1 : ccColor3B {255, 110, 110});

    // Download progress: GD grows its bar sprite's texture rect across the groove.
    c.track->setVisible(downloading);
    c.fill->setVisible(downloading);
    if (downloading && w->m_sliderBar) {
        float full = std::max(1.f, w->m_sliderGroove->getTextureRect().size.width - 4.f);
        float ratio = std::clamp(w->m_sliderBar->getTextureRect().size.width / full, 0.f, 1.f);
        float h = c.fill->getContentSize().height;
        c.fill->setContentSize({std::max(h, c.barW * ratio), h});
    }

    // Buttons, in GD's order, only while GD shows the matching one.
    auto visible = [&](size_t index) -> bool {
        if (index == c.download) return w->m_downloadBtn && shown(w->m_downloadBtn);
        if (index == c.cancel) return w->m_cancelDownloadBtn && shown(w->m_cancelDownloadBtn);
        if (index == c.getInfo) return w->m_getSongInfoBtn && shown(w->m_getSongInfoBtn);
        if (index == c.jukebox) {
            auto disc = w->querySelector("fleym.nongd/nong-button");
            return disc && shown(disc);
        }
        if (index == c.more) return w->m_moreBtn && shown(w->m_moreBtn);
        if (index == c.infoBtn) return w->m_infoBtn && shown(w->m_infoBtn);
        if (index == c.remove) return w->m_deleteBtn && shown(w->m_deleteBtn);
        return false;
    };
    float x = c.buttonX;
    for (size_t index : c.buttons) {
        if (index >= m_wedgeButtons.size()) continue;
        auto& b = m_wedgeButtons[index];
        bool on = visible(index);
        b.node->setVisible(on);
        if (!on) continue;
        b.node->setPosition({x, c.buttonY});
        x += b.node->getContentSize().width + 6 * m_k;
    }
}

int SongSelect::getActiveSongID() {
    if (!m_hasSelection) return 0;
    return m_entries[m_visible[m_selected]].level->m_songID;
}

void SongSelect::loadLeaderboard() {
    if (!m_hasSelection) return;
    auto const& e = m_entries[m_visible[m_selected]];
    if (e.official) return;
    m_board = Board::Loading;
    m_boardLevel = e.id;
    m_boardScores = nullptr;
    refreshDetails();
    auto glm = GameLevelManager::sharedState();
    glm->m_leaderboardManagerDelegate = this;
    glm->getLevelLeaderboard(e.level, LevelLeaderboardType::Global,
                             e.platformer ? LevelLeaderboardMode::Time : LevelLeaderboardMode::Time);
}

void SongSelect::loadLeaderboardFinished(CCArray* scores, char const*) {
    auto glm = GameLevelManager::sharedState();
    if (glm->m_leaderboardManagerDelegate == this) glm->m_leaderboardManagerDelegate = nullptr;
    if (m_board != Board::Loading) return;
    m_board = Board::Loaded;
    m_boardScores = scores;
    if (scores && scores->count() > 0) {
        auto s = static_cast<GJUserScore*>(scores->objectAtIndex(0));
        log::debug("Leaderboard[0]: rank {} stars {} moons {} coins {}/{} demons {} diamonds {} cp {}",
                   s->m_playerRank, s->m_stars, s->m_moons, s->m_secretCoins, s->m_userCoins, s->m_demons,
                   s->m_diamonds, s->m_creatorPoints);
    }
    if (m_hasSelection && m_entries[m_visible[m_selected]].id == m_boardLevel) refreshDetails();
}

void SongSelect::loadLeaderboardFailed(char const*) {
    auto glm = GameLevelManager::sharedState();
    if (glm->m_leaderboardManagerDelegate == this) glm->m_leaderboardManagerDelegate = nullptr;
    if (m_board != Board::Loading) return;
    m_board = Board::Failed;
    if (m_hasSelection && m_entries[m_visible[m_selected]].id == m_boardLevel) refreshDetails();
}

// --- carousel ---

SongSelect::Panel& SongSelect::makePanel(size_t visibleIndex) {
    float k = m_k;
    auto const& e = m_entries[m_visible[visibleIndex]];
    float pw = m_rightW + 60 * k, ph = m_panelH;
    auto accent = levels::difficultyColor(e.difficulty);

    auto root = CCNode::create();
    root->setContentSize({pw, ph});
    root->setAnchorPoint({0, 0.5f});
    m_carousel->addChild(root);

    auto bg = RoundedBox::create({pw, ph}, CORNER * k, PANEL_BG);
    bg->setAnchorPoint({0, 0});
    root->addChild(bg, 0);

    // Thumbnail on the right, fading into the panel (PanelSetBackground).
    auto thumb = RoundedBox::create({pw * 0.62f, ph}, CORNER * k, {255, 255, 255, 255});
    thumb->setCornerRadii(0, CORNER * k, 0, CORNER * k);
    thumb->setAnchorPoint({0, 0});
    thumb->setPosition({pw * 0.38f, 0});
    thumb->setOpacity(0);
    thumb->setVisible(false);
    root->addChild(thumb, 1);
    auto fade = CCLayerGradient::create({PANEL_BG.r, PANEL_BG.g, PANEL_BG.b, 255}, {PANEL_BG.r, PANEL_BG.g, PANEL_BG.b, 0}, {1, 0});
    fade->setContentSize({pw * 0.3f, ph});
    fade->setPosition({pw * 0.38f, 0});
    root->addChild(fade, 2);

    // Difficulty strip.
    auto strip = RoundedBox::create({STRIP_WIDTH * k, ph}, CORNER * k, {accent.r, accent.g, accent.b, 255});
    strip->setCornerRadii(CORNER * k, 0, CORNER * k, 0);
    strip->setAnchorPoint({0, 0});
    root->addChild(strip, 3);
    auto face = difficultyFace(e, ph * 0.56f);
    face->setPosition({STRIP_WIDTH * k / 2, ph * 0.6f});
    root->addChild(face, 4);
    if (e.stars > 0) {
        auto stars = infoRow({{rewardIcon(e), std::to_string(e.stars)}}, 12 * k, {255, 255, 255});
        stars->setPosition({(STRIP_WIDTH * k - stars->getContentSize().width + 10 * k) / 2, ph * 0.17f});
        root->addChild(stars, 4);
    }

    float x = STRIP_WIDTH * k + 14 * k;
    float maxW = pw * 0.62f - x;
    auto title = makeText(e.name, Weight::SemiBold, 22 * k);
    title->setAnchorPoint({0, 0.5f});
    title->setPosition({x, ph * 0.72f});
    fit(title, maxW);
    root->addChild(title, 4);

    auto creator = makeText("by " + e.creator, Weight::Regular, 14 * k);
    creator->setColor(theme::CONTENT2);
    creator->setAnchorPoint({0, 0.5f});
    creator->setPosition({x, ph * 0.46f});
    fit(creator, maxW);
    root->addChild(creator, 4);

    std::vector<std::pair<char const*, std::string>> info;
    if (!e.platformer) info.push_back({icon::CLOCK, levels::lengthName(e.length)});
    if (e.coins > 0) info.push_back({icon::COINS, fmt::format("{}/{}", e.coinsCollected, e.coins)});
    if (!e.platformer) info.push_back({e.normalPercent >= 100 ? icon::CHECK : nullptr, fmt::format("{}%", e.normalPercent)});
    else if (e.normalPercent >= 100) info.push_back({icon::CHECK, e.bestTime > 0 ? formatTime(e.bestTime) : "completed"});
    auto row = infoRow(info, 12 * k, theme::LIGHT1);
    row->setPosition({x, ph * 0.2f});
    root->addChild(row, 4);

    auto& panel = m_panels[visibleIndex];
    panel = Panel {m_visible[visibleIndex], root, bg, thumb};
    return panel;
}

void SongSelect::updateCarousel(float dt) {
    float ms = dt * 1000.f;
    float k = m_k;
    float viewH = viewHeight(), halfH = viewH / 2;
    float step = m_panelH + m_spacing;

    if (!m_visible.empty()) {
        float minScroll = m_panelH / 2 - halfH;
        float maxScroll = itemTop(m_visible.size() - 1) + m_panelH / 2 - halfH;
        if (!m_dragging) m_scrollTarget = std::clamp(m_scrollTarget, minScroll, maxScroll);
    }
    if (m_dragging) m_scroll = m_scrollTarget;
    else m_scroll = damp(m_scroll, m_scrollTarget, SCROLL_DECAY, ms);

    for (auto& [index, panel] : m_panels) panel.seen = false;
    if (!m_visible.empty()) {
        int first = std::max(0, static_cast<int>(std::floor((m_scroll - m_panelH) / step)));
        int last = std::min(static_cast<int>(m_visible.size()) - 1, static_cast<int>(std::ceil((m_scroll + viewH + m_panelH) / step)));
        auto mouse = geode::cocos::getMousePos();
        float colLeft = m_win.width - m_rightW;

        for (int i = first; i <= last; i++) {
            auto it = m_panels.find(i);
            Panel& p = it != m_panels.end() ? it->second : makePanel(i);
            p.seen = true;

            float centerFromTop = itemTop(i) - m_scroll + m_panelH / 2;
            float y = m_carouselTop - centerFromTop;
            // Carousel.offsetX: panels curve away towards the top and bottom.
            float dist = std::abs(1.f - centerFromTop / halfH);
            float offset = (3.f - std::sqrt(std::max(0.f, 9.f - dist * dist))) * halfH;

            bool selected = m_hasSelection && static_cast<size_t>(i) == m_selected;
            if (p.active.target() != (selected ? 1.f : 0.f)) p.active.to(selected ? 1.f : 0.f, 400, Easing::OutQuint);

            p.root->setPosition({colLeft + offset - p.active.get() * ACTIVE_X * k, y});
            bool hovered = !m_dragging && mouse.y > m_carouselBottom && mouse.y < m_carouselTop && containsWorld(p.root, mouse);
            if (hovered != p.hovered) {
                p.hovered = hovered;
                p.hover.to(hovered ? 1.f : 0.f, hovered ? 100 : 500, Easing::OutQuint);
                if (hovered) sfx::hover(sfx::sound::DEFAULT_HOVER);
            }
            p.active.update(dt);
            p.hover.update(dt);
            p.bg->setFillColor(theme::lerp(PANEL_BG, PANEL_HOVER, p.hover.get()));
            auto const& e = m_entries[p.entry];
            auto accent = levels::difficultyColor(e.difficulty);
            p.bg->setBorder(2.5f * k * p.active.get(), {accent.r, accent.g, accent.b, static_cast<GLubyte>(255 * p.active.get())});

            // Thumbnail, once the panel has settled in view.
            p.visibleMs += ms;
            if (!p.thumbRequested && p.visibleMs > THUMB_DELAY) {
                p.thumbRequested = true;
                Ref<RoundedBox> thumb = p.thumb;
                levelThumbnail(e, [thumb](CCTexture2D* texture) {
                    if (!texture || !thumb->getParent()) return;
                    thumb->setTexture(texture);
                    thumb->setVisible(true);
                    thumb->setUserObject("loaded"_spr, CCBool::create(true));
                });
            }
            if (p.thumb->getUserObject("loaded"_spr) && p.thumbAlpha.target() < 1.f) p.thumbAlpha.to(1.f, 300, Easing::OutQuint);
            p.thumbAlpha.update(dt);
            p.thumb->setOpacity(static_cast<GLubyte>(p.thumbAlpha.get() * 150));
        }
    }
    for (auto it = m_panels.begin(); it != m_panels.end();) {
        if (!it->second.seen) {
            it->second.root->removeFromParent();
            it = m_panels.erase(it);
        } else {
            ++it;
        }
    }
}

void SongSelect::update(float dt) {
    float ms = dt * 1000.f;
    m_enterMs += ms;
    // GD's mouse dispatcher only feeds its newest delegate, and GD's song widget
    // (or mods extending it) can register one: take the wheel back now and then.
    m_wheelClaimMs += ms;
    if (m_wheelClaimMs > 500) {
        m_wheelClaimMs = 0;
        auto dispatcher = CCDirector::get()->getMouseDispatcher();
        dispatcher->removeDelegate(this);
        dispatcher->addDelegate(this);
    }
    updateCarousel(dt);

    m_wedgeAlpha.update(dt);
    m_wedge->setPositionX(-24 * m_k * (1.f - m_wedgeAlpha.get()));

    if (m_previewDelay >= 0) {
        m_previewDelay -= ms;
        if (m_previewDelay < 0) previewSong();
    }

    auto mouse = geode::cocos::getMousePos();
    auto updateButton = [&](Button& b) {
        bool hovered = hittable(b, mouse);
        if (hovered != b.hovered) {
            b.hovered = hovered;
            b.hover.to(hovered ? 1.f : 0.f, hovered ? 100 : 400, Easing::OutQuint);
            if (hovered) sfx::hover(sfx::sound::BUTTON_HOVER);
        }
        b.hover.update(dt);
        auto base = b.selected ? theme::COLOUR3 : b.color;
        b.bg->setFillColor(theme::lerp(base, {255, 255, 255, 255}, b.hover.get() * 0.15f));
    };
    for (auto& b : m_tabs) updateButton(b);
    for (auto& b : m_buttons) updateButton(b);
    for (auto& b : m_wedgeButtons) updateButton(b);
    for (auto& b : m_folderItems) updateButton(b);
    updateSongCard();
}

// --- input ---

size_t SongSelect::panelAt(CCPoint world) {
    if (world.y < m_carouselBottom || world.y > m_carouselTop) return SIZE_MAX;
    for (auto& [index, panel] : m_panels) {
        if (containsWorld(panel.root, world)) return index;
    }
    return SIZE_MAX;
}

bool SongSelect::hittable(Button const& b, CCPoint world) {
    for (CCNode* n = b.node; n; n = n->getParent()) {
        if (!n->isVisible()) return false;
    }
    if (b.clip && !b.clip->containsWorldPoint(world)) return false;
    return containsWorld(b.node, world);
}

SongSelect::Button* SongSelect::buttonAt(CCPoint world) {
    // The open dropdown sits on top of everything.
    for (auto& b : m_folderItems) {
        if (hittable(b, world)) return &b;
    }
    for (auto list : {&m_tabs, &m_buttons, &m_wedgeButtons}) {
        for (auto& b : *list) {
            if (hittable(b, world)) return &b;
        }
    }
    return nullptr;
}

bool SongSelect::ccTouchBegan(CCTouch* touch, CCEvent*) {
    auto loc = touch->getLocation();
    // Let the search field take its own touches.
    if (m_search && containsWorld(m_search, loc)) return false;
    if (m_starting) return true;
    m_touchDown = true;
    m_dragging = false;
    m_touchStart = m_touchLast = loc;
    m_pressed = buttonAt(loc);
    // A tap outside the open folder list closes it.
    if (m_folderMenu) {
        bool inMenu = m_pressed && (m_pressed == &m_buttons[m_folderButton]
            || (m_pressed >= m_folderItems.data() && m_pressed < m_folderItems.data() + m_folderItems.size()));
        if (!inMenu) {
            closeFolders();
            m_pressed = nullptr;
            m_touchDown = false;
            return true;
        }
    }
    m_detailsDrag.began(m_details, loc);
    return true;
}

void SongSelect::ccTouchMoved(CCTouch* touch, CCEvent*) {
    auto loc = touch->getLocation();
    if (!m_touchDown) return;
    // Dragging the details scrolls them, and cancels a press on their buttons.
    if (m_detailsDrag.moved(loc)) {
        m_pressed = nullptr;
        m_touchLast = loc;
        return;
    }
    bool inCarousel = m_touchStart.x > m_win.width - m_rightW && m_touchStart.y > m_carouselBottom && m_touchStart.y < m_carouselTop;
    if (!m_dragging && inCarousel && !m_pressed && std::abs(loc.y - m_touchStart.y) > 8 * m_k) m_dragging = true;
    if (m_dragging) m_scrollTarget += loc.y - m_touchLast.y;
    m_touchLast = loc;
}

void SongSelect::ccTouchEnded(CCTouch* touch, CCEvent*) {
    auto loc = touch->getLocation();
    bool wasDragging = m_dragging;
    bool wasDown = m_touchDown;
    m_touchDown = false;
    m_dragging = false;
    bool scrolledDetails = m_detailsDrag.ended();
    if (m_refreshPending) refreshDetails();
    if (!wasDown || wasDragging || scrolledDetails) {
        m_pressed = nullptr;
        return;
    }

    if (m_pressed) {
        if (buttonAt(loc) == m_pressed) {
            sfx::click(sfx::sound::BUTTON_SELECT);
            auto action = m_pressed->action;
            m_pressed = nullptr;
            if (action) action();
        }
        m_pressed = nullptr;
        return;
    }

    size_t index = panelAt(loc);
    if (index == SIZE_MAX) return;
    // Clicking the selected level plays it, like osu!.
    if (m_hasSelection && index == m_selected) {
        start();
        return;
    }
    sfx::play(sfx::sound::DEFAULT_SELECT);
    select(index);
}

void SongSelect::scrollWheel(float y, float) {
    // Left side: the level details; right side: the carousel.
    if (geode::cocos::getMousePos().x < m_win.width - m_rightW) {
        if (m_details) m_details->scrollWheel(y, 0);
        return;
    }
    // Positive = down; one notch moves about one and a half panels.
    float notches = std::clamp(y / 12.f, -3.f, 3.f);
    m_scrollTarget += notches * (m_panelH + m_spacing) * 1.5f;
}

void SongSelect::keyDown(enumKeyCodes key, double) {
    if (!m_hasSelection) return;
    switch (key) {
        case KEY_Up:
            if (m_selected > 0) {
                sfx::play(sfx::sound::DEFAULT_HOVER);
                select(m_selected - 1);
            }
            break;
        case KEY_Down:
            if (m_selected + 1 < m_visible.size()) {
                sfx::play(sfx::sound::DEFAULT_HOVER);
                select(m_selected + 1);
            }
            break;
        case KEY_Enter:
            start();
            break;
        case KEY_F2:
            selectRandom();
            break;
        default:
            break;
    }
}

void SongSelect::keyBackClicked() {
    back();
}

} // namespace lazer

// Leaving gameplay or GD's level page returns to song select when that's where
// the player came from.
class $modify(SongSelectReturn, GameManager) {
    void returnToLastScene(GJGameLevel* level) {
        if (lazer::SongSelect::returnsHere() && Mod::get()->getSettingValue<bool>("enabled")) {
            CCDirector::get()->replaceScene(CCTransitionFade::create(0.5f, lazer::SongSelect::scene()));
            return;
        }
        GameManager::returnToLastScene(level);
    }
};

class $modify(SongSelectLevelPage, LevelInfoLayer) {
    void onBack(CCObject* sender) {
        if (lazer::SongSelect::returnsHere() && Mod::get()->getSettingValue<bool>("enabled")) {
            CCDirector::get()->replaceScene(CCTransitionFade::create(0.5f, lazer::SongSelect::scene()));
            return;
        }
        LevelInfoLayer::onBack(sender);
    }

#ifndef GEODE_IS_ANDROID
    // On Android, keyBackClicked is just onBack(nullptr), which is hooked above;
    // hooking a function that small spills the patch into the next one.
    void keyBackClicked() {
        if (lazer::SongSelect::returnsHere() && Mod::get()->getSettingValue<bool>("enabled")) {
            this->onBack(nullptr);
            return;
        }
        LevelInfoLayer::keyBackClicked();
    }
#endif
};
