#include "SongSelectInternal.hpp"

#include "../../audio/Sfx.hpp"
#include "../core/MenuCursor.hpp"
#include "../overlays/Dialog.hpp"

#include <algorithm>
#include <unordered_map>

using namespace geode::prelude;

namespace lazer {

// --- data ---

void SongSelect::applyFilter() {
    int keepId = -1;
    bool keepOfficial = false, keepHeader = false;
    if (m_hasSelection && m_selected < m_visible.size() && m_visible[m_selected] < m_entries.size()) {
        auto const& e = m_entries[m_visible[m_selected]];
        keepId = e.id;
        keepOfficial = e.official;
        keepHeader = e.packHeader;
    } else {
        keepId = remembered().selectedId;
        keepOfficial = remembered().selectedOfficial;
        keepHeader = remembered().selectedHeader;
    }

    // Online, GD's servers did the searching: every row shows.
    std::string query = onlineMode() ? std::string() : lower(m_query);
    m_visible.clear();
    for (size_t i = 0; i < m_entries.size(); i++) {
        auto const& e = m_entries[i];
        if (packMode()) {
            // The packs are the rows; the open one's levels (and, searching,
            // the matching ones) go under them below.
            if (!e.packHeader) continue;
            if (onlineMode()) {
                m_visible.push_back(i);
                continue;
            }
            auto pack = packOf(e);
            bool done = pack && !pack->levelIDs.empty() && pack->completed >= static_cast<int>(pack->levelIDs.size());
            if (m_group == Group::Official && done) continue; // unfinished
            if (m_group == Group::Liked && !done) continue;   // completed
            if (!query.empty() && e.search.find(query) == std::string::npos) {
                // By a level's name or creator, once the levels are known.
                bool byLevel = pack && std::any_of(pack->levels.begin(), pack->levels.end(), [&](auto const& l) {
                    return l.search.find(query) != std::string::npos;
                });
                if (!byLevel) continue;
            }
            m_visible.push_back(i);
            continue;
        }
        if (onlineMode()) {
            m_visible.push_back(i);
            continue;
        }
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
    if (packMode()) {
        // Under each header: the open pack's levels, and while searching the
        // levels that match, in the pack's order. Searching needs every
        // pack's levels: fetched now, a few packs at a time.
        if (!onlineMode() && !query.empty() && packLevelsProgress() < 1.f) loadAllPackLevels();
        std::vector<size_t> rows;
        for (size_t v : m_visible) {
            rows.push_back(v);
            auto const& h = m_entries[v];
            for (size_t j = v + 1; j < m_entries.size(); j++) {
                auto const& l = m_entries[j];
                if (l.packHeader || l.pack != h.pack) break;
                bool open = h.pack == m_expandedPack;
                bool hit = !query.empty() && l.search.find(query) != std::string::npos;
                if (open || hit) rows.push_back(j);
            }
        }
        m_visible = std::move(rows);
    }
    layoutRows();

    // Indices changed: panels still in the list move to their new index (no
    // fade in again while typing), the rest go.
    std::unordered_map<size_t, Panel> byEntry;
    for (auto& [index, panel] : m_panels) byEntry.emplace(panel.entry, std::move(panel));
    m_panels.clear();
    for (size_t i = 0; i < m_visible.size() && !byEntry.empty(); i++) {
        auto it = byEntry.find(m_visible[i]);
        if (it == byEntry.end()) continue;
        m_panels.emplace(i, std::move(it->second));
        byEntry.erase(it);
    }
    for (auto& [entry, panel] : byEntry) panel.root->removeFromParent();

    if (!onlineMode()) {
        for (size_t i = 0; i < m_tabs.size(); i++) m_tabs[i].selected = static_cast<int>(m_group) == static_cast<int>(i);
    }
    // RobTop's levels aren't in folders (and packs aren't levels).
    if (m_folderButton < m_buttons.size()) {
        auto& folderButton = m_buttons[m_folderButton];
        folderButton.node->setVisible(m_group != Group::Official && !packMode());
        folderButton.selected = m_folder != 0;
        if (m_folderLabel) {
            m_folderLabel->setString(m_folder == 0 ? "all folders" : levels::folderName(m_folder).c_str());
            fitLabel(m_folderLabel, folderButton.node);
        }
    }
    static char const* SORT_NAMES[] = {"sort: default", "sort: title", "sort: difficulty", "sort: progress"};
    if (m_sortLabel && !onlineMode()) {
        m_sortLabel->setString(SORT_NAMES[static_cast<int>(m_sort)]);
        fitLabel(m_sortLabel, m_sortLabel->getParent());
    }
    if (onlineMode()) updateOnlineLabels();
    else if (m_countLabel) {
        if (packMode()) {
            size_t n = std::count_if(m_visible.begin(), m_visible.end(), [&](size_t i) { return m_entries[i].packHeader; });
            std::string text = fmt::format("{} {}{}", n, packWord(), n == 1 ? "" : "s");
            if (packListState() == packs::State::Loading) text += " so far";
            else if (!query.empty() && packLevelsLoading()) {
                text = fmt::format("searching levels... {}%", static_cast<int>(packLevelsProgress() * 100));
            }
            m_countLabel->setString(text.c_str());
        } else {
            m_countLabel->setString(fmt::format("{} {} level{}", m_visible.size(),
                m_kind == levels::Kind::Platformer ? "platformer" : "classic", m_visible.size() == 1 ? "" : "s").c_str());
        }
    }

    if (m_visible.empty()) {
        m_hasSelection = false;
        m_lastSelection = {};
        updateWedge();
        return;
    }
    size_t index = 0;
    bool found = false;
    for (size_t i = 0; i < m_visible.size(); i++) {
        auto const& e = m_entries[m_visible[i]];
        if (e.id == keepId && e.official == keepOfficial && e.packHeader == keepHeader) {
            index = i;
            found = true;
            break;
        }
    }
    // Gone (a pack closed on its level): its header, if it's showing.
    if (!found && packMode() && m_expandedPack >= 0) {
        for (size_t i = 0; i < m_visible.size(); i++) {
            auto const& e = m_entries[m_visible[i]];
            if (e.packHeader && e.pack == m_expandedPack) {
                index = i;
                break;
            }
        }
    }
    m_hasSelection = false; // force the selection to refresh
    select(index);
}

void SongSelect::reloadEntries() {
    // Entry indices change: no panel can be reused.
    for (auto& [index, panel] : m_panels) panel.root->removeFromParent();
    m_panels.clear();
    if (onlineMode()) {
        browse::refreshProgress();
        rebuildOnlineEntries();
        m_hasSelection = false;
    } else if (packMode()) {
        if (gauntletMode()) for (auto& p : gauntlets::all()) gauntlets::refresh(p);
        else for (auto& p : packs::all()) packs::refresh(p);
        rebuildPackEntries();
        m_hasSelection = false; // the old rows are gone: found again by ID
    } else {
        m_entries = levels::all(m_kind);
    }
    applyFilter();
}

// --- map packs ---

void SongSelect::rebuildPackEntries() {
    // Panels keep going across the rebuild: each one's entry is found again
    // (a pack's header by pack, a level by ID), so nothing fades in twice.
    struct Key { bool header; int pack; int id; };
    std::unordered_map<size_t, Key> keys;
    for (auto& [index, panel] : m_panels) {
        if (panel.entry < m_entries.size()) {
            auto const& e = m_entries[panel.entry];
            keys.emplace(index, Key {e.packHeader, e.pack, e.id});
        }
    }
    m_entries.clear();
    auto& packs = packList();
    for (size_t i = 0; i < packs.size(); i++) {
        auto const& p = packs[i];
        levels::Entry h;
        h.packHeader = true;
        h.pack = static_cast<int>(i);
        h.id = p.id;
        h.name = p.name;
        h.creator = p.list ? p.creator : "RobTop";
        h.difficulty = p.difficulty;
        h.gauntlet = p.gauntlet;
        // A list's reward is in diamonds.
        h.stars = p.list ? p.diamonds : p.stars;
        h.coins = p.coins;
        h.coinsVerified = true;
        int total = static_cast<int>(p.levelIDs.size());
        h.normalPercent = total > 0 ? p.completed * 100 / total : 0;
        h.search = p.search;
        h.resolved = true;
        m_entries.push_back(std::move(h));
        for (auto const& level : p.levels) {
            auto e = level;
            e.pack = static_cast<int>(i);
            m_entries.push_back(std::move(e));
        }
    }
    for (auto it = m_panels.begin(); it != m_panels.end();) {
        auto key = keys.find(it->first);
        size_t found = SIZE_MAX;
        if (key != keys.end()) {
            for (size_t j = 0; j < m_entries.size(); j++) {
                auto const& e = m_entries[j];
                if (e.packHeader == key->second.header && e.pack == key->second.pack && e.id == key->second.id) {
                    found = j;
                    break;
                }
            }
        }
        if (found == SIZE_MAX) {
            it->second.root->removeFromParent();
            it = m_panels.erase(it);
        } else {
            it->second.entry = found;
            ++it;
        }
    }
}

void SongSelect::onPacksChanged() {
    if (!packMode() || onlineMode() || m_starting) return;
    // The list comes a page at a time: it shows once it's all here, so the
    // screen doesn't build and fade in again with every page.
    if (packListState() == packs::State::Loading) {
        if (m_countLabel) {
            size_t n = packList().size();
            m_countLabel->setString(fmt::format("{} {}{} so far", n, packWord(), n == 1 ? "" : "s").c_str());
        }
        return;
    }
    // The rows are rebuilt: the selection is found again by ID (select()
    // keeps it in remembered()), and its details are redrawn in place.
    rebuildPackEntries();
    m_hasSelection = false;
    applyFilter();
    refreshDetails();
    restoreExpandedPack();
    if (m_expandedPack >= 0 && m_expandedPack < static_cast<int>(packList().size())
        && packList()[m_expandedPack].state == packs::State::Failed) {
        cursorSay("the servers said no");
    }
}

void SongSelect::restoreExpandedPack() {
    if (!onlineMode() && packListState() != packs::State::Loaded) return;
    auto& packs = packList();
    if (m_expandedPack >= static_cast<int>(packs.size())) m_expandedPack = -1;
    if (m_expandedPack < 0) {
        // The pack open last time stays open.
        int wanted = remembered().expandedPack;
        for (size_t i = 0; i < packs.size() && wanted >= 0; i++) {
            if (packs[i].id != wanted) continue;
            m_expandedPack = static_cast<int>(i);
            m_hasSelection = false; // its levels go under it: the rows change
            applyFilter();
            break;
        }
        if (m_expandedPack < 0) return;
    }
    auto& p = packs[m_expandedPack];
    if (p.state == packs::State::Loaded) {
        // Its levels are here: on to the first one still to beat.
        if (auto e = selectedEntry(); e && e->packHeader && e->pack == m_expandedPack) selectPackLevel();
    } else if (p.state == packs::State::Unloaded) {
        // Not fetched yet (or its request had to wait for another pack's).
        loadPackLevels(m_expandedPack);
    }
}

void SongSelect::expandPack(int pack) {
    if (!packMode()) return;
    auto& packs = packList();
    if (pack >= static_cast<int>(packs.size())) pack = -1;
    int was = m_expandedPack;
    if (pack == was) return;
    m_expandedPack = pack;
    auto& r = remembered();
    r.expandedPack = pack >= 0 ? packs[pack].id : -1;
    // The selection moves to the pack's header (its levels follow), or to
    // the header of the pack that closed.
    int header = pack >= 0 ? pack : was;
    if (header >= 0) {
        r.selectedId = packs[header].id;
        r.selectedOfficial = false;
        r.selectedHeader = true;
    }
    m_hasSelection = false;
    applyFilter();
    if (pack < 0) return;
    auto& p = packs[pack];
    if (p.state == packs::State::Loaded) selectPackLevel();
    else if (p.state != packs::State::Loading) loadPackLevels(pack);
}

void SongSelect::selectPackLevel() {
    size_t first = SIZE_MAX;
    for (size_t v = 0; v < m_visible.size(); v++) {
        auto const& e = m_entries[m_visible[v]];
        if (e.packHeader || e.pack != m_expandedPack) continue;
        if (first == SIZE_MAX) first = v;
        if (e.normalPercent < 100) {
            first = v;
            break;
        }
    }
    if (first != SIZE_MAX) select(first);
}

void SongSelect::claimPack(int pack) {
    if (onlineMode()) return;
    auto& packs = packList();
    if (pack < 0 || pack >= static_cast<int>(packs.size()) || !canClaimPack(packs[pack])) return;
    auto& p = packs[pack];
    if (gauntletMode()) {
        // The chest: GD's own popup opens it and shows what was in it.
        gauntlets::claim(p);
        cursorSay("a chest! what's in it?");
    } else {
        packs::claim(p);
        sfx::play(sfx::sound::DIALOG_OK_SELECT);
        Dialog::show(icon::GIFT, "Map pack complete!",
            fmt::format("{} gave you {} star{} and {} coin{}.", p.name, p.stars, p.stars == 1 ? "" : "s",
                        p.coins, p.coins == 1 ? "" : "s"),
            {{"Nice", Dialog::Kind::Ok, nullptr}});
        cursorSay("free stars! well, earned.");
    }
    // The header's progress and its reward chip change.
    rebuildPackEntries();
    m_hasSelection = false;
    applyFilter();
    refreshDetails();
}

} // namespace lazer
