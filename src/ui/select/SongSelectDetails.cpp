#include "SongSelectInternal.hpp"

#include "../../audio/Sfx.hpp"
#include "../core/Theme.hpp"
#include "../overlays/CommentsOverlay.hpp"

#include <Geode/utils/base64.hpp>
#include <algorithm>

using namespace geode::prelude;

namespace lazer {

// --- left: title wedge + details ---

void SongSelect::updateWedge(bool animate) {
    // Rebuilt from scratch: drop the old level's buttons (and any press on them).
    if (m_pressed >= m_wedgeButtons.data() && m_pressed < m_wedgeButtons.data() + m_wedgeButtons.size()) m_pressed = nullptr;
    m_wedgeButtons.clear();
    m_details = nullptr;
    m_songWidget = nullptr;
    m_loadingSpinner = nullptr;
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
        // A fruitless search gets one of a few lines, held still by the text.
        std::string text = m_query.empty() ? "no levels match your search"
                                           : NO_RESULTS_LINES[pickLine(m_query, NO_RESULTS_LINES.size())];
        bool retry = false;
        bool spin = false;
        if (onlineMode()) {
            auto const& r = browse::results();
            if (r.state == browse::State::Loading) {
                text = m_onlineLists ? "loading lists..." : m_query.empty() ? "loading levels..." : "searching...";
                spin = true;
            } else if (r.state == browse::State::Failed) {
                text = "couldn't reach GD's servers";
                retry = true;
            } else if (m_query.empty()) text = m_onlineLists ? "no lists here" : "no levels here";
        } else if (packMode()) {
            auto state = packListState();
            std::string what = gauntletMode() ? "gauntlets" : "map packs";
            if (state == packs::State::Loading || state == packs::State::Unloaded) {
                text = "loading " + what + "...";
                spin = true;
            } else if (state == packs::State::Failed) {
                text = "couldn't load the " + what;
                retry = true;
            } else if (!m_query.empty() && packLevelsLoading()) {
                text = fmt::format("searching their levels... {}%", static_cast<int>(packLevelsProgress() * 100));
                spin = true;
            } else if (m_query.empty() && m_group == Group::Liked) text = "no " + what + " completed yet";
            else if (m_query.empty() && m_group == Group::Official) text = gauntletMode() ? "every gauntlet is complete!" : "every map pack is complete!";
        } else if (m_entries.empty()) text = "no levels yet";
        else if (m_query.empty() && m_group == Group::Liked) text = "no hearted levels yet";
        else if (m_query.empty() && m_folder != 0) text = "nothing in this folder";
        auto none = makeText(text, Weight::SemiBold, 26 * k);
        none->setColor(theme::LIGHT1);
        none->setAnchorPoint({0, 0.5f});
        none->setPosition({40 * k, H - 60 * k});
        m_wedge->addChild(none);
        if (spin) {
            // A spinner beside it (turned in update()), on the glyph's own centre.
            auto holder = CCNode::create();
            auto glyph = makeIcon(icon::ROTATE, 22 * k);
            glyph->setColor(theme::LIGHT1);
            glyph->setAnchorPoint({0, 0});
            if (auto letter = glyph->getChildByType<CCSprite>(0)) glyph->setPosition(-letter->getPosition() * glyph->getScale());
            holder->addChild(glyph);
            holder->setPosition({40 * k + none->getScaledContentSize().width + 22 * k, H - 60 * k});
            m_wedge->addChild(holder);
            m_loadingSpinner = holder;
        }
        if (m_group == Group::Liked && m_query.empty() && !onlineMode()) {
            auto hint = makeText("heart a level with the heart next to its title", Weight::Regular, 17 * k);
            hint->setColor(theme::CONTENT2);
            hint->setAnchorPoint({0, 0.5f});
            hint->setPosition({40 * k, H - 94 * k});
            m_wedge->addChild(hint);
        }
        if (retry) {
            addButton(m_wedgeButtons, m_wedge, icon::ROTATE, "try again", {40 * k, H - 104 * k}, 30 * k, theme::COLOUR3, [this] {
                if (timelyMode()) timely::retry(timelyType());
                loadPackList();
                this->updateWedge(false);
            }, 0);
        }
        if (!m_query.empty() && !retry && !spin) {
            // osu!'s NoResultsPlaceholder: clear the search, or search online for it.
            std::string query = m_query.size() > 24 ? m_query.substr(0, 22) + "..." : m_query;
            float y = H - 104 * k;
            if (!packMode() && !onlineMode()) {
                addButton(m_wedgeButtons, m_wedge, icon::GLOBE, fmt::format("search online for \"{}\"", query), {40 * k, y},
                          30 * k, theme::COLOUR3, [this] { this->browseOnline(); }, 0);
                y -= 40 * k;
            }
            addButton(m_wedgeButtons, m_wedge, icon::XMARK, "clear search", {40 * k, y}, 30 * k, TAB, [this] {
                m_query.clear();
                if (m_search) m_search->setString("");
                if (onlineMode()) {
                    m_request.query.clear();
                    applyOnlineRequest();
                    return;
                }
                remembered().query.clear();
                applyFilter();
            }, 0);
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
    float titleW = e.official ? maxW : maxW - 112 * k;
    auto pack = packOf(e);
    auto title = makeText(e.name, Weight::SemiBold, 36 * k);
    if (e.packHeader && pack) title->setColor(pack->textColor);
    title->setAnchorPoint({0, 0.5f});
    title->setPosition({x0, H - 38 * k});
    fit(title, titleW);
    m_wedge->addChild(title, 1);

    // Heart (GD's favourite) and delete: saved levels only, like the level page
    // (online, the ones you have a saved copy of; nothing to delete from there).
    // GD's own copies of a daily or a gauntlet's level aren't yours to keep.
    bool saved = !onlineMode() || levels::isSaved(e.level.data());
    if (!e.official && e.pack < 0 && saved && !levels::specialCopy(e.level.data())) {
        float iconH = 30 * k;
        CCSize size {iconH * 1.4f, iconH};
        // Icon only: a small square-ish pill, icon centred.
        auto iconButton = [&](char const* glyph, float x, ccColor4B color, std::function<void()> action) {
            auto& b = addButton(m_wedgeButtons, m_wedge, glyph, "", {x, H - 38 * k}, iconH, color, std::move(action), 0);
            b.node->setContentSize(size);
            b.bg->setContentSize(size);
            for (auto child : CCArrayExt<CCNode*>(b.node->getChildren())) {
                if (child == b.bg) continue;
                child->setAnchorPoint({0.5f, 0.5f});
                child->setPosition(size / 2);
            }
        };
        float x = x0 + title->getScaledContentSize().width + 14 * k;
        size_t index = m_wedgeButtons.size();
        iconButton(icon::HEART, x, levels::favorited(e) ? PINK : TAB, [this, index] {
            if (!m_hasSelection || index >= m_wedgeButtons.size()) return;
            auto const& entry = m_entries[m_visible[m_selected]];
            bool on = !levels::favorited(entry);
            levels::setFavorited(entry, on);
            m_wedgeButtons[index].color = on ? PINK : TAB;
            sfx::play(on ? sfx::sound::CHECK_ON : sfx::sound::CHECK_OFF);
        });
        if (!onlineMode()) iconButton(icon::TRASH, x + size.width + 8 * k, TAB, [this] { this->confirmDeleteLevel(); });
    }

    std::vector<std::pair<char const*, std::string>> line;
    bool list = pack && pack->list;
    if (e.packHeader) {
        int total = pack ? static_cast<int>(pack->levelIDs.size()) : 0;
        line.push_back({list ? icon::LAYERS : icon::BOXES,
                        fmt::format("{}  -  {} level{}", list ? "level list" : "map pack", total, total == 1 ? "" : "s")});
    } else {
        line.push_back({icon::MUSIC, e.songArtist.empty() ? e.songTitle : e.songTitle + "  -  " + e.songArtist});
        if (pack) line.push_back({list ? icon::LAYERS : pack->gauntlet ? icon::DUNGEON : icon::BOXES, pack->name});
    }
    auto song = infoRow(line, 17 * k, theme::CONTENT2);
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
    CCNode* face = nullptr;
    if (e.packHeader && pack && pack->gauntlet && !pack->frame.empty()) {
        // A gauntlet: GD's badge for it in place of a difficulty.
        if (auto badge = CCSprite::createWithSpriteFrameName(pack->frame.c_str())) {
            auto size = badge->getContentSize();
            if (size.width > 0 && size.height > 0) badge->setScale(40 * k / std::max(size.width, size.height));
            face = badge;
        }
    }
    if (!face) face = difficultyFace(e, 34 * k);
    face->setPosition({x0 + 17 * k, statsY});
    m_wedge->addChild(face, 1);
    std::vector<std::pair<char const*, std::string>> stats;
    if (e.packHeader) {
        // The pack's reward (a list's diamonds), and how far along it is.
        int total = pack ? static_cast<int>(pack->levelIDs.size()) : 0;
        int done = pack ? std::min(pack->completed, total) : 0;
        if (e.stars > 0) stats.push_back({list ? icon::GEM : icon::STAR, "+" + std::to_string(e.stars)});
        if (e.coins > 0) stats.push_back({icon::COINS, "+" + std::to_string(e.coins)});
        if (pack && pack->gauntlet) stats.push_back({icon::GIFT, pack->claimed ? "chest opened" : "a chest"});
        stats.push_back({total > 0 && done >= total ? icon::CHECK : nullptr, fmt::format("{}/{} done", done, total)});
        if (list) {
            stats.push_back({icon::CLOUD_DOWN, std::to_string(pack->downloads)});
            stats.push_back({icon::THUMBS_UP, std::to_string(pack->likes)});
            stats.push_back({icon::ID_CARD, std::to_string(e.id)});
        }
    } else {
        if (e.stars > 0) stats.push_back({rewardIcon(e), std::to_string(e.stars)});
        if (!e.platformer) stats.push_back({icon::CLOCK, levels::lengthName(e.length)});
        if (e.coins > 0) stats.push_back({icon::COINS, fmt::format("{}/{}", e.coinsCollected, e.coins)});
        if (e.dailyID > 0) {
            auto type = levels::timedTypeOf(e.dailyID);
            char const* glyph = type == GJTimedLevelType::Weekly ? icon::CALENDAR_WEEK
                              : type == GJTimedLevelType::Event ? icon::BOLT : icon::CALENDAR_DAY;
            stats.push_back({glyph, fmt::format("{} #{}", levels::timelyName(type), levels::timelyNumber(e.dailyID))});
        }
        if (e.locked) stats.push_back({icon::LOCK, "locked"});
        if (!e.official) stats.push_back({icon::ID_CARD, std::to_string(e.id)});
    }
    auto statsRow = infoRow(stats, 17 * k, theme::CONTENT1);
    statsRow->setPosition({x0 + 44 * k, statsY});
    if (statsRow->getContentSize().width > maxW - 44 * k) statsRow->setScale((maxW - 44 * k) / statsRow->getContentSize().width);
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

    // A pack: each level's state, and the reward for finishing them all.
    auto pack = packOf(e);
    bool list = pack && pack->list;
    if (e.packHeader) {
        if (pack) {
            int index = e.pack;
            int total = static_cast<int>(pack->levelIDs.size());
            int done = std::min(pack->completed, total);
            section("progress");
            bar("levels", total > 0 ? done * 100 / total : 0, pack->barColor);
            if (pack->state == packs::State::Loaded) {
                for (auto const& l : pack->levels) {
                    bool beaten = l.normalPercent >= 100;
                    auto row = infoRow({{beaten ? icon::CHECK : l.locked ? icon::LOCK : icon::XMARK, l.name},
                                        {nullptr, l.locked ? "locked" : fmt::format("{}%", l.normalPercent)}},
                                       15 * k, beaten ? theme::CONTENT1 : l.locked ? theme::LIGHT1 : theme::CONTENT2);
                    if (row->getContentSize().width > maxW) row->setScale(maxW / row->getContentSize().width);
                    add(row, 0, y);
                    y -= 24 * k;
                }
                y -= 6 * k;
            } else if (pack->state == packs::State::Failed) {
                auto label = makeText(list ? "couldn't load the list's levels" : "couldn't load the pack's levels", Weight::Regular, 15 * k);
                label->setColor(theme::CONTENT2);
                label->setAnchorPoint({0, 0.5f});
                add(label, 0, y);
                y -= 34 * k;
                button(icon::ROTATE, "try again", TAB, [this, index] {
                    this->loadPackLevels(index);
                    this->refreshDetails();
                });
                y -= 40 * k;
            } else if (pack->state == packs::State::Loading || e.pack == m_expandedPack) {
                auto label = makeText(list ? "loading the list's levels..." : "loading the pack's levels...", Weight::Regular, 15 * k);
                label->setColor(theme::CONTENT2);
                label->setAnchorPoint({0, 0.5f});
                add(label, 0, y);
                y -= 30 * k;
            }

            section("reward");
            std::vector<std::pair<char const*, std::string>> reward;
            if (list) {
                if (pack->diamonds > 0) reward.push_back({icon::GEM, fmt::format("{} diamond{}", pack->diamonds, pack->diamonds == 1 ? "" : "s")});
            } else if (pack->gauntlet) {
                reward.push_back({icon::GIFT, "a chest"});
            } else {
                if (pack->stars > 0) reward.push_back({icon::STAR, fmt::format("{} star{}", pack->stars, pack->stars == 1 ? "" : "s")});
                if (pack->coins > 0) reward.push_back({icon::COINS, fmt::format("{} coin{}", pack->coins, pack->coins == 1 ? "" : "s")});
            }
            if (reward.empty()) reward.push_back({nullptr, "none"});
            add(infoRow(reward, 15 * k, theme::CONTENT1), 0, y);
            y -= 30 * k;
            if (canClaimPack(*pack)) {
                button(icon::GIFT, pack->gauntlet ? "open the chest" : "claim reward", theme::COLOUR3, [this, index] { this->claimPack(index); });
                y -= 40 * k;
            } else {
                std::string note;
                if (list) {
                    if (pack->diamonds > 0) {
                        int need = std::max(1, pack->levelsToClaim);
                        note = fmt::format("beat {} of its levels, then claim it on GD's list page", need);
                    }
                } else if (pack->gauntlet) {
                    note = pack->claimed ? "chest opened" : "beat its five levels, in order, to open the chest";
                } else note = pack->claimed ? "claimed" : "beat every level in the pack to claim it";
                if (!note.empty()) {
                    auto label = makeWrappedText(note, 15 * k, maxW, theme::LIGHT1);
                    add(label, 0, y + 9 * k);
                    y -= label->getScaledContentSize().height + 16 * k;
                }
            }
            if (e.pack != m_expandedPack) {
                button(icon::CHEVRON_DOWN, "show levels", TAB, [this, index] { this->expandPack(index); });
                y -= 40 * k;
            }
            if (list) {
                std::string desc = pack->list->getUnpackedDescription();
                if (!desc.empty()) {
                    section("description");
                    auto text = makeWrappedText(desc, 15 * k, maxW, theme::CONTENT2);
                    add(text, 0, y + 9 * k);
                    y -= text->getScaledContentSize().height + 16 * k;
                }
            }
        }
        m_details->setContentHeight(-y + 10 * k);
        return;
    }
    if (pack) {
        // One of a pack's (or a list's, a gauntlet's) levels: its progress on top.
        int index = e.pack;
        int total = static_cast<int>(pack->levelIDs.size());
        int done = std::min(pack->completed, total);
        section(list ? "list" : pack->gauntlet ? "gauntlet" : "map pack");
        auto name = makeText(pack->name, Weight::SemiBold, 15 * k);
        name->setColor(pack->textColor);
        name->setAnchorPoint({0, 0.5f});
        fit(name, maxW);
        add(name, 0, y);
        y -= 26 * k;
        bar("levels", total > 0 ? done * 100 / total : 0, pack->barColor);
        if (e.locked) {
            // A gauntlet's levels open in order.
            std::string before;
            for (size_t i = 1; i < pack->levels.size(); i++) {
                if (pack->levels[i].id == e.id) before = pack->levels[i - 1].name;
            }
            auto note = makeWrappedText(before.empty() ? "locked: beat the level before it first" : "locked: beat " + before + " first",
                                        15 * k, maxW, theme::LIGHT1);
            add(note, 0, y + 9 * k);
            y -= note->getScaledContentSize().height + 16 * k;
        }
        if (canClaimPack(*pack)) {
            button(icon::GIFT, pack->gauntlet ? "open the chest" : "claim reward", theme::COLOUR3, [this, index] { this->claimPack(index); });
            y -= 40 * k;
        }
        y -= 8 * k;
    }

    // A daily (weekly, event) level: the current one with its state, or one
    // from the safe. Either is GD's own copy of the level: beating it counts
    // as beating that daily, apart from the level played on its own.
    if (e.dailyID > 0) {
        auto type = levels::timedTypeOf(e.dailyID);
        auto name = levels::timelyName(type);
        bool current = timelyMode() && m_timelyRow && m_visible[m_selected] == 0;
        section(fmt::format("{} #{}", name, levels::timelyNumber(e.dailyID)).c_str());
        if (current) {
            std::string state;
            if (m_timely.downloading) state = "the level is on its way";
            else if (m_timely.claimable) state = "beaten! its reward is waiting";
            else if (m_timely.completed) state = "beaten, reward claimed";
            else state = fmt::format("the current {} level: beat it for its reward", name);
            auto label = makeWrappedText(state, 15 * k, maxW, theme::CONTENT1);
            add(label, 0, y + 9 * k);
            y -= label->getScaledContentSize().height + 16 * k;
            if (m_timely.claimable) {
                button(icon::GIFT, "claim reward", theme::COLOUR3, [this] { this->claimTimely(); });
                y -= 40 * k;
            }
            if (m_timely.skippable) {
                button(icon::CHEVRON_RIGHT, fmt::format("skip to the new {} level", name), TAB, [this] { this->skipTimely(); });
                y -= 40 * k;
            }
        } else {
            auto label = makeWrappedText(fmt::format("from the safe: beating it counts as {} #{}, kept apart from the level on its own",
                                                     name, levels::timelyNumber(e.dailyID)), 15 * k, maxW, theme::LIGHT1);
            add(label, 0, y + 9 * k);
            y -= label->getScaledContentSize().height + 16 * k;
        }
    }

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

    // Comments: the osu!-style page. RobTop's levels have none online.
    if (!e.official) {
        section("comments");
        button(icon::COMMENTS, "show comments", TAB, [this] { this->openComments(); });
        y -= 40 * k;
    }

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
    if (!m_hasSelection || m_starting) return;
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
    auto level = m_entries[m_visible[m_selected]].level;
    return level ? level->m_songID : 0;
}

void SongSelect::loadLeaderboard() {
    if (!m_hasSelection) return;
    auto const& e = m_entries[m_visible[m_selected]];
    if (e.official || !e.level) return;
    m_board = Board::Loading;
    m_boardLevel = e.id;
    m_boardScores = nullptr;
    refreshDetails();
    auto glm = GameLevelManager::sharedState();
    glm->m_leaderboardManagerDelegate = this;
    glm->getLevelLeaderboard(e.level, LevelLeaderboardType::Global,
                             e.platformer ? LevelLeaderboardMode::Time : LevelLeaderboardMode::Time);
}

void SongSelect::openComments() {
    if (!m_hasSelection || m_starting) return;
    auto const& e = m_entries[m_visible[m_selected]];
    if (e.official || !e.level) return;
    closeMenu();
    CommentsOverlay::present(e.level);
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

} // namespace lazer
