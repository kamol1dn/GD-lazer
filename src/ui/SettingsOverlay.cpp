#include "SettingsOverlay.hpp"

#include "Text.hpp"
#include "Theme.hpp"

#include <algorithm>
#include <cctype>

using namespace geode::prelude;

namespace lazer {

namespace {
    // SettingsPanel.cs / SettingsSidebar.cs, osu! px.
    constexpr float TRANSITION = 600.f;
    constexpr float PANEL_WIDTH = 400.f;
    constexpr float SIDEBAR_WIDTH = 60.f;
    constexpr float SIDEBAR_EXPANDED_WIDTH = 170.f;
    constexpr float CONTENT_MARGINS = 20.f;
    constexpr float DIM_ALPHA = 0.45f;
    constexpr float TOOLTIP_DELAY = 0.45f;

    GLubyte toByte(float a) { return static_cast<GLubyte>(std::clamp(a, 0.f, 1.f) * 255.f); }

    std::string lower(std::string s) {
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
        return s;
    }

    bool nodeContains(CCNode* node, CCPoint world) {
        auto local = node->convertToNodeSpace(world);
        auto size = node->getContentSize();
        return local.x >= 0 && local.y >= 0 && local.x <= size.width && local.y <= size.height;
    }

    // Greedy word wrap using the label's own measurements.
    CCNode* makeWrappedText(std::string const& text, float size, float maxWidth, ccColor3B color) {
        auto holder = CCNode::create();
        std::vector<std::string> lines;
        std::string line, word;
        auto measure = [&](std::string const& s) {
            auto l = makeText(s, Weight::Regular, size);
            return l->getScaledContentSize().width;
        };
        auto flushWord = [&] {
            if (word.empty()) return;
            std::string candidate = line.empty() ? word : line + " " + word;
            if (!line.empty() && measure(candidate) > maxWidth) {
                lines.push_back(line);
                line = word;
            } else {
                line = candidate;
            }
            word.clear();
        };
        for (char c : text) {
            if (c == ' ' || c == '\n') {
                flushWord();
                if (c == '\n') { lines.push_back(line); line.clear(); }
            } else {
                word += c;
            }
        }
        flushWord();
        if (!line.empty()) lines.push_back(line);

        float lineHeight = size * 1.15f;
        float width = 0;
        for (size_t i = 0; i < lines.size(); i++) {
            auto l = makeText(lines[i], Weight::Regular, size);
            l->setColor(color);
            l->setAnchorPoint({0, 1});
            l->setPosition({0, -lineHeight * i});
            holder->addChild(l);
            width = std::max(width, l->getScaledContentSize().width);
        }
        holder->setContentSize({width, lineHeight * lines.size()});
        return holder;
    }

