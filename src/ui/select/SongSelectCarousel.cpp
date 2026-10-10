#include "SongSelectInternal.hpp"

#include "../../audio/Sfx.hpp"
#include "../core/Theme.hpp"

#include <algorithm>

using namespace geode::prelude;

namespace lazer {

// --- carousel ---

SongSelect::Panel& SongSelect::makePanel(size_t visibleIndex) {
    float k = m_k;
    levels::resolve(m_entries[m_visible[visibleIndex]]); // coins for the info row
    auto const& e = m_entries[m_visible[visibleIndex]];
    float pw = m_rightW + 60 * k, ph = rowHeight(visibleIndex);
    // Pack mode: a pack's header row (osu!'s set panel), or one of its levels,
    // a shorter row (PanelBeatmap) under the open header.
    bool header = e.packHeader;
    bool compact = packMode() && !header;
    auto pack = packOf(e);
    auto accent = header && pack ? pack->barColor : levels::difficultyColor(e.difficulty);
    float stripW = (compact ? PACK_STRIP_WIDTH : STRIP_WIDTH) * k;

    // Fades in as a whole: its parts follow its opacity.
    auto root = CCNodeRGBA::create();
    root->setCascadeOpacityEnabled(true);
    root->setOpacity(0);
    root->setContentSize({pw, ph});
    root->setAnchorPoint({0, 0.5f});
    m_carousel->addChild(root);

    // A pack's levels are darker rows under it.
    ccColor4B base = compact ? PACK_LEVEL_BG : PANEL_BG;
    auto bg = RoundedBox::create({pw, ph}, CORNER * k, base);
    bg->setAnchorPoint({0, 0});
    root->addChild(bg, 0);

    // Thumbnail on the right, fading into the panel (PanelSetBackground). A
    // pack shows every one of its levels' pictures side by side.
    RoundedBox* thumb = nullptr;
    std::vector<Panel::Tile> tiles;
    float thumbX = pw * 0.38f;
    if (header && pack && !pack->levelIDs.empty()) {
        size_t n = std::min<size_t>(pack->levelIDs.size(), 5);
        float gap = 2 * k;
        float tileW = (pw - thumbX - gap * (n - 1)) / n;
        for (size_t i = 0; i < n; i++) {
            bool lastTile = i + 1 == n;
            auto tile = RoundedBox::create({tileW, ph}, CORNER * k, {255, 255, 255, 255});
            tile->setCornerRadii(0, lastTile ? CORNER * k : 0, 0, lastTile ? CORNER * k : 0);
            tile->setAnchorPoint({0, 0});
            tile->setPosition({thumbX + i * (tileW + gap), 0});
            tile->setOpacity(0);
            tile->setVisible(false);
            root->addChild(tile, 1);
            tiles.push_back({tile, pack->levelIDs[i]});
        }
    } else {
        thumb = RoundedBox::create({pw - thumbX, ph}, CORNER * k, {255, 255, 255, 255});
        thumb->setCornerRadii(0, CORNER * k, 0, CORNER * k);
        thumb->setAnchorPoint({0, 0});
        thumb->setPosition({thumbX, 0});
        thumb->setOpacity(0);
        thumb->setVisible(false);
        root->addChild(thumb, 1);
    }
    auto fade = CascadingGradient::create({base.r, base.g, base.b, 255}, {base.r, base.g, base.b, 0}, {1, 0});
    fade->setContentSize({pw * 0.3f, ph});
    fade->setPosition({thumbX, 0});
    root->addChild(fade, 2);
    if (header) {
        // A wash of the pack's colour behind its name, so a pack reads as one.
        auto wash = CascadingGradient::create({accent.r, accent.g, accent.b, 60}, {accent.r, accent.g, accent.b, 0}, {1, 0});
        wash->setContentSize({pw * 0.45f, ph});
        root->addChild(wash, 2);
    }

    // Difficulty strip (a pack's, in its bar colour).
    auto strip = RoundedBox::create({stripW, ph}, CORNER * k, {accent.r, accent.g, accent.b, 255});
    strip->setCornerRadii(CORNER * k, 0, CORNER * k, 0);
    strip->setAnchorPoint({0, 0});
    root->addChild(strip, 3);
    CCSprite* gauntletIcon = header && pack && pack->gauntlet && !pack->frame.empty()
        ? CCSprite::createWithSpriteFrameName(pack->frame.c_str()) : nullptr;
    if (compact) {
        auto face = difficultyFace(e, ph * 0.62f);
        face->setPosition({stripW / 2, ph / 2});
        root->addChild(face, 4);
        if (e.locked) {
            // A gauntlet's level not reached yet: a lock over its face.
            if (auto rgba = dynamic_cast<CCRGBAProtocol*>(face)) rgba->setOpacity(90);
            auto lock = makeIcon(icon::LOCK, ph * 0.34f);
            lock->setColor({255, 255, 255});
            lock->setPosition({stripW / 2, ph / 2});
            root->addChild(lock, 5);
        }
    } else if (gauntletIcon) {
        // A gauntlet shows GD's badge for it in place of a difficulty.
        auto size = gauntletIcon->getContentSize();
        float fitTo = ph * 0.8f;
        if (size.width > 0 && size.height > 0) gauntletIcon->setScale(fitTo / std::max(size.width, size.height));
        gauntletIcon->setPosition({stripW / 2, ph / 2});
        root->addChild(gauntletIcon, 4);
    } else {
        auto face = difficultyFace(e, ph * 0.56f);
        face->setPosition({stripW / 2, ph * 0.6f});
        root->addChild(face, 4);
        if (e.stars > 0) {
            // A pack's stars are what finishing it gives (a list's diamonds).
            char const* glyph = header && pack && pack->list ? icon::GEM : rewardIcon(e);
            auto stars = infoRow({{glyph, (header ? "+" : "") + std::to_string(e.stars)}}, 12 * k, {255, 255, 255});
            stars->setPosition({(stripW - stars->getContentSize().width + 10 * k) / 2, ph * 0.17f});
            root->addChild(stars, 4);
        }
    }

    float x = stripW + 14 * k;
    float maxW = pw * 0.62f - x;
    CCLabelBMFont* chevron = nullptr;
    if (header) {
        // The set panel: the pack's name in its own colour, how many of its
        // levels are done with a bar in the pack's colour, the reward's state,
        // and a chevron that turns as the pack opens.
        auto tag = makeText(pack && pack->list ? "LIST" : pack && pack->gauntlet ? "GAUNTLET" : "MAP PACK", Weight::SemiBold, 10 * k);
        tag->setColor(accent);
        tag->setAnchorPoint({0, 0.5f});
        tag->setPosition({x + 1 * k, ph * 0.88f});
        root->addChild(tag, 4);
        auto title = makeText(e.name, Weight::SemiBold, 22 * k);
        title->setColor(pack ? pack->textColor : ccColor3B {255, 255, 255});
        title->setAnchorPoint({0, 0.5f});
        title->setPosition({x, ph * 0.66f});
        fit(title, maxW - 20 * k);
        root->addChild(title, 4);

        int total = pack ? static_cast<int>(pack->levelIDs.size()) : 0;
        int done = pack ? std::min(pack->completed, total) : 0;
        std::vector<std::pair<char const*, std::string>> info;
        if (pack && pack->list) info.push_back({icon::USER, pack->creator});
        info.push_back({icon::LAYERS, fmt::format("{} level{}", total, total == 1 ? "" : "s")});
        info.push_back({total > 0 && done >= total ? icon::CHECK : nullptr, fmt::format("{}/{} done", done, total)});
        if (e.coins > 0) info.push_back({icon::COINS, "+" + std::to_string(e.coins)});
        if (pack && pack->list) info.push_back({icon::CLOUD_DOWN, std::to_string(pack->downloads)});
        auto row = infoRow(info, 12 * k, theme::LIGHT1);
        if (row->getContentSize().width > maxW) row->setScale(maxW / row->getContentSize().width);
        row->setPosition({x, ph * 0.4f});
        root->addChild(row, 4);

        float barW = std::min(maxW - 20 * k, 220 * k);
        auto track = RoundedBox::create({barW, 5 * k}, 2.5f * k, {255, 255, 255, 40});
        track->setAnchorPoint({0, 0.5f});
        track->setPosition({x, ph * 0.17f});
        root->addChild(track, 4);
        if (done > 0 && total > 0) {
            auto fill = RoundedBox::create({barW * done / total, 5 * k}, 2.5f * k, {accent.r, accent.g, accent.b, 255});
            fill->setAnchorPoint({0, 0.5f});
            fill->setPosition({x, ph * 0.17f});
            root->addChild(fill, 4);
        }

        float cx = pw * 0.66f;
        auto chevronBg = RoundedBox::create({26 * k, 26 * k}, 13 * k, {0, 0, 0, 120});
        chevronBg->setPosition({cx, ph / 2});
        root->addChild(chevronBg, 4);
        chevron = makeIcon(icon::CHEVRON_DOWN, 14 * k);
        chevron->setPosition({cx, ph / 2});
        root->addChild(chevron, 5);
        if (pack && (canClaimPack(*pack) || pack->claimed)) {
            bool claim = canClaimPack(*pack);
            auto chip = infoRow({{claim ? icon::GIFT : icon::CHECK, claim ? "reward!" : "claimed"}}, 12 * k,
                                claim ? ccColor3B {255, 214, 76} : theme::LIGHT1);
            float chipW = chip->getContentSize().width;
            float chipX = cx - 22 * k - chipW;
            // On a dark pill: it sits over the pictures.
            auto pill = RoundedBox::create({chipW + 8 * k, 22 * k}, 11 * k, {0, 0, 0, 120});
            pill->setAnchorPoint({0, 0.5f});
            pill->setPosition({chipX - 10 * k, ph / 2});
            root->addChild(pill, 4);
            chip->setPosition({chipX - 4 * k, ph / 2});
            root->addChild(chip, 5);
        }
    } else if (compact) {
        auto title = makeText(e.name, Weight::SemiBold, 18 * k);
        if (e.locked) title->setColor(theme::LIGHT1);
        title->setAnchorPoint({0, 0.5f});
        title->setPosition({x, ph * 0.68f});
        fit(title, maxW);
        root->addChild(title, 4);

        std::vector<std::pair<char const*, std::string>> info;
        if (e.locked) info.push_back({icon::LOCK, "locked"});
        info.push_back({icon::USER, e.creator});
        if (!e.platformer) info.push_back({icon::CLOCK, levels::lengthName(e.length)});
        if (e.coins > 0) info.push_back({icon::COINS, fmt::format("{}/{}", e.coinsCollected, e.coins)});
        if (!e.platformer) info.push_back({e.normalPercent >= 100 ? icon::CHECK : nullptr, fmt::format("{}%", e.normalPercent)});
        else if (e.normalPercent >= 100) info.push_back({icon::CHECK, e.bestTime > 0 ? formatTime(e.bestTime) : "completed"});
        auto row = infoRow(info, 12 * k, theme::LIGHT1);
        if (row->getContentSize().width > maxW) row->setScale(maxW / row->getContentSize().width);
        row->setPosition({x, ph * 0.28f});
        root->addChild(row, 4);
    } else {
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
        if (e.dailyID > 0) {
            // A daily (weekly, event) level: which one it was.
            auto type = levels::timedTypeOf(e.dailyID);
            char const* glyph = type == GJTimedLevelType::Weekly ? icon::CALENDAR_WEEK
                              : type == GJTimedLevelType::Event ? icon::BOLT : icon::CALENDAR_DAY;
            info.push_back({glyph, fmt::format("{} #{}", levels::timelyName(type), levels::timelyNumber(e.dailyID))});
        }
        if (!e.platformer) info.push_back({icon::CLOCK, levels::lengthName(e.length)});
        if (e.coins > 0) info.push_back({icon::COINS, fmt::format("{}/{}", e.coinsCollected, e.coins)});
        if (!e.platformer) info.push_back({e.normalPercent >= 100 ? icon::CHECK : nullptr, fmt::format("{}%", e.normalPercent)});
        else if (e.normalPercent >= 100) info.push_back({icon::CHECK, e.bestTime > 0 ? formatTime(e.bestTime) : "completed"});
        auto row = infoRow(info, 12 * k, theme::LIGHT1);
        if (row->getContentSize().width > maxW) row->setScale(maxW / row->getContentSize().width);
        row->setPosition({x, ph * 0.2f});
        root->addChild(row, 4);

        if (timelyMode() && m_timelyRow && m_visible[visibleIndex] == 0) {
            // The current one: a chip over the picture, like a pack's reward.
            bool claim = m_timely.claimable;
            char const* text = claim ? "reward!" : m_timely.completed ? "beaten" : "current";
            auto chip = infoRow({{claim ? icon::GIFT : m_timely.completed ? icon::CHECK : icon::CLOCK, text}}, 12 * k,
                                claim ? ccColor3B {255, 214, 76} : theme::CONTENT1);
            float chipW = chip->getContentSize().width;
            float chipX = pw * 0.66f - chipW;
            auto pill = RoundedBox::create({chipW + 8 * k, 22 * k}, 11 * k, {0, 0, 0, 140});
            pill->setAnchorPoint({0, 0.5f});
            pill->setPosition({chipX - 10 * k, ph / 2});
            root->addChild(pill, 4);
            chip->setPosition({chipX - 4 * k, ph / 2});
            root->addChild(chip, 5);
        }
    }

    auto& panel = m_panels[visibleIndex];
    panel = Panel {m_visible[visibleIndex], root, bg, thumb};
    panel.chevron = chevron;
    panel.tiles = std::move(tiles);
    panel.base = base;
    if (header && e.pack == m_expandedPack) panel.expand.set(1.f);
    if (chevron) chevron->setRotation(-90.f * (1.f - panel.expand.get()));
    panel.appear.to(1.f, PANEL_FADE, Easing::OutQuint);
    return panel;
}

void SongSelect::updateCarousel(float dt) {
    float ms = dt * 1000.f;
    float k = m_k;
    float viewH = viewHeight(), halfH = viewH / 2;
    float step = m_panelH + m_spacing;

    if (!m_visible.empty()) {
        auto [minScroll, maxScroll] = scrollRange();
        if (!m_dragging) m_scrollTarget = std::clamp(m_scrollTarget, minScroll, maxScroll);
    }
    if (m_dragging || m_barDragging) m_scroll = m_scrollTarget;
    else m_scroll = damp(m_scroll, m_scrollTarget, SCROLL_DECAY, ms);

    for (auto& [index, panel] : m_panels) panel.seen = false;
    if (!m_visible.empty()) {
        int count = static_cast<int>(m_visible.size());
        int first = 0, last = count - 1;
        float middle = 0;
        if (m_rowTops.size() == m_visible.size() + 1) {
            // Rows can differ in height (packs): the view's edges, and its
            // middle, found among the row tops.
            auto rowAt = [&](float y) {
                auto it = std::upper_bound(m_rowTops.begin(), m_rowTops.end() - 1, y);
                return std::clamp(static_cast<int>(it - m_rowTops.begin()) - 1, 0, count - 1);
            };
            first = rowAt(m_scroll - m_panelH);
            last = std::min(count - 1, rowAt(m_scroll + viewH + m_panelH) + 1);
            middle = static_cast<float>(rowAt(m_scroll + halfH));
        } else {
            first = std::max(0, static_cast<int>(std::floor((m_scroll - m_panelH) / step)));
            last = std::min(count - 1, static_cast<int>(std::ceil((m_scroll + viewH + m_panelH) / step)));
            middle = (m_scroll + halfH - m_panelH / 2) / step;
        }
        auto mouse = geode::cocos::getMousePos();
        float colLeft = m_win.width - m_rightW;

        // Missing panels, a few per frame, from the middle of the view outwards.
        std::vector<int> missing;
        for (int i = first; i <= last; i++) {
            if (!m_panels.contains(i)) missing.push_back(i);
        }
        std::sort(missing.begin(), missing.end(), [middle](int a, int b) {
            return std::abs(a - middle) < std::abs(b - middle);
        });
        for (size_t n = 0; n < missing.size() && n < static_cast<size_t>(PANEL_LOADS_PER_FRAME); n++) makePanel(missing[n]);

        for (int i = first; i <= last; i++) {
            auto it = m_panels.find(i);
            if (it == m_panels.end()) continue; // built in a later frame
            Panel& p = it->second;
            p.seen = true;
            p.appear.update(dt);
            auto alpha = static_cast<GLubyte>(255 * std::clamp(p.appear.get(), 0.f, 1.f));
            if (p.root->getOpacity() != alpha) p.root->setOpacity(alpha);

            float centerFromTop = itemTop(i) - m_scroll + rowHeight(i) / 2;
            float y = m_carouselTop - centerFromTop;
            // Carousel.offsetX: panels curve away towards the top and bottom.
            float dist = std::abs(1.f - centerFromTop / halfH);
            float offset = (3.f - std::sqrt(std::max(0.f, 9.f - dist * dist))) * halfH;

            auto const& e = m_entries[p.entry];
            bool open = e.packHeader && e.pack == m_expandedPack;
            bool selected = (m_hasSelection && static_cast<size_t>(i) == m_selected) || open;
            if (p.active.target() != (selected ? 1.f : 0.f)) p.active.to(selected ? 1.f : 0.f, 400, Easing::OutQuint);
            if (p.chevron && p.expand.target() != (open ? 1.f : 0.f)) p.expand.to(open ? 1.f : 0.f, 400, Easing::OutQuint);
            // Where a row rests when it isn't the selection: packs sit back,
            // their levels a little less (Panel.updateXOffset).
            float rest = 0;
            if (packMode()) rest = (e.packHeader ? PACK_REST_X : PACK_LEVEL_REST_X) * k;
            float active = p.active.get();
            p.root->setPosition({colLeft + offset + rest * (1 - active) - active * ACTIVE_X * k, y});
            bool hovered = !m_dragging && !g_overlayOpen && mouse.y > m_carouselBottom && mouse.y < m_carouselTop
                && containsWorld(p.root, mouse);
            if (hovered != p.hovered) {
                p.hovered = hovered;
                p.hover.to(hovered ? 1.f : 0.f, hovered ? 100 : 500, Easing::OutQuint);
                if (hovered) sfx::hover(sfx::sound::DEFAULT_HOVER);
            }
            p.active.update(dt);
            p.hover.update(dt);
            p.expand.update(dt);
            if (p.chevron) p.chevron->setRotation(-90.f * (1.f - p.expand.get()));
            p.bg->setFillColor(theme::lerp(p.base, PANEL_HOVER, p.hover.get()));
            auto pack = packOf(e);
            auto accent = e.packHeader && pack ? pack->barColor : levels::difficultyColor(e.difficulty);
            p.bg->setBorder(2.5f * k * p.active.get(), {accent.r, accent.g, accent.b, static_cast<GLubyte>(255 * p.active.get())});

            // Thumbnail, once the panel has settled in view.
            p.visibleMs += ms;
            if (!p.thumbRequested && p.visibleMs > THUMB_DELAY) {
                p.thumbRequested = true;
                auto onTexture = [](Ref<RoundedBox> box) {
                    return [box](CCTexture2D* texture) {
                        if (!texture || !box->getParent()) return;
                        box->setTexture(texture);
                        box->setVisible(true);
                        box->setUserObject("loaded"_spr, CCBool::create(true));
                    };
                };
                // Its panel left the view before its turn came: skip it.
                auto stillWanted = [](Ref<RoundedBox> box) {
                    return [box] { return box->getParent() != nullptr; };
                };
                if (p.thumb) levelThumbnail(e, packList(), onTexture(p.thumb), stillWanted(p.thumb));
                for (auto& t : p.tiles) thumbnails::fetch(t.levelID, onTexture(t.box), stillWanted(t.box));
            }
            if (p.thumb) {
                if (p.thumb->getUserObject("loaded"_spr) && p.thumbAlpha.target() < 1.f) p.thumbAlpha.to(1.f, 300, Easing::OutQuint);
                p.thumbAlpha.update(dt);
                p.thumb->setOpacity(static_cast<GLubyte>(p.thumbAlpha.get() * 150));
            }
            for (auto& t : p.tiles) {
                if (t.box->getUserObject("loaded"_spr) && t.alpha.target() < 1.f) t.alpha.to(1.f, 300, Easing::OutQuint);
                t.alpha.update(dt);
                t.box->setOpacity(static_cast<GLubyte>(t.alpha.get() * 150));
            }
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

std::pair<float, float> SongSelect::scrollRange() const {
    float halfH = viewHeight() / 2;
    if (m_visible.empty()) return {m_panelH / 2 - halfH, m_panelH / 2 - halfH};
    size_t last = m_visible.size() - 1;
    return {rowHeight(0) / 2 - halfH, itemTop(last) + rowHeight(last) / 2 - halfH};
}

// Length from the visible share of the list (at least three widths, like
// osu!), position from the scroll. Grey, white on hover, highlight while held.
void SongSelect::updateScrollbar(float dt) {
    float k = m_k;
    auto [minScroll, maxScroll] = scrollRange();
    float range = maxScroll - minScroll;
    float viewH = viewHeight();
    m_bar->setVisible(range > 1.f);
    if (range <= 1.f) return;

    m_barLength = std::max(SCROLLBAR_WIDTH * 3 * k, viewH * viewH / (range + viewH));
    float t = std::clamp((m_scroll - minScroll) / range, 0.f, 1.f);
    m_barY = m_carouselTop - m_barLength / 2 - t * (viewH - m_barLength);
    m_barWidth.update(dt);
    m_barPull.update(dt);
    float width = SCROLLBAR_WIDTH * k * m_barWidth.get();
    float right = m_win.width - SCROLLBAR_MARGIN * k + m_barPull.get();
    m_bar->setContentSize({width, m_barLength});
    m_bar->setRadius(width / 2);
    m_bar->setPosition({right, m_barY});

    // Label: follows the bar while it's held.
    m_barLabelAlpha.update(dt);
    float labelAlpha = m_barLabelAlpha.get();
    m_barLabel->setVisible(labelAlpha > 0.01f);
    if (m_barDragging) {
        auto text = scrollbarText();
        if (text != m_barText) {
            m_barText = text;
            m_barLabelText->setString(text.c_str());
            float padX = 14 * k;
            float textW = m_barLabelText->getScaledContentSize().width;
            m_barLabelBg->setContentSize({std::max(textW + padX * 2, 44 * k), 44 * k});
            m_barLabelText->setPosition({-padX, 0});
        }
    }
    if (m_barLabel->isVisible()) {
        // Slides out from the bar as it fades in.
        float gap = SCROLLBAR_LABEL_GAP * k * (0.6f + 0.4f * labelAlpha);
        float y = std::clamp(m_barY, m_carouselBottom + 22 * k, m_carouselTop - 22 * k);
        m_barLabel->setPosition({right - width - gap, y});
        m_barLabelBg->setOpacity(static_cast<GLubyte>(labelAlpha * 255));
        m_barLabelText->setOpacity(static_cast<GLubyte>(labelAlpha * 255));
    }

    auto mouse = geode::cocos::getMousePos();
    auto local = m_bar->convertToNodeSpace(mouse);
    auto size = m_bar->getContentSize();
    bool hovered = local.x >= 0 && local.y >= 0 && local.x <= size.width && local.y <= size.height;
    if (hovered != m_barHovered) {
        m_barHovered = hovered;
        m_barHover.to(hovered ? 1.f : 0.f, 100, Easing::None);
    }
    m_barHover.update(dt);
    m_barHighlight.update(dt);
    auto grey = static_cast<GLubyte>(136 + (255 - 136) * m_barHover.get());
    ccColor4B colour {grey, grey, grey, 255};
    m_bar->setFillColor(theme::lerp(colour, theme::HIGHLIGHT1, m_barHighlight.get()));
}

bool SongSelect::scrollbarHit(CCPoint world) const {
    if (!m_bar->isVisible()) return false;
    return world.x > m_win.width - SCROLLBAR_HIT_WIDTH * m_k
        && world.y > m_carouselBottom && world.y < m_carouselTop;
}

std::string SongSelect::scrollbarText() const {
    if (m_visible.empty()) return "";
    size_t index;
    if (m_rowTops.size() == m_visible.size() + 1) {
        auto it = std::upper_bound(m_rowTops.begin(), m_rowTops.end() - 1, m_scroll + viewHeight() / 2);
        index = static_cast<size_t>(std::max<std::ptrdiff_t>(0, (it - m_rowTops.begin()) - 1));
        index = std::min(index, m_visible.size() - 1);
    } else {
        float step = m_panelH + m_spacing;
        float middle = m_scroll + viewHeight() / 2 - m_panelH / 2;
        index = static_cast<size_t>(std::clamp(std::round(middle / step), 0.f, float(m_visible.size() - 1)));
    }
    size_t entry = m_visible[index];
    if (packMode()) {
        // A pack's level counts as its pack, and packs go by name.
        while (entry > 0 && !m_entries[entry].packHeader) entry--;
        if (m_sort == levels::Sort::Default) return m_entries[entry].name;
    }
    auto const& e = m_entries[entry];
    switch (m_sort) {
        case levels::Sort::Title:
            for (char c : e.name) {
                auto u = static_cast<unsigned char>(c);
                if (std::isalpha(u)) return std::string(1, static_cast<char>(std::toupper(u)));
                if (std::isdigit(u)) return "#";
            }
            return "?";
        case levels::Sort::Difficulty: return levels::difficultyName(e.difficulty);
        case levels::Sort::Progress:
            if (e.platformer) return e.normalPercent >= 100 ? "completed" : "not completed";
            return fmt::format("{}%", e.normalPercent);
        default: return fmt::format("{} / {}", index + 1, m_visible.size());
    }
}

// Moves the bar's centre to the touch's y - grab, and the list with it; the
// touch's x pulls the bar sideways.
void SongSelect::dragScrollbar(CCPoint touch) {
    float k = m_k;
    float dx = touch.x - (m_win.width - SCROLLBAR_MARGIN * k - SCROLLBAR_WIDTH * k / 2);
    float max = SCROLLBAR_PULL_MAX * k;
    m_barPull.set(std::clamp(dx * SCROLLBAR_PULL_FOLLOW, -max, 0.f));

    auto [minScroll, maxScroll] = scrollRange();
    float travel = viewHeight() - m_barLength;
    if (travel <= 0) return;
    float centre = std::clamp(touch.y - m_barGrab, m_carouselBottom + m_barLength / 2, m_carouselTop - m_barLength / 2);
    float t = (m_carouselTop - m_barLength / 2 - centre) / travel;
    m_scrollTarget = minScroll + t * (maxScroll - minScroll);
}

} // namespace lazer
