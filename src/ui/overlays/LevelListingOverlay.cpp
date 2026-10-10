#include "LevelListingInternal.hpp"

#include "../../audio/Sfx.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>

using namespace geode::prelude;

namespace lazer {

using namespace levellisting;

namespace {
    bool nodeContains(CCNode* node, CCPoint world) {
        auto local = node->convertToNodeSpace(world);
        auto size = node->getContentSize();
        return local.x >= 0 && local.y >= 0 && local.x <= size.width && local.y <= size.height;
    }

    bool nodeShown(CCNode* node) {
        for (auto n = node; n; n = n->getParent()) {
            if (!n->isVisible()) return false;
        }
        return true;
    }

    std::string trim(std::string s) {
        auto space = [](char c) { return std::isspace(static_cast<unsigned char>(c)) != 0; };
        while (!s.empty() && space(s.back())) s.pop_back();
        size_t start = 0;
        while (start < s.size() && space(s[start])) start++;
        return s.substr(start);
    }

    ccColor3B mix3(ccColor3B a, ccColor3B b, float t) {
        auto mix = [t](GLubyte x, GLubyte y) { return static_cast<GLubyte>(x + (y - x) * t); };
        return {mix(a.r, b.r), mix(a.g, b.g), mix(a.b, b.b)};
    }