    // GD descriptions use colour tags like <cy>...</c>; strip them for plain text.
    std::string stripTags(std::string s) {
        std::string out;
        for (size_t i = 0; i < s.size(); i++) {
            if (s[i] == '<') {
                auto end = s.find('>', i);
                if (end != std::string::npos && end - i <= 4) { i = end; continue; }
            }
            out += s[i];
        }
        return out;
    }
}

SettingsOverlay* SettingsOverlay::create(float topInset) {
    auto ret = new SettingsOverlay();
    if (ret->init(topInset)) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

bool SettingsOverlay::init(float topInset) {
    if (!CCNode::init()) return false;
    auto win = CCDirector::sharedDirector()->getWinSize();
    this->setContentSize(win);

    m_k = win.height / 768.f;
    m_topInset = topInset;
    m_sidebarWidth = SIDEBAR_WIDTH * m_k;
    m_sidebarExpandedWidth = SIDEBAR_EXPANDED_WIDTH * m_k;
    m_panelWidth = PANEL_WIDTH * m_k;
    m_margin = CONTENT_MARGINS * m_k;
    m_rowWidth = m_panelWidth - m_margin * 2;
    float height = win.height - topInset;

    m_dim = CCLayerColor::create({0, 0, 0, 0});
    m_dim->setContentSize(win);
    this->addChild(m_dim, 0);

    m_root = CCNode::create();
    this->addChild(m_root, 1);

    // Panel background (visible between sections as the separator colour).
    auto panelBg = CCLayerColor::create(theme::BACKGROUND6);
    panelBg->setContentSize({m_panelWidth, height});
    panelBg->setPosition({m_sidebarWidth, 0});
    m_root->addChild(panelBg, 0);

    // --- sticky header: title, subtitle, search (SettingsHeader.cs) ---
    auto header = CCNode::create();
    m_root->addChild(header, 2);
    float y = height - 22 * m_k;

    auto title = makeText("settings", Weight::Regular, 40 * m_k);
    title->setAnchorPoint({0, 1});
    title->setPosition({m_sidebarWidth + m_margin, y});
    header->addChild(title, 1);
    y -= 44 * m_k;

    auto subtitle = makeText("change the way Geometry Dash behaves", Weight::Regular, 18 * m_k);
    subtitle->setColor(theme::CONTENT2);
    subtitle->setAnchorPoint({0, 1});
    subtitle->setPosition({m_sidebarWidth + m_margin, y});
    header->addChild(subtitle, 1);
    y -= 32 * m_k;

    float searchH = 38 * m_k;
    auto searchBg = RoundedBox::create({m_rowWidth, searchH}, 6 * m_k, theme::BACKGROUND6);
    searchBg->setAnchorPoint({0, 1});
    searchBg->setPosition({m_sidebarWidth + m_margin, y});
    header->addChild(searchBg, 1);

    auto searchIcon = makeIcon(icon::SEARCH, 14 * m_k);
    searchIcon->setColor(theme::FOREGROUND1);
    searchIcon->setPosition({m_sidebarWidth + m_margin + m_rowWidth - 16 * m_k, y - searchH / 2});
    header->addChild(searchIcon, 2);

    // Geode's text input, with our font and without its GD-styled background.
    constexpr float inputScale = 0.42f;
    m_search = TextInput::create((m_rowWidth - 40 * m_k) / inputScale, "type to search", "outfit-regular.fnt"_spr);
    m_search->hideBG();
    m_search->setTextAlign(TextInputAlign::Left);
    m_search->setScale(inputScale);
    m_search->setAnchorPoint({0, 0.5f});
    m_search->setPosition({m_sidebarWidth + m_margin + 10 * m_k, y - searchH / 2});
    m_search->setCallback([this](std::string const& text) { this->applyFilter(text); });
    header->addChild(m_search, 2);

    y -= searchH + 14 * m_k;
    m_headerHeight = height - y;

    auto headerBg = CCLayerColor::create(theme::BACKGROUND4);
    headerBg->setContentSize({m_panelWidth, m_headerHeight});
    headerBg->setPosition({m_sidebarWidth, y});
    header->addChild(headerBg, 0);

    // --- scrolling sections ---
    m_scroll = ScrollArea::create({m_panelWidth, height - m_headerHeight});
    m_scroll->setPosition({m_sidebarWidth, 0});
    m_root->addChild(m_scroll, 1);

    // --- sidebar (SettingsSidebar.cs), drawn over the panel when expanded ---
    m_sidebar = CCNode::create();
    m_sidebar->setContentSize({m_sidebarWidth, height});
    m_root->addChild(m_sidebar, 3);

    m_sidebarBg = RoundedBox::create({m_sidebarWidth, height}, 0, theme::BACKGROUND5);
    m_sidebarBg->setAnchorPoint({0, 0});
    m_sidebarBg->setShadow(0, {0, 0, 0, 0});
    m_sidebar->addChild(m_sidebarBg, 0);

    m_sidebarSelection = RoundedBox::create({4 * m_k, 60 * m_k}, 2 * m_k, theme::HIGHLIGHT1);
    m_sidebarSelection->setAnchorPoint({0, 0.5f});
    m_sidebar->addChild(m_sidebarSelection, 2);

    m_tooltip = CCNode::create();
    m_tooltip->setVisible(false);
    this->addChild(m_tooltip, 10);

    this->setVisible(false);
    this->scheduleUpdate();
    return true;
}

void SettingsOverlay::onEnter() {
    CCNode::onEnter();
    CCDirector::sharedDirector()->getTouchDispatcher()->addTargetedDelegate(this, -140, true);
}

void SettingsOverlay::onExit() {
    CCDirector::sharedDirector()->getTouchDispatcher()->removeDelegate(this);
    g_overlayOpen = false;
    CCNode::onExit();
}

// ---------------------------------------------------------------------------
// Building

void SettingsOverlay::beginSection(std::string const& name, char const* icon) {
    Section s;
    s.name = name;
    s.icon = icon;

    s.background = RoundedBox::create({m_panelWidth, 1}, 0, theme::BACKGROUND5);
    s.background->setAnchorPoint({0, 1});
    m_scroll->content()->addChild(s.background, -1);

    auto header = SectionHeaderRow::create(name, m_rowWidth, m_k);
    m_scroll->content()->addChild(header, 1);
    s.rows.push_back(header);

    // Sidebar button: icon, plus a label revealed when the sidebar expands.
    float size = SIDEBAR_WIDTH * m_k;
    auto button = CCNode::create();
    button->setContentSize({m_sidebarExpandedWidth, size});
    s.sidebarIcon = makeIcon(icon, 20 * m_k);
    s.sidebarIcon->setPosition({size / 2, size / 2});
    button->addChild(s.sidebarIcon);
    s.sidebarLabel = makeText(name, Weight::SemiBold, 17 * m_k);
    s.sidebarLabel->setAnchorPoint({0, 0.5f});
    s.sidebarLabel->setPosition({size, size / 2});
    s.sidebarLabel->setOpacity(0);
    button->addChild(s.sidebarLabel);
    m_sidebar->addChild(button, 1);
    s.sidebarButton = button;

    m_sections.push_back(std::move(s));
}

void SettingsOverlay::addSubsection(std::string const& title) {
    addRow(SubsectionHeaderRow::create(title, m_rowWidth, m_k));
}

void SettingsOverlay::addRow(SettingsRow* row) {
    if (!row || m_sections.empty()) return;
    m_scroll->content()->addChild(row, 1);
    m_sections.back().rows.push_back(row);
}

void SettingsOverlay::finish() {
    // Sidebar buttons, top-down.
    float height = m_sidebar->getContentSize().height;
    float size = SIDEBAR_WIDTH * m_k;
    for (size_t i = 0; i < m_sections.size(); i++) {
        m_sections[i].sidebarButton->setPosition({0, height - size * (i + 1) - 10 * m_k});
    }
    layout();
}

void SettingsOverlay::layout() {
    constexpr float SECTION_GAP = 3.f;
    float y = 0;
    for (auto& s : m_sections) {
        s.background->setVisible(s.visible);
        s.sidebarButton->setVisible(s.visible);
        if (!s.visible) {
            for (auto r : s.rows) r->setVisible(false);
            continue;
        }
        s.top = y;
        float sectionTop = y;
        y += 12 * m_k;
        for (auto r : s.rows) {
            if (!r->isVisible()) continue;
            float h = r->getContentSize().height;
            r->setPosition({m_margin, -(y + h)});
            y += h;
        }
        y += 24 * m_k;
        s.background->setPosition({0, -sectionTop});
        s.background->setContentSize({m_panelWidth, y - sectionTop});
        y += SECTION_GAP * m_k;
    }
    m_scroll->setContentHeight(y);
}

void SettingsOverlay::applyFilter(std::string const& queryRaw) {
    auto query = lower(queryRaw);
    for (auto& s : m_sections) {
        bool headerMatch = query.empty() || s.rows.front()->searchText().find(query) != std::string::npos;
        bool anyItem = false;
        SettingsRow* subsection = nullptr;
        bool subsectionMatch = false;
        bool subsectionHasItem = false;

        auto closeSubsection = [&] {
            if (subsection) subsection->setVisible(subsectionHasItem);
        };
        for (size_t i = 1; i < s.rows.size(); i++) {
            auto r = s.rows[i];
            if (r->kind() == SettingsRow::Kind::SubsectionHeader) {
                closeSubsection();
                subsection = r;
                subsectionMatch = !query.empty() && r->searchText().find(query) != std::string::npos;
                subsectionHasItem = false;
                continue;
            }
            bool match = headerMatch || subsectionMatch || r->searchText().find(query) != std::string::npos;
            r->setVisible(match);
            if (match) { anyItem = true; subsectionHasItem = true; }
        }
        closeSubsection();
        s.visible = anyItem || headerMatch;
        s.rows.front()->setVisible(s.visible);
    }
    layout();
    m_scroll->scrollTo(0);
}

// ---------------------------------------------------------------------------
// Open / close

void SettingsOverlay::open() {
    if (m_open) return;
    m_open = true;
    this->setVisible(true);
    for (auto& s : m_sections) {
        for (auto r : s.rows) r->refresh();
    }
    // SettingsPanel.PopIn
    m_slide.to(1.f, TRANSITION, Easing::OutQuint);
}

void SettingsOverlay::close() {
    if (!m_open) return;
    m_open = false;
    m_slide.to(0.f, TRANSITION, Easing::OutQuint);
    if (m_hoveredRow) { m_hoveredRow->setHovered(false); m_hoveredRow = nullptr; }
    m_tooltip->setVisible(false);
    // Drop keyboard focus from the search box.
    m_search->defocus();
}

bool SettingsOverlay::back() {
    if (!m_open) return false;
    if (!std::string(m_search->getString()).empty()) {
        m_search->setString("", true);
        return true;
    }
    close();
    return true;
}

// ---------------------------------------------------------------------------
// Frame update

SettingsRow* SettingsOverlay::rowAt(CCPoint world) {
    if (!m_scroll->containsWorldPoint(world)) return nullptr;
    for (auto& s : m_sections) {
        if (!s.visible) continue;
        for (auto r : s.rows) {
            if (!r->isVisible() || r->kind() != SettingsRow::Kind::Item) continue;
            if (nodeContains(r, world)) return r;
        }
    }
    return nullptr;
}

int SettingsOverlay::sidebarButtonAt(CCPoint world) {
    for (size_t i = 0; i < m_sections.size(); i++) {
        auto b = m_sections[i].sidebarButton;
        if (!b->isVisible()) continue;
        auto local = b->convertToNodeSpace(world);
        float w = m_sidebarWidth + (m_sidebarExpandedWidth - m_sidebarWidth) * m_sidebarExpand.get();
        if (local.x >= 0 && local.y >= 0 && local.x <= w && local.y <= b->getContentSize().height) return int(i);
    }
    return -1;
}

void SettingsOverlay::updateTooltip(float dt, SettingsRow* hovered) {
    if (!hovered || hovered->tooltip().empty() || m_scrollDragging) {
        m_tooltipRow = nullptr;
        m_tooltipTimer = 0;
        m_tooltip->setVisible(false);
        return;
    }
    if (hovered != m_tooltipRow) {
        m_tooltipRow = hovered;
        m_tooltipTimer = 0;
        m_tooltip->setVisible(false);
        m_tooltip->removeAllChildren();

        auto text = makeWrappedText(stripTags(hovered->tooltip()), 15 * m_k, 260 * m_k, theme::CONTENT1);
        float pad = 8 * m_k;
        auto size = text->getContentSize();
        auto box = RoundedBox::create({size.width + pad * 2, size.height + pad * 2}, 5 * m_k, theme::BACKGROUND6);
        box->setAnchorPoint({0, 1});
        box->setShadow(6 * m_k, {0, 0, 0, 90});
        text->setPosition({pad, size.height + pad});
        box->addChild(text);
        m_tooltip->addChild(box);
        m_tooltip->setContentSize(box->getContentSize());
    }
    m_tooltipTimer += dt;
    if (m_tooltipTimer >= TOOLTIP_DELAY) {
        auto win = CCDirector::sharedDirector()->getWinSize();
        auto mouse = geode::cocos::getMousePos();
        auto size = m_tooltip->getContentSize();
        float x = std::min(mouse.x + 10 * m_k, win.width - size.width - 4 * m_k);
        float y = std::max(mouse.y - 10 * m_k, size.height + 4 * m_k);
        m_tooltip->setPosition({x, y});
        m_tooltip->setVisible(true);
    }
}

void SettingsOverlay::update(float dt) {
    m_slide.update(dt);
    m_sidebarExpand.update(dt);
    m_selectionY.update(dt);

    float total = m_sidebarWidth + m_panelWidth;
    m_root->setPositionX((m_slide.get() - 1.f) * total);
    m_dim->setOpacity(toByte(DIM_ALPHA * m_slide.get()));
    if (!m_open && m_slide.get() <= 0.001f) this->setVisible(false);
    g_overlayOpen = m_open;
    if (!this->isVisible()) return;

    auto mouse = geode::cocos::getMousePos();
    bool interactive = m_open && m_slide.get() > 0.5f;

    // Sidebar expands while hovered (SettingsSidebar / ExpandingContainer).
    bool overSidebar = interactive && nodeContains(m_sidebar, mouse);
    if (overSidebar != (m_sidebarExpand.target() > 0.5f)) {
        m_sidebarExpand.to(overSidebar ? 1.f : 0.f, 500, Easing::OutQuint);
    }
    float expand = m_sidebarExpand.get();
    float sidebarW = m_sidebarWidth + (m_sidebarExpandedWidth - m_sidebarWidth) * expand;
    m_sidebar->setContentSize({sidebarW, m_sidebar->getContentSize().height});
    m_sidebarBg->setContentSize({sidebarW, m_sidebar->getContentSize().height});
    m_sidebarBg->setShadow(12 * m_k * expand, {0, 0, 0, static_cast<GLubyte>(120 * expand)});

    // Current section from scroll position.
    float probe = m_scroll->scroll() + m_scroll->getContentSize().height * 0.2f;
    int current = 0;
    for (size_t i = 0; i < m_sections.size(); i++) {
        if (m_sections[i].visible && m_sections[i].top <= probe) current = int(i);
    }
    if (current != m_currentSection || m_selectionY.target() == 0) {
        m_currentSection = current;
        auto b = m_sections.empty() ? nullptr : m_sections[current].sidebarButton;
        if (b) m_selectionY.to(b->getPositionY() + b->getContentSize().height / 2, 300, Easing::OutQuint);
    }
    m_sidebarSelection->setPositionY(m_selectionY.get());
    int hoveredButton = interactive ? sidebarButtonAt(mouse) : -1;
    for (size_t i = 0; i < m_sections.size(); i++) {
        auto& s = m_sections[i];
        bool lit = int(i) == m_currentSection || int(i) == hoveredButton;
        s.sidebarIcon->setColor(lit ? theme::CONTENT1 : theme::FOREGROUND1);
        s.sidebarLabel->setColor(lit ? theme::CONTENT1 : theme::FOREGROUND1);
        s.sidebarLabel->setOpacity(toByte(expand));
    }

    // Row hover.
    SettingsRow* hovered = (interactive && !m_scrollDragging && !overSidebar) ? rowAt(mouse) : nullptr;
    if (hovered != m_hoveredRow) {
        if (m_hoveredRow) m_hoveredRow->setHovered(false);
        if (hovered) hovered->setHovered(true);
        m_hoveredRow = hovered;
    }
    updateTooltip(dt, hovered);
}

// ---------------------------------------------------------------------------
// Input

bool SettingsOverlay::ccTouchBegan(CCTouch* touch, CCEvent*) {
    if (!m_open || !this->isVisible()) return false;
    auto win = CCDirector::sharedDirector()->getWinSize();
    auto loc = touch->getLocation();

    // Toolbar stays usable.
    if (loc.y > win.height - m_topInset) return false;

    // Click outside the panel closes it, like osu!.
    float right = m_root->getPositionX() + m_sidebarWidth + m_panelWidth;
    if (loc.x > right) {
        close();
        return true;
    }

    // Let the search box take its own touches.
    if (m_search->getInputNode() && nodeContains(m_search, loc)) return false;

    m_touchStart = m_lastTouch = loc;
    m_scrollDragging = false;
    m_dragVelocity = 0;
    m_pressedRow = nullptr;
    m_pressedSidebar = sidebarButtonAt(loc);
    if (m_pressedSidebar < 0 && m_scroll->containsWorldPoint(loc)) {
        m_pressedRow = rowAt(loc);
        if (m_pressedRow && m_pressedRow->wantsDrag()) {
            m_pressedRow->onDrag(m_pressedRow->convertToNodeSpace(loc));
        }
    }
    return true;
}

void SettingsOverlay::ccTouchMoved(CCTouch* touch, CCEvent*) {
    auto loc = touch->getLocation();
    float dy = loc.y - m_lastTouch.y;
    m_lastTouch = loc;

    if (m_pressedRow && m_pressedRow->wantsDrag()) {
        m_pressedRow->onDrag(m_pressedRow->convertToNodeSpace(loc));
        return;
    }
    if (!m_scrollDragging && m_pressedSidebar < 0 && ccpDistance(loc, m_touchStart) > 5 * m_k
        && m_scroll->containsWorldPoint(m_touchStart)) {
        m_scrollDragging = true;
        m_pressedRow = nullptr;
        m_scroll->beginDrag();
    }
    if (m_scrollDragging) {
        // Content follows the finger: dragging up scrolls down.
        m_scroll->dragBy(dy);
        float dt = CCDirector::sharedDirector()->getDeltaTime();
        if (dt > 0) m_dragVelocity = m_dragVelocity * 0.5f + (dy / dt) * 0.5f;
    }
}

void SettingsOverlay::ccTouchEnded(CCTouch* touch, CCEvent*) {
    auto loc = touch->getLocation();
    if (m_scrollDragging) {
        m_scroll->endDrag(m_dragVelocity);
    } else if (m_pressedSidebar >= 0) {
        if (sidebarButtonAt(loc) == m_pressedSidebar) {
            m_scroll->scrollTo(m_sections[m_pressedSidebar].top);
        }
    } else if (m_pressedRow && !m_pressedRow->wantsDrag() && rowAt(loc) == m_pressedRow) {
        m_pressedRow->onClick(m_pressedRow->convertToNodeSpace(loc));
    }
    m_scrollDragging = false;
    m_pressedRow = nullptr;
    m_pressedSidebar = -1;
}

void SettingsOverlay::ccTouchCancelled(CCTouch* touch, CCEvent* e) {
    ccTouchEnded(touch, e);
}

} // namespace lazer
