#include "SongSelectInternal.hpp"

#include "../../audio/Sfx.hpp"
#include "../core/MenuCursor.hpp"
#include "../core/Theme.hpp"
#include "../overlays/Dialog.hpp"
#include "../overlays/VolumeOverlay.hpp"

#include <algorithm>

using namespace geode::prelude;

namespace lazer {

void SongSelect::update(float dt) {
    // A level page's loading card: only the loader runs (there's no song
    // select around it, and it never took the mouse delegate).
    if (m_pageLaunch) {
        updateLoader(dt);
        return;
    }
    float ms = dt * 1000.f;
    m_enterMs += ms;
    // GD's mouse dispatcher only feeds its newest delegate, and GD's song widget
    // (or mods extending it) can register one: take the wheel back now and then.
    m_wheelClaimMs += ms;
    if (m_wheelClaimMs > 500 && !g_overlayOpen) {
        m_wheelClaimMs = 0;
        auto dispatcher = CCDirector::get()->getMouseDispatcher();
        dispatcher->removeDelegate(this);
        dispatcher->addDelegate(this);
    }
    // An overlay (the comments page) covers everything, and so does the
    // loader: the search box mustn't take taps through them, and nothing here
    // hovers. (GD's text box takes its own touches, so it's switched off.)
    bool covered = g_overlayOpen || m_starting;
    if (m_search && m_searchEnabled == covered) {
        m_searchEnabled = !covered;
        m_search->setEnabled(m_searchEnabled);
        if (m_pageInput) m_pageInput->setEnabled(m_searchEnabled);
    }
    // Playing: only the loader animates; song select is frozen and fading.
    if (m_starting) {
        updateLoader(dt);
        return;
    }
    if (onlineMode()) updateOnline(dt);
    updateCarousel(dt);
    updateScrollbar(dt);

    m_wedgeAlpha.update(dt);
    if (m_loadingSpinner) m_loadingSpinner->setRotation(m_loadingSpinner->getRotation() + dt * 300.f);
    if (m_noResultsMs >= 0) {
        m_noResultsMs -= ms;
        bool searching = (packMode() && packLevelsLoading()) || (onlineMode() && browse::loading());
        if (m_noResultsMs < 0 && !m_query.empty() && m_visible.empty() && !searching && m_saidFor != m_query) {
            m_saidFor = m_query;
            cursorSay(NO_RESULTS_CURSOR[pickLine(m_query + "!", NO_RESULTS_CURSOR.size())]);
        }
    }
    m_wedge->setPositionX(-24 * m_k * (1.f - m_wedgeAlpha.get()));

    if (m_previewDelay >= 0) {
        m_previewDelay -= ms;
        if (m_previewDelay < 0) previewSong();
    } else if (!m_leaving && !m_previewPath.empty() && !FMODAudioEngine::sharedEngine()->isMusicPlaying(0)) {
        // The menu's song (not looped, unlike previews) ran out: loop it like one.
        m_previewPath.clear();
        previewSong();
    }

    auto mouse = geode::cocos::getMousePos();
    auto updateButton = [&](Button& b) {
        bool hovered = !g_overlayOpen && hittable(b, mouse);
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
    for (auto& b : m_menuItems) updateButton(b);
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
    for (auto& b : m_menuItems) {
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
    // An overlay is open over song select: its own text box may want the touch.
    if (g_overlayOpen) return false;
    // The loader: nothing underneath it reacts (the search box included).
    if (m_starting) return true;
    // Let the search field (and the pager's box) take their own touches.
    if (m_search && containsWorld(m_search, loc)) return false;
    if (m_pageInput && containsWorld(m_pageInput, loc)) return false;
    m_touchDown = true;
    m_dragging = false;
    m_touchStart = m_touchLast = loc;
    // The scrollbar: grab it where it was touched, or (touching the track
    // beside it) jump there and hold it by the middle.
    if (!m_menu && scrollbarHit(loc)) {
        m_barDragging = true;
        m_barGrab = std::abs(loc.y - m_barY) <= m_barLength / 2 ? loc.y - m_barY : 0.f;
        m_barHighlight.to(1.f, 100, Easing::None);
        m_barWidth.to(SCROLLBAR_HELD_WIDTH, 400, Easing::OutElastic);
        m_barLabelAlpha.to(1.f, 150, Easing::OutQuint);
        m_barText.clear();
        dragScrollbar(loc);
        return true;
    }
    m_pressed = buttonAt(loc);
    // A tap outside the open dropdown closes it.
    if (m_menu) {
        bool inMenu = m_pressed && (m_pressed == m_menuAnchor
            || (m_pressed >= m_menuItems.data() && m_pressed < m_menuItems.data() + m_menuItems.size()));
        if (!inMenu) {
            closeMenu();
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
    if (m_barDragging) {
        dragScrollbar(loc);
        return;
    }
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
    if (m_barDragging) {
        m_barDragging = false;
        m_touchDown = false;
        m_barHighlight.to(0.f, 100, Easing::None);
        m_barWidth.to(1.f, 300, Easing::OutQuint);
        // Let go: the rubber snaps back and wobbles.
        m_barPull.to(0.f, 600, Easing::OutElastic);
        m_barLabelAlpha.to(0.f, 300, Easing::OutQuint);
        return;
    }
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
    if (index == SIZE_MAX || index >= m_visible.size()) return;
    auto const& row = m_entries[m_visible[index]];
    // A pack opens on a click and closes on another, like osu!'s beatmap sets.
    if (row.packHeader) {
        sfx::play(sfx::sound::DEFAULT_SELECT);
        expandPack(row.pack == m_expandedPack ? -1 : row.pack);
        return;
    }
    // Clicking the selected level plays it, like osu!.
    if (m_hasSelection && index == m_selected) {
        start();
        return;
    }
    sfx::play(sfx::sound::DEFAULT_SELECT);
    select(index);
}

void SongSelect::scrollWheel(float y, float) {
    if (m_starting || Dialog::isOpen() || g_overlayOpen) return;
    // Left side: the level details; right side: the carousel.
    if (geode::cocos::getMousePos().x < m_win.width - m_rightW) {
        if (m_details) m_details->scrollWheel(y, 0);
        return;
    }
    // Positive = down; one notch moves about one and a half panels.
    volume::markWheelHandled();
    float notches = std::clamp(y / 12.f, -3.f, 3.f);
    m_scrollTarget += notches * (m_panelH + m_spacing) * 1.5f;
}

void SongSelect::keyDown(enumKeyCodes key, double timestamp) {
    // A dialog or an overlay (the comments page) has the keys, and handles Escape itself.
    if (Dialog::isOpen() || g_overlayOpen) return;
    // Typing a page number: its box has the keys. GD's text inputs don't
    // report Enter (its delegate's enterPressed never fires): it's taken here.
    if (m_pageInputOpen) {
        if (key == KEY_Enter || key == KEY_NumEnter) {
            m_pageCommitted = true;
            commitPageInput();
            if (m_pageInput) m_pageInput->defocus();
        }
        return;
    }
    // GD's CCLayer::keyDown turns Escape into keyBackClicked: let it through.
    if (!m_hasSelection || m_starting) return CCLayer::keyDown(key, timestamp);
    // Stepping onto a closed pack opens it (osu! moves through the sets' difficulties).
    auto step = [this](size_t to) {
        sfx::play(sfx::sound::DEFAULT_HOVER);
        auto const& e = m_entries[m_visible[to]];
        if (e.packHeader && e.pack != m_expandedPack) expandPack(e.pack);
        else select(to);
    };
    switch (key) {
        case KEY_Up:
            if (m_selected > 0) step(m_selected - 1);
            break;
        case KEY_Down:
            if (m_selected + 1 < m_visible.size()) step(m_selected + 1);
            break;
        case KEY_Enter:
            start();
            break;
        case KEY_F2:
            selectRandom();
            break;
        default:
            CCLayer::keyDown(key, timestamp);
            break;
    }
}

void SongSelect::keyBackClicked() {
    if (Dialog::isOpen() || g_overlayOpen) return;
    // During the loader, back cancels it (osu!'s back button does the same).
    if (m_starting) return cancelLoader();
    back();
}

} // namespace lazer