    struct PageText {
        char const* icon;
        std::string title;
        std::string description;
    };
    // OverlayTitle: the page's icon, title and description.
    PageText pageText(LevelBrowserLayer* owner, GJSearchObject* search) {
        bool lists = search->m_searchMode == 1;
        switch (search->m_searchType) {
            case SearchType::MyLevels: return {icon::FOLDER_OPEN, "your levels", "the levels you're making"};
            case SearchType::MyLists: return {icon::LIST, "your lists", "the lists you've put together"};
            default: break;
        }
        // GD's own title for anything else.
        std::string title = lower(std::string(owner->getSearchTitle()));
        if (title.empty()) title = lists ? "lists" : "levels";
        return {lists ? icon::LAYERS : icon::LIST, title, lists ? "level lists" : "online levels"};
    }
}

bool LevelListingOverlay::wants(GJSearchObject* search) {
    if (!search || !Mod::get()->getSettingValue<bool>("enabled")) return false;
    // The online lists are song select's now (OnlineBrowse.hpp): this page
    // is your levels and lists. Not as the list picker GD opens over a
    // level's page ("add to list").
    switch (search->m_searchType) {
        case SearchType::MyLevels:
        case SearchType::MyLists:
            return !search->m_searchIsOverlay;
        default:
            return false;
    }
}

theme::Scheme LevelListingOverlay::schemeFor(GJSearchObject*) {
    return CREATE_SCHEME;
}

bool& LevelListingOverlay::backToCreate() {
    static bool back = false;
    return back;
}

LevelListingOverlay* LevelListingOverlay::create(LevelBrowserLayer* owner, GJSearchObject* search) {
    auto ret = new LevelListingOverlay();
    if (ret->init(owner, search)) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

bool LevelListingOverlay::init(LevelBrowserLayer* owner, GJSearchObject* search) {
    m_owner = owner;
    m_lists = search->m_searchMode == 1 || search->m_searchType == SearchType::MyLists;
    auto text = pageText(owner, search);
    if (!WaveOverlay::init(0, schemeFor(search), text.icon, text.title, text.description)) return false;
    m_alive = std::make_shared<char>(0);
    m_pad = HORIZONTAL_PADDING * m_k;
    m_rowNodes.assign(static_cast<size_t>(Row::Count), nullptr);
    m_rowHeights.assign(static_cast<size_t>(Row::Count), 0.f);
    // GD's browser may carry a folder and a search of its own: start from them.
    m_mineSearch = search;
    m_query = trim(std::string(search->m_searchQuery));
    m_folder = std::max(0, search->m_folder);
    readFolders();

    m_scroll = ScrollArea::create(bodySize());
    body()->addChild(m_scroll);

    // As many columns of roomy cards as fit, stretched to fill the row.
    float k = m_k;
    float avail = bodySize().width - 2 * CARDS_PADDING * k, spacing = CARD_SPACING * k;
    m_columns = std::max(1, static_cast<int>((avail + spacing) / (CARD_MIN_WIDTH * k + spacing)));
    m_cardW = (avail - (m_columns - 1) * spacing) / m_columns;
    m_cardH = CARD_HEIGHT * k;

    // The controls are built once; the cards and the footer under them change.
    buildSearchControl();
    buildStrip();
    m_list = CCNodeRGBA::create();
    m_list->setCascadeOpacityEnabled(true);
    m_scroll->content()->addChild(m_list, 3);
    m_footer = CCNode::create();
    m_scroll->content()->addChild(m_footer, 3);

    // Your levels are read straight from GD's local levels, a few cards a frame.
    showLocal();
    return true;
}

// --- per frame and input ---

void LevelListingOverlay::updatePill(Pill& p, bool hovered, float dt) {
    if (hovered && !p.hovered) sfx::hover(p.kind == Pill::Kind::Card ? sfx::sound::BUTTON_HOVER : sfx::sound::DEFAULT_HOVER);
    p.hovered = hovered;
    bool active = p.active && p.active();
    bool usable = !p.usable || p.usable();
    float target = hovered ? 1.f : 0.f;
    switch (p.kind) {
        case Pill::Kind::Card: {
            // BeatmapCardContentBackground: brighter while hovered, with a rim.
            if (p.hover.target() != target) p.hover.to(target, CARD_TRANSITION, Easing::OutQuint);
            p.hover.update(dt);
            float t = p.hover.get();
            p.bg->setFillColor(theme::lerp(p.color, p.hoverColor, t));
            if (p.rim) p.rim->setOpacity(static_cast<GLubyte>(255 * std::clamp(t, 0.f, 1.f)));
            return;
        }
        case Pill::Kind::Chip:
        case Pill::Kind::Tab: {
            // Filled in the page's colour while chosen; a lighter fill while hovered.
            if (p.hover.target() != target) p.hover.to(target, CHIP_TRANSITION, Easing::OutQuint);
            p.hover.update(dt);
            float t = p.hover.get();
            auto base = active ? p.activeColor : p.color;
            auto lit = active ? theme::lerp(p.activeColor, WHITE, 0.15f) : p.hoverColor;
            auto fill = theme::lerp(base, lit, t);
            auto text = active ? p.textActive : mix3(p.textColor, p.textHover, t);
            if (!usable) {
                fill = theme::lerp(fill, BLACK, 0.5f);
                text = mix3(text, {0, 0, 0}, 0.5f);
            }
            p.bg->setFillColor(fill);
            for (auto label : p.tinted) label->setColor(text);
            return;
        }
        case Pill::Kind::Button: {
            if (p.bg) {
                auto colour = hovered ? p.hoverColor : p.color;
                if (!p.enabled) colour = theme::lerp(p.color, {40, 40, 40, 255}, 0.6f);
                p.bg->setFillColor(colour);
            }
            for (auto label : p.tinted) label->setColor(hovered ? p.textHover : p.textColor);
            return;
        }
    }
}

void LevelListingOverlay::onUpdate(float dt) {
    if (m_leaving) return;
    // The close button: leave right away (the waves keep dropping during the fade).
    if (!isOpen()) return goBack();
    float ms = dt * 1000.f;
    // A frame in: every mod's hook on GD's browser has added its buttons by now.
    if (!m_modButtonsAdded) {
        m_modButtonsAdded = true;
        addModButtons();
    }

    if (m_searchDelay >= 0) {
        m_searchDelay -= ms;
        if (m_searchDelay < 0) startSearch();
    }
    if (m_state == State::Loading) {
        m_loadingMs += ms;
        if (m_loadingMs > LOAD_TIMEOUT_MS) {
            m_key.clear();
            if (m_stale) clearCards();
            m_state = State::Failed;
            m_dirty = true;
        }
    }
    // Your levels, a few more cards each frame.
    if (!m_toBuild.empty()) buildPending();
    if (m_dirty) rebuildFooter();

    // The placeholders' shimmer and the loading bar's sweep.
    m_shimmerPhase += dt * SHIMMER_SPEED;
    auto second = theme::lerp(m_scheme.background3(), m_scheme.background4(), 0.5f);
    for (auto box : m_shimmer) box->setGradient(second, 0.4f, m_shimmerPhase);
    if (m_progressTrack && m_progressTrack->isVisible()) {
        m_progressT = std::fmod(m_progressT + dt / PROGRESS_PERIOD, 1.f);
        float W = bodySize().width, barW = m_progressBar->getContentSize().width;
        float p = static_cast<float>(ease(Easing::InOutSine, m_progressT));
        m_progressBar->setPositionX(-barW + (W + barW) * p);
    }

    // The old cards dim while a new search loads.
    float alphaTarget = m_stale ? STALE_ALPHA : 1.f;
    if (m_listAlpha.target() != alphaTarget) m_listAlpha.to(alphaTarget, 200, Easing::OutQuint);
    m_listAlpha.update(dt);
    auto listAlpha = static_cast<GLubyte>(255 * std::clamp(m_listAlpha.get(), 0.f, 1.f));
    if (m_list->getOpacity() != listAlpha) m_list->setOpacity(listAlpha);

    for (auto& card : m_cards) {
        // Cards come in one after the other, rising into place.
        if (!card.started) {
            card.delayMs -= ms;
            if (card.delayMs <= 0) {
                card.started = true;
                card.appear.to(1.f, CARD_FADE, Easing::OutQuint);
            }
        } else if (card.appear.get() < 1.f) {
            card.appear.update(dt);
            auto alpha = static_cast<GLubyte>(255 * std::clamp(card.appear.get(), 0.f, 1.f));
            card.root->setOpacity(alpha);
            placeCard(card);
        }
        if (card.shimmer) card.thumb->setGradient(m_scheme.background4(), 0.4f, m_shimmerPhase);
        // Thumbnails for the cards near the view.
        if (!card.thumbRequested && nearView(card.top, m_cardH)) requestThumbnail(card);
    }

    // The next page as the end of this one nears the view (osu! pages on scroll).
    if (m_state == State::Loaded && m_more) {
        float viewH = m_scroll->getContentSize().height;
        if (m_scroll->contentHeight() - (m_scroll->scroll() + viewH) < viewH) loadMore();
    }

    // The search box's cross shows while there's something to clear.
    if (m_clearPill < m_fixedPills.size()) m_fixedPills[m_clearPill].node->setVisible(!m_query.empty());

    auto mouse = geode::cocos::getMousePos();
    bool interactive = isOpen() && !popupOnTop() && !m_drag.dragging() && m_scroll->containsWorldPoint(mouse);
    for (auto list : {&m_fixedPills, &m_cardPills, &m_footerPills}) {
        for (auto& p : *list) {
            bool usable = p.enabled && (!p.usable || p.usable());
            bool hovered = interactive && p.action && usable && nodeShown(p.node) && nodeContains(p.node, mouse);
            updatePill(p, hovered, dt);
        }
    }
}

LevelListingOverlay::Pill* LevelListingOverlay::pillAt(CCPoint world) {
    if (!m_scroll->containsWorldPoint(world)) return nullptr;
    Pill* hit = nullptr;
    for (auto list : {&m_fixedPills, &m_cardPills, &m_footerPills}) {
        for (auto& p : *list) {
            bool usable = p.enabled && (!p.usable || p.usable());
            if (p.action && usable && nodeShown(p.node) && nodeContains(p.node, world)) hit = &p;
        }
    }
    return hit;
}

bool LevelListingOverlay::ccTouchBegan(CCTouch* touch, CCEvent* e) {
    if (m_leaving) return false;
    auto loc = touch->getLocation();
    // The search box takes its own touches (its input node registers below us).
    if (isOpen() && m_input && nodeShown(m_input) && m_scroll->containsWorldPoint(loc) && nodeContains(m_input, loc)) {
        return false;
    }
    if (!WaveOverlay::ccTouchBegan(touch, e)) return false;
    // A tap anywhere else leaves the box (it only ever sees its own touches).
    if (m_input) m_input->defocus();
    m_pressed = pillAt(loc);
    m_drag.began(m_scroll, loc);
    return true;
}

void LevelListingOverlay::ccTouchMoved(CCTouch* touch, CCEvent*) {
    if (m_drag.moved(touch->getLocation())) m_pressed = nullptr;
}

void LevelListingOverlay::ccTouchEnded(CCTouch* touch, CCEvent* e) {
    WaveOverlay::ccTouchEnded(touch, e);
    m_drag.ended();
    auto pressed = m_pressed;
    m_pressed = nullptr;
    if (!pressed || !pressed->action || !nodeContains(pressed->node, touch->getLocation())) return;
    if (!pressed->enabled || (pressed->usable && !pressed->usable())) return;
    // HoverSampleSet.Button for cards, the default for everything else.
    auto kind = pressed->kind;
    auto action = pressed->action; // may rebuild the list, and the pill with it
    if (kind == Pill::Kind::Card) sfx::click(sfx::sound::BUTTON_SELECT);
    else sfx::click(sfx::sound::DEFAULT_SELECT);
    action();
}

void LevelListingOverlay::textChanged(CCTextInputNode*) {
    if (!m_input) return;
    auto query = trim(std::string(m_input->getString()));
    if (query == m_query) return;
    m_query = query;
    // Typing waits a little before searching (BeatmapListingFilterControl's
    // debounce); your levels on this device filter almost right away.
    queueSearch(local() ? FILTER_DEBOUNCE : QUERY_DEBOUNCE);
}

void LevelListingOverlay::enterPressed(CCTextInputNode*) {
    if (m_input) m_query = trim(std::string(m_input->getString()));
    startSearch();
}

} // namespace lazer
