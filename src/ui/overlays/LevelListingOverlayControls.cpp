#include "LevelListingInternal.hpp"

#include <algorithm>
#include <array>

using namespace geode::prelude;

namespace lazer {

using namespace levellisting;

namespace {
    // A filter row (osu!'s BeatmapSearchFilterRow): its label and options.
    struct RowDef {
        std::string label;
        std::vector<std::string> options;
    };
}

// --- building ---

LevelListingOverlay::Pill& LevelListingOverlay::addPill(std::vector<Pill>& list, CCNode* parent, CCSize size, float radius,
                                                        CCPoint pos, CCPoint anchor, ccColor4B color, ccColor4B hoverColor,
                                                        std::function<void()> action) {
    auto node = CCNode::create();
    node->setContentSize(size);
    node->setAnchorPoint(anchor);
    node->setPosition(pos);
    parent->addChild(node, 1);
    auto bg = RoundedBox::create(size, radius, color);
    bg->setPosition({size.width / 2, size.height / 2});
    node->addChild(bg);

    Pill pill;
    pill.node = node;
    pill.bg = bg;
    pill.color = color;
    pill.hoverColor = hoverColor;
    pill.action = std::move(action);
    list.push_back(std::move(pill));
    m_pressed = nullptr; // the list may have moved
    return list.back();
}

LevelListingOverlay::Pill& LevelListingOverlay::addTab(std::vector<Pill>& list, CCNode* parent, char const* glyph,
                                                       std::string const& text, float height, CCPoint pos, CCPoint anchor,
                                                       std::function<void()> action) {
    float k = m_k;
    auto label = makeText(text, Weight::SemiBold, 12 * k);
    CCLabelBMFont* icon = glyph ? makeIcon(glyph, 11 * k) : nullptr;
    float iconW = icon ? icon->getScaledContentSize().width + 6 * k : 0;
    float w = 2 * TAB_PAD * k + iconW + label->getScaledContentSize().width;
    auto& tab = addPill(list, parent, {w, height}, height / 2, pos, anchor, CLEAR, m_scheme.background3(), std::move(action));
    tab.kind = Pill::Kind::Tab;
    tab.activeColor = m_scheme.colour3();
    tab.textColor = theme::rgb(m_scheme.light2());
    tab.textHover = theme::rgb(m_scheme.content1());
    tab.textActive = {255, 255, 255};
    float x = TAB_PAD * k;
    if (icon) {
        icon->setAnchorPoint({0, 0.5f});
        icon->setPosition({x, height / 2});
        tab.node->addChild(icon, 1);
        tab.tinted.push_back(icon);
        x += iconW;
    }
    label->setAnchorPoint({0, 0.5f});
    label->setPosition({x, height / 2});
    tab.node->addChild(label, 1);
    tab.tinted.push_back(label);
    return tab;
}

void LevelListingOverlay::buildSearchControl() {
    float k = m_k, W = bodySize().width;
    auto content = m_scroll->content();

    // Its background (Dark6), sized by relayout.
    m_controlBg = CCLayerColor::create(m_scheme.dark6());
    content->addChild(m_controlBg, -1);

    // The search box's row, placed by relayout.
    m_searchRow = CCNode::create();
    content->addChild(m_searchRow, 2);
    float boxH = TEXTBOX_HEIGHT * k, cy = -boxH / 2;
    float right = W - m_pad;

    // The search box (BasicSearchTextBox): a rounded field, the magnifier at
    // the left, a cross to clear it at the right.
    float boxX = m_pad, boxW = right - m_pad;
    auto searchBox = RoundedBox::create({boxW, boxH}, TEXTBOX_RADIUS * k, m_scheme.background5());
    searchBox->setAnchorPoint({0, 0.5f});
    searchBox->setPosition({boxX, cy});
    m_searchRow->addChild(searchBox);
    auto magnifier = makeIcon(icon::SEARCH, SEARCH_ICON * k);
    magnifier->setColor(theme::rgb(m_scheme.content2()));
    magnifier->setAnchorPoint({0, 0.5f});
    magnifier->setPosition({boxX + TEXTBOX_SIDE * k, cy});
    m_searchRow->addChild(magnifier, 1);
    float textX = boxX + TEXTBOX_SIDE * k + magnifier->getScaledContentSize().width + 10 * k;

    // GD's text input (Geode's box is 30 tall: scaled so its text suits the page).
    float scale = k;
    float inputW = boxW - (textX - boxX) - 44 * k; // room for the cross
    char const* placeholder = m_lists ? "search your lists..." : "search your levels...";
    m_input = TextInput::create(inputW / scale, placeholder, "outfit-regular.fnt"_spr);
    // GD's own character filter drops punctuation: allow everything typeable.
    m_input->setCommonFilter(CommonFilter::Any);
    m_input->hideBG();
    m_input->setTextAlign(TextInputAlign::Left);
    m_input->setScale(scale);
    m_input->setAnchorPoint({0, 0.5f});
    m_input->setPosition({textX, cy});
    m_input->setMaxCharCount(QUERY_LIMIT);
    if (!m_query.empty()) m_input->setString(m_query);
    m_input->setDelegate(this);
    m_searchRow->addChild(m_input, 2);

    float cross = 26 * k;
    auto& clear = addPill(m_fixedPills, m_searchRow, {cross, cross}, cross / 2, {boxX + boxW - 9 * k, cy}, {1, 0.5f},
                          CLEAR, m_scheme.background3(), [this] {
        if (!m_input) return;
        m_input->setString("");
        m_query.clear();
        m_input->defocus();
        startSearch();
    });
    auto crossIcon = makeIcon(icon::XMARK, 13 * k);
    anchorOnGlyph(crossIcon);
    crossIcon->setPosition({cross / 2, cross / 2});
    clear.node->addChild(crossIcon, 1);
    clear.tinted = {crossIcon};
    clear.textColor = theme::rgb(m_scheme.foreground1());
    clear.textHover = theme::rgb(m_scheme.content1());
    clear.node->setVisible(!m_query.empty());
    m_clearPill = m_fixedPills.size() - 1;

    // The filter rows: your folders (when you use any) and whether to show
    // the levels on this device or your uploads.
    m_rowsHolder = CCNode::create();
    content->addChild(m_rowsHolder, 1);
    if (m_folders.size() > 1) buildFilterRow(Row::Folder);
    if (!m_lists && GameManager::get()->m_playerUserID.value() > 0) buildFilterRow(Row::Source);
    layoutRows();
}

void LevelListingOverlay::buildFilterRow(Row row) {
    float k = m_k, W = bodySize().width;
    RowDef def = row == Row::Folder ? RowDef {"Folder", m_folderNames} : RowDef {"Show", {"on this device", "uploaded"}};
    size_t r = static_cast<size_t>(row);

    // The row's label and chips, stacked by layoutRows.
    auto node = CCNode::create();
    m_rowsHolder->addChild(node);
    m_rowNodes[r] = node;
    float h = CHIP_HEIGHT * k;
    auto label = makeText(def.label, Weight::SemiBold, 12 * k);
    label->setColor(theme::rgb(m_scheme.light1()));
    label->setAnchorPoint({0, 0.5f});
    label->setPosition({m_pad, -h / 2});
    node->addChild(label);

    // FilterTabItem as chips: filled in the page's colour while chosen, and
    // flowing onto more lines when the row runs out of room.
    float x0 = m_pad + ROW_LABEL_WIDTH * k, avail = W - m_pad - x0;
    float lineX = 0;
    int line = 0;
    for (size_t i = 0; i < def.options.size(); i++) {
        auto text = makeText(def.options[i], Weight::SemiBold, 13 * k);
        float w = text->getScaledContentSize().width + 2 * CHIP_PAD * k;
        if (lineX > 0 && lineX + w > avail) {
            lineX = 0;
            line++;
        }
        float y = -(line * (h + LINE_SPACING * k) + h / 2);
        auto& chip = addPill(m_fixedPills, node, {w, h}, h / 2, {x0 + lineX, y}, {0, 0.5f},
                             m_scheme.background4(), m_scheme.background3(),
                             [this, row, i] { this->toggleOption(row, static_cast<int>(i)); });
        chip.kind = Pill::Kind::Chip;
        chip.activeColor = m_scheme.colour3();
        chip.textColor = theme::rgb(m_scheme.light2());
        chip.textHover = theme::rgb(m_scheme.content1());
        chip.textActive = {255, 255, 255};
        chip.active = [this, row, i] { return this->optionActive(row, static_cast<int>(i)); };
        text->setPosition({w / 2, h / 2});
        chip.node->addChild(text, 1);
        chip.tinted = {text};
        lineX += w + CHIP_SPACING * k;
    }
    m_rowHeights[r] = (line + 1) * h + line * LINE_SPACING * k;
}

void LevelListingOverlay::layoutRows() {
    if (!m_rowsHolder) return;
    float k = m_k, y = 0;
    for (size_t r = 0; r < m_rowNodes.size(); r++) {
        auto node = m_rowNodes[r];
        if (!node) continue;
        bool shown = rowShown(static_cast<Row>(r));
        node->setVisible(shown);
        if (!shown) continue;
        if (y > 0) y += ROW_SPACING * k;
        node->setPosition({0, -y});
        y += m_rowHeights[r];
    }
    m_rowsHeight = y;
}

bool LevelListingOverlay::rowShown(Row row) const {
    // Your uploads aren't in your folders.
    if (row == Row::Folder) return !m_online;
    return true;
}

void LevelListingOverlay::buildStrip() {
    float k = m_k, W = bodySize().width, h = STRIP_HEIGHT * k;
    auto content = m_scroll->content();
    m_stripHolder = CCNode::create();
    content->addChild(m_stripHolder, 2);
    auto bg = CCLayerColor::create(m_scheme.background4());
    bg->setContentSize({W, h});
    bg->setPosition({0, -h});
    m_stripHolder->addChild(bg);
    float cy = -h / 2, tabH = TAB_HEIGHT * k;

    // GD's "new" button, as a filled button at the left (osu!'s RoundedButton
    // in the page's colour).
    auto plusIcon = makeIcon(icon::PLUS, 11 * k);
    auto newLabel = makeText(m_lists ? "new list" : "new level", Weight::SemiBold, 12 * k);
    float iconW = plusIcon->getScaledContentSize().width;
    float bh = tabH + 4 * k;
    float w = iconW + 6 * k + newLabel->getScaledContentSize().width + 2 * TAB_PAD * k;
    auto& button = addPill(m_fixedPills, m_stripHolder, {w, bh}, bh / 2, {STRIP_MARGIN * k, cy}, {0, 0.5f}, m_scheme.colour3(),
                           theme::lerp(m_scheme.colour3(), m_scheme.highlight1(), 0.5f), [this] { this->createNew(); });
    plusIcon->setAnchorPoint({0, 0.5f});
    plusIcon->setPosition({TAB_PAD * k, bh / 2});
    button.node->addChild(plusIcon, 1);
    newLabel->setAnchorPoint({0, 0.5f});
    newLabel->setPosition({TAB_PAD * k + iconW + 6 * k, bh / 2});
    button.node->addChild(newLabel, 1);
    m_stripNextX = STRIP_MARGIN * k + w + 10 * k;

    // At the right: refresh (GD's lists have one) and how many there are.
    float right = W - STRIP_MARGIN * k;
    auto& refresh = addTab(m_fixedPills, m_stripHolder, icon::ROTATE, "refresh", tabH, {right, cy}, {1, 0.5f},
                           [this] { this->refresh(); });
    right -= refresh.node->getContentSize().width + 10 * k;
    m_countLabel = makeText("", Weight::SemiBold, 12 * k);
    m_countLabel->setColor(theme::rgb(m_scheme.content2()));
    m_countLabel->setAnchorPoint({1, 0.5f});
    m_countLabel->setPosition({right, cy});
    m_stripHolder->addChild(m_countLabel, 1);

    // The loading bar along the strip's bottom edge: a sweep while a page is on its way.
    float ph = PROGRESS_HEIGHT * k;
    m_progressTrack = RoundedBox::create({W, ph}, 0, m_scheme.background5());
    m_progressTrack->setAnchorPoint({0, 0});
    m_progressTrack->setPosition({0, -h});
    m_progressTrack->setVisible(false);
    m_stripHolder->addChild(m_progressTrack, 3);
    m_progressBar = RoundedBox::create({W * 0.25f, ph}, ph / 2, m_scheme.highlight1());
    m_progressBar->setAnchorPoint({0, 0});
    m_progressBar->setPosition({0, 0});
    m_progressTrack->addChild(m_progressBar);
}

// Other mods put their buttons in GD's "new level" menu (GDShare's import):
// the browser sits hidden under this page, so they go on the strip after
// "new", and press the hidden button. Known ones get an icon and a name; the
// rest the words of their ID.
void LevelListingOverlay::addModButtons() {
    if (!m_owner || !m_stripHolder) return;
    auto menu = m_owner->getChildByID("new-level-menu");
    if (!menu) return;
    constexpr std::array VANILLA = {"new-level-button", "new-list-button", "my-levels-button", "switch-mode-button"};
    float k = m_k, tabH = TAB_HEIGHT * k, cy = -STRIP_HEIGHT * k / 2;
    for (auto child : CCArrayExt<CCNode*>(menu->getChildren())) {
        auto item = typeinfo_cast<CCMenuItem*>(child);
        if (!item) continue;
        std::string id = item->getID();
        if (id.empty() || std::find(VANILLA.begin(), VANILLA.end(), id) != VANILLA.end()) continue;
        char const* glyph = nullptr;
        std::string text;
        if (id == "hjfod.gdshare/import-level-button") {
            glyph = icon::FILE_IMPORT;
            text = "import";
        } else {
            text = id.substr(id.find('/') + 1);
            if (text.ends_with("-button")) text.resize(text.size() - 7);
            std::replace(text.begin(), text.end(), '-', ' ');
            std::replace(text.begin(), text.end(), '_', ' ');
        }
        Ref<CCMenuItem> keep = item;
        auto& tab = addTab(m_fixedPills, m_stripHolder, glyph, text, tabH, {m_stripNextX, cy}, {0, 0.5f}, [this, keep] {
            if (m_leaving) return;
            if (m_input) m_input->defocus();
            keep->activate();
        });
        m_stripNextX += tab.node->getContentSize().width + 10 * k;
    }
}

// --- filter rows ---

bool LevelListingOverlay::optionActive(Row row, int option) const {
    switch (row) {
        case Row::Folder: return option >= 0 && static_cast<size_t>(option) < m_folders.size() && m_folders[static_cast<size_t>(option)] == m_folder;
        case Row::Source: return (option == 1) == m_online;
        default: return false;
    }
}

void LevelListingOverlay::toggleOption(Row row, int option) {
    bool rowsChange = false;
    switch (row) {
        case Row::Folder:
            if (option < 0 || static_cast<size_t>(option) >= m_folders.size()) return;
            if (m_folder == m_folders[static_cast<size_t>(option)]) return;
            m_folder = m_folders[static_cast<size_t>(option)];
            break;
        case Row::Source:
            if (m_online == (option == 1)) return;
            m_online = option == 1;
            rowsChange = true;
            // GD's browser goes back to "my levels" too, like its own online button's back.
            if (!m_online && m_mineSearch) m_owner->loadPage(m_mineSearch);
            break;
        default:
            return;
    }
    if (rowsChange) {
        layoutRows();
        relayout();
    }
    queueSearch(FILTER_DEBOUNCE);
}

} // namespace lazer
