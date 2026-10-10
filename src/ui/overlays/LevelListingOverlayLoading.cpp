#include "LevelListingInternal.hpp"

#include "../../audio/Sfx.hpp"

#include <algorithm>

using namespace geode::prelude;

namespace lazer {

using namespace levellisting;

namespace {
    int itemID(CCObject* item) {
        if (auto level = typeinfo_cast<GJGameLevel*>(item)) return level->m_levelID.value();
        if (auto list = typeinfo_cast<GJLevelList*>(item)) return list->m_listID;
        return 0;
    }
}

// --- loading (through GD's browser) ---

void LevelListingOverlay::request(GJSearchObject* search, bool fresh) {
    if (!search || m_leaving) return;
    dropPrefetch();
    if (fresh) {
        m_total = -1;
        m_more = true;
        m_scroll->scrollTo(0);
        // The old cards stay, dimmed, until the new ones arrive: a page that
        // goes blank and fills again feels slower than one that changes.
        if (m_cards.empty() || local()) clearCards();
        else m_stale = true;
    }
    m_current = search;
    m_currentFresh = fresh;
    m_key = search->getKey();
    m_pendingTotal = m_pendingEnd = -1;
    m_state = State::Loading;
    m_loadingMs = 0;
    // GD's browser fetches the page and gets the result (see LevelListingOverlayHooks.cpp).
    // A cached page comes back before this returns.
    m_owner->loadPage(search);
    rebuildFooter();
}

void LevelListingOverlay::startSearch() {
    m_searchDelay = -1;
    if (m_online) request(onlineSearch(), true);
    else showLocal();
}

void LevelListingOverlay::queueSearch(float delayMs) {
    m_searchDelay = delayMs;
}

void LevelListingOverlay::loadMore() {
    if (m_state != State::Loaded || !m_more || !m_current) return;
    if (m_prefetch) {
        // The next page was asked for ahead of time: take it if it's here,
        // otherwise wait for it the usual way.
        auto next = m_prefetch;
        auto key = m_prefetchKey;
        int total = m_prefetchTotal, end = m_prefetchEnd;
        bool ready = m_prefetchReady, failed = m_prefetchFailed;
        Ref<CCArray> items = m_prefetched;
        dropPrefetch();
        if (ready && failed) return request(next, false);
        m_current = next;
        m_currentFresh = false;
        m_pendingTotal = total;
        m_pendingEnd = end;
        if (ready) {
            m_lastKey = key;
            ingest(items);
            return;
        }
        m_key = key;
        m_state = State::Loading;
        m_loadingMs = 0;
        rebuildFooter();
        return;
    }
    request(m_current->getNextPageObject(), false);
}

void LevelListingOverlay::prefetchNext() {
    dropPrefetch();
    if (!m_more || !m_current || local() || m_leaving) return;
    auto next = m_current->getNextPageObject();
    if (!next) return;
    m_prefetch = next;
    m_prefetchKey = next->getKey();
    // GD keeps the page once it arrives; asked for again, it comes straight back.
    m_owner->loadPage(next);
}

void LevelListingOverlay::dropPrefetch() {
    m_prefetch = nullptr;
    m_prefetchKey.clear();
    m_prefetched = nullptr;
    m_prefetchReady = m_prefetchFailed = false;
    m_prefetchTotal = m_prefetchEnd = -1;
}

void LevelListingOverlay::refresh() {
    if (m_state == State::Loading) return;
    // Something may have changed your levels since (another mod, a sync): read them again.
    if (local()) return showLocal();
    auto search = onlineSearch();
    if (!search) return;
    // GD keeps pages for a while; a refresh wants fresh ones.
    GameLevelManager::sharedState()->resetTimerForKey(search->getKey());
    request(search, true);
}

void LevelListingOverlay::levelsLoaded(CCArray* items, char const* key) {
    // Your levels on this device don't come through GD's browser at all.
    if (!key || local()) return;
    // The page asked for ahead of time: kept until it's scrolled to.
    if (!m_prefetchKey.empty() && m_prefetchKey == key && m_key != key) {
        m_prefetched = items;
        m_prefetchReady = true;
        m_prefetchFailed = false;
        return;
    }
    // Only the page asked for: a stale one (the filters changed mid-load) is dropped.
    if (m_key != key) return;
    m_lastKey = m_key;
    m_key.clear();
    if (m_stale) {
        clearCards();
        m_stale = false;
    }
    ingest(items);
}

void LevelListingOverlay::ingest(CCArray* items) {
    m_pageStart = m_cards.size();
    int count = items ? static_cast<int>(items->count()) : 0;
    int fresh = 0;
    // Your uploads can't be searched on the server: the search box filters them here.
    auto query = lower(m_query);
    if (items) {
        for (auto item : CCArrayExt<CCObject*>(items)) {
            int id = itemID(item);
            if (id && m_seen.contains(id)) continue; // pages can overlap, like osu-web's
            if (id) m_seen.insert(id);
            fresh++;
            auto level = typeinfo_cast<GJGameLevel*>(item);
            if (!query.empty() && level && lower(std::string(level->m_levelName)).find(query) == std::string::npos) continue;
            addCard(item);
        }
    }
    if (m_pendingTotal >= 0) m_total = m_pendingTotal;
    // Another page? GD says how far this one reached; failing that, a short page is the last.
    if (count == 0 || fresh == 0) m_more = false;
    else if (m_pendingTotal >= 0 && m_pendingEnd >= 0) m_more = m_pendingEnd < m_pendingTotal;
    else m_more = count >= LEVELS_PER_PAGE;
    m_pendingTotal = m_pendingEnd = -1;
    m_state = State::Loaded;
    rebuildFooter();
    prefetchNext();
}

void LevelListingOverlay::levelsFailed(char const* key) {
    if (!key) return;
    if (!m_prefetchKey.empty() && m_prefetchKey == key && m_key != key) {
        m_prefetchReady = m_prefetchFailed = true;
        return;
    }
    if (m_key != key) return;
    m_key.clear();
    if (m_stale) {
        clearCards();
        m_stale = false;
    }
    m_state = State::Failed;
    rebuildFooter();
}

void LevelListingOverlay::pageInfo(std::string const& info, char const* key) {
    if (!key) return;
    // "total:start:count" (GameLevelManager::createPageInfo). GD sends it with
    // the page; whether before or after it, the numbers land.
    auto parts = utils::string::split(info, ":");
    if (parts.size() < 3) return;
    int total = utils::numFromString<int>(parts[0]).unwrapOr(-1);
    int start = utils::numFromString<int>(parts[1]).unwrapOr(-1);
    int count = utils::numFromString<int>(parts[2]).unwrapOr(-1);
    if (total < 0 || start < 0 || count < 0) return;
    if (!m_prefetchKey.empty() && m_prefetchKey == key && m_key != key) {
        m_prefetchTotal = total;
        m_prefetchEnd = start + count;
    } else if (m_key == key) {
        m_pendingTotal = total;
        m_pendingEnd = start + count;
    } else if (m_lastKey == key) {
        m_total = total;
        if (m_state == State::Loaded && !m_cards.empty()) m_more = start + count < total;
        m_dirty = true;
    }
}

void LevelListingOverlay::openItem(CCObject* item) {
    if (m_leaving) return;
    if (local()) {
        // GD's "my levels" cells replace the screen with the level's own page
        // (EditLevelLayer; a list's LevelListLayer), whose back button opens
        // "my levels" again: this page.
        CCScene* scene = nullptr;
        if (auto level = typeinfo_cast<GJGameLevel*>(item)) scene = EditLevelLayer::scene(level);
        else if (auto list = typeinfo_cast<GJLevelList*>(item)) scene = LevelListLayer::scene(list);
        if (!scene) return;
        m_leaving = true;
        if (m_input) m_input->defocus();
        CCDirector::get()->replaceScene(CCTransitionFade::create(0.5f, scene));
        return;
    }
    // GD's own cells push the page, so its back button returns to this list.
    CCScene* scene = nullptr;
    if (auto level = typeinfo_cast<GJGameLevel*>(item)) scene = LevelInfoLayer::scene(level, false);
    else if (auto list = typeinfo_cast<GJLevelList*>(item)) scene = LevelListLayer::scene(list);
    if (!scene) return;
    if (m_input) m_input->defocus();
    CCDirector::get()->pushScene(CCTransitionFade::create(0.5f, scene));
}

void LevelListingOverlay::goBack() {
    // The scene can't change while one fades in: wait for it (#52).
    if (m_leaving || CCDirector::get()->getIsTransitioning()) return;
    m_leaving = true;
    if (m_input) m_input->defocus();
    sfx::play(sfx::sound::WAVE_POP_OUT);
    backToCreate() = true;
    // GD's creator hub, which the menu hook turns into wherever the player came from.
    CCDirector::get()->replaceScene(CCTransitionFade::create(0.5f, CreatorLayer::scene()));
}

// --- your levels and lists ---

void LevelListingOverlay::readFolders() {
    // Only the folders something is in (GD numbers them up to 999).
    std::vector<int> used;
    auto llm = LocalLevelManager::sharedState();
    if (m_lists) {
        if (llm->m_localLists) {
            for (auto list : CCArrayExt<GJLevelList*>(llm->m_localLists)) if (list && list->m_folder > 0) used.push_back(list->m_folder);
        }
    } else if (llm->m_localLevels) {
        for (auto level : CCArrayExt<GJGameLevel*>(llm->m_localLevels)) if (level && level->m_levelFolder > 0) used.push_back(level->m_levelFolder);
    }
    if (m_folder > 0) used.push_back(m_folder);
    std::sort(used.begin(), used.end());
    used.erase(std::unique(used.begin(), used.end()), used.end());

    m_folders = {0};
    m_folderNames = {"all"};
    for (int folder : used) {
        m_folders.push_back(folder);
        m_folderNames.push_back(createdFolderName(folder));
    }
}

void LevelListingOverlay::showLocal() {
    dropPrefetch();
    clearCards();
    m_scroll->scrollTo(0);
    m_key.clear();
    m_current = nullptr;
    m_more = false;
    m_state = State::Loaded;

    // In GD's order (the one "my levels" shows), filtered like its search: by name.
    auto query = lower(m_query);
    auto matches = [&](std::string const& name, int folder) {
        if (m_folder > 0 && folder != m_folder) return false;
        return query.empty() || lower(name).find(query) != std::string::npos;
    };
    auto llm = LocalLevelManager::sharedState();
    if (m_lists) {
        if (llm->m_localLists) {
            for (auto list : CCArrayExt<GJLevelList*>(llm->m_localLists)) {
                if (list && matches(std::string(list->m_listName), list->m_folder)) m_toBuild.push_back(list);
            }
        }
    } else if (llm->m_localLevels) {
        for (auto level : CCArrayExt<GJGameLevel*>(llm->m_localLevels)) {
            if (level && matches(std::string(level->m_levelName), level->m_levelFolder)) m_toBuild.push_back(level);
        }
    }
    m_total = static_cast<int>(m_toBuild.size());
    // Room for every card up front: a press on one survives the others being built.
    m_cardPills.reserve(m_toBuild.size());
    m_cards.reserve(m_toBuild.size());
    m_pageStart = 0;
    buildPending();
    rebuildFooter();
}

void LevelListingOverlay::buildPending() {
    if (m_toBuildNext >= m_toBuild.size()) return;
    size_t end = std::min(m_toBuild.size(), m_toBuildNext + CARDS_PER_FRAME);
    for (; m_toBuildNext < end; m_toBuildNext++) addCard(m_toBuild[m_toBuildNext].data());
    if (m_toBuildNext >= m_toBuild.size()) {
        m_toBuild.clear();
        m_toBuildNext = 0;
    }
    // The footer already made room for them all (cardsHeight counts the pending ones).
    layoutCards();
}

GJSearchObject* LevelListingOverlay::onlineSearch() {
    // What GD's "my levels" shows for its online button: your levels by player ID.
    int user = GameManager::get()->m_playerUserID.value();
    return GJSearchObject::create(SearchType::UsersLevels, std::to_string(user));
}

void LevelListingOverlay::createNew() {
    if (m_leaving || !m_owner) return;
    m_leaving = true;
    if (m_input) m_input->defocus();
    // GD's own: a new level opens on its level page, a new list on its list
    // page, and their back buttons come back here.
    if (m_lists) m_owner->createNewList(nullptr);
    else m_owner->createNewLevel(nullptr);
}

} // namespace lazer
