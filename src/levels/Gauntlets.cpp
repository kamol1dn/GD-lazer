#include "Gauntlets.hpp"

#include <algorithm>
#include <cctype>

using namespace geode::prelude;

namespace lazer::gauntlets {

namespace {
    std::string lower(std::string s) {
        for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
    }

    std::vector<int> parseIDs(std::string const& ids) {
        std::vector<int> out;
        for (auto part : utils::string::split(ids, ",")) {
            if (auto id = utils::numFromString<int>(utils::string::trim(part)); id && *id > 0) out.push_back(*id);
        }
        return out;
    }

    Pack makePack(GJMapPack* pack) {
        Pack p;
        p.pack = pack;
        p.gauntlet = true;
        p.id = pack->m_packID;
        p.gauntletType = pack->m_packID;
        auto type = static_cast<GauntletType>(pack->m_packID);
        p.name = GauntletNode::nameForType(type);
        if (p.name.empty()) p.name = pack->m_packName;
        if (p.name.empty()) p.name = fmt::format("Gauntlet {}", pack->m_packID);
        if (lower(p.name).find("gauntlet") == std::string::npos) p.name += " Gauntlet";
        p.frame = GauntletNode::frameForType(type);
        p.difficulty = 0;
        p.stars = 0;
        p.coins = 0;
        p.levelIDs = parseIDs(pack->m_levelStrings);
        if (p.levelIDs.empty() && pack->m_levels) {
            for (auto obj : CCArrayExt<CCObject*>(pack->m_levels)) {
                if (auto str = typeinfo_cast<CCString*>(obj)) {
                    if (auto id = utils::numFromString<int>(str->getCString()); id && *id > 0) p.levelIDs.push_back(*id);
                } else if (auto level = typeinfo_cast<GJGameLevel*>(obj)) {
                    p.levelIDs.push_back(level->m_levelID.value());
                }
            }
        }
        // GD's own node for the gauntlet knows its colours (GauntletNode::generateNode).
        p.textColor = {255, 255, 255};
        p.barColor = {120, 100, 200};
        if (auto node = GauntletNode::create(pack)) {
            p.textColor = node->m_labelColor;
            p.barColor = node->m_backgroundColor;
        }
        p.search = lower(p.name);
        lazer::gauntlets::refresh(p);
        return p;
    }

    // The session's store, and GD's delegate for its requests.
    class Store : public LevelManagerDelegate {
    public:
        static Store& get() {
            static Store store;
            return store;
        }

        State state = State::Unloaded;
        std::vector<Pack> packs;
        std::function<void()> listener;
        bool listPending = false;
        size_t levelsFor = SIZE_MAX;       // the gauntlet whose levels are in flight
        std::vector<size_t> levelsQueue;   // gauntlets waiting their turn
        std::string levelsKey;

        void notify() {
            if (listener) listener();
        }

        void release() {
            auto glm = GameLevelManager::sharedState();
            if (glm->m_levelManagerDelegate == this && !listPending && levelsFor == SIZE_MAX) glm->m_levelManagerDelegate = nullptr;
        }

        // GD keeps the gauntlets it fetched (GameLevelManager::m_savedGauntlets):
        // those fill the list in without a request.
        bool fillFromSaved() {
            auto glm = GameLevelManager::sharedState();
            if (!glm->areGauntletsLoaded() || !glm->m_savedGauntlets) return false;
            std::vector<GJMapPack*> found;
            for (auto [key, pack] : CCDictionaryExt<std::string, GJMapPack*>(glm->m_savedGauntlets)) {
                if (pack) found.push_back(pack);
            }
            if (found.empty()) return false;
            takePacks(found);
            return true;
        }

        void takePacks(std::vector<GJMapPack*> found) {
            // In GD's order: by ID.
            std::sort(found.begin(), found.end(), [](GJMapPack* a, GJMapPack* b) { return a->m_packID < b->m_packID; });
            packs.clear();
            for (auto pack : found) {
                if (std::any_of(packs.begin(), packs.end(), [&](Pack const& p) { return p.id == pack->m_packID; })) continue;
                packs.push_back(makePack(pack));
            }
            state = packs.empty() ? State::Failed : State::Loaded;
            log::info("Gauntlets: {} loaded", packs.size());
        }

        void requestList() {
            auto glm = GameLevelManager::sharedState();
            listPending = true;
            glm->m_levelManagerDelegate = this;
            log::debug("Gauntlets: requesting the list");
            glm->getGauntlets();
        }

        void listArrived(CCArray* items) {
            listPending = false;
            std::vector<GJMapPack*> found;
            auto glm = GameLevelManager::sharedState();
            for (auto pack : CCArrayExt<GJMapPack*>(items)) {
                if (!pack) continue;
                found.push_back(pack);
                // Kept the way GD's own page keeps them, so the next open is instant.
                glm->saveGauntlet(pack);
            }
            takePacks(found);
            release();
            notify();
        }

        void requestLevels(size_t index) {
            auto& p = packs[index];
            p.state = State::Loading;
            levelsFor = index;
            auto glm = GameLevelManager::sharedState();
            levelsKey = glm->getGauntletSearchKey(p.id);
            glm->m_levelManagerDelegate = this;
            log::debug("Gauntlets: requesting the levels of {} ({})", p.name, levelsKey);
            glm->getGauntletLevels(p.id);
        }

        void nextLevels() {
            if (levelsFor != SIZE_MAX || levelsQueue.empty()) return;
            size_t index = levelsQueue.front();
            levelsQueue.erase(levelsQueue.begin());
            if (index < packs.size()) requestLevels(index);
            else nextLevels();
        }

        void levelsDone() {
            levelsFor = SIZE_MAX;
            levelsKey.clear();
            release();
            notify();
            nextLevels();
        }

        void levelsArrived(CCArray* items) {
            if (levelsFor >= packs.size()) return levelsDone();
            auto& p = packs[levelsFor];
            std::vector<levels::Entry> got;
            for (auto level : CCArrayExt<GJGameLevel*>(items)) {
                if (!level) continue;
                // GD's copy of a gauntlet's level: its progress is kept apart
                // from the same level played from search.
                level->m_gauntletLevel = true;
                level = levels::withSavedCopy(level);
                level->m_gauntletLevel = true;
                auto e = levels::fromLevel(level, false);
                e.pack = static_cast<int>(levelsFor);
                e.gauntlet = true;
                got.push_back(std::move(e));
            }
            // In the gauntlet's order. GD's list may name levels the server
            // didn't return: the ones it did stand, in order.
            p.levels.clear();
            if (p.levelIDs.empty()) {
                for (auto const& e : got) p.levelIDs.push_back(e.id);
            }
            for (int id : p.levelIDs) {
                auto it = std::find_if(got.begin(), got.end(), [id](auto const& e) { return e.id == id; });
                if (it != got.end()) p.levels.push_back(*it);
            }
            for (auto& e : got) {
                if (std::none_of(p.levels.begin(), p.levels.end(), [&](auto const& l) { return l.id == e.id; })) p.levels.push_back(e);
            }
            p.state = p.levels.empty() ? State::Failed : State::Loaded;
            lazer::gauntlets::refresh(p);
            log::info("Gauntlet {}: {} of {} levels loaded", p.name, p.levels.size(), p.levelIDs.size());
            levelsDone();
        }

        // LevelManagerDelegate. The list and a gauntlet's levels both come
        // through here: told apart by what's in them (GD's keys for them
        // differ between versions).
        void loadLevelsFinished(CCArray* levels, char const* key) override { loadLevelsFinished(levels, key, 0); }
        void loadLevelsFailed(char const* key) override { loadLevelsFailed(key, 0); }

        void loadLevelsFinished(CCArray* items, char const* key, int) override {
            std::string k = key ? key : "";
            bool packsIn = items && items->count() > 0 && typeinfo_cast<GJMapPack*>(items->objectAtIndex(0));
            bool levelsIn = items && items->count() > 0 && typeinfo_cast<GJGameLevel*>(items->objectAtIndex(0));
            if (listPending && (packsIn || (!levelsIn && levelsFor == SIZE_MAX))) listArrived(items);
            else if (levelsFor != SIZE_MAX && (levelsIn || k == levelsKey)) levelsArrived(items);
            else log::debug("Gauntlets: ignored levels for key {}", k);
        }

        void loadLevelsFailed(char const* key, int) override {
            std::string k = key ? key : "";
            if (listPending && (k.empty() || k == "get_gauntlets" || k.find("gauntlet") != std::string::npos) && (levelsFor == SIZE_MAX || k != levelsKey)) {
                log::warn("Gauntlets: the list failed ({})", k);
                listPending = false;
                state = packs.empty() ? State::Failed : State::Loaded;
                release();
                notify();
            } else if (levelsFor != SIZE_MAX) {
                if (levelsFor < packs.size()) packs[levelsFor].state = State::Failed;
                log::warn("Gauntlets: levels failed ({})", k);
                levelsDone();
            }
        }
    };
}

State state() {
    return Store::get().state;
}

std::vector<Pack>& all() {
    return Store::get().packs;
}

void load() {
    auto& s = Store::get();
    if (s.state == State::Loading || s.state == State::Loaded) return;
    // The gauntlets' badges live in a sheet of their own, which GD's gauntlet
    // screen loads when it opens (GauntletNode's colours come from it too).
    CCSpriteFrameCache::sharedSpriteFrameCache()->addSpriteFramesWithFile("GauntletSheet.plist");
    s.state = State::Loading;
    if (s.fillFromSaved()) return;
    s.requestList();
}

void loadLevels(size_t index) {
    auto& s = Store::get();
    if (index >= s.packs.size()) return;
    auto& p = s.packs[index];
    if (p.state == State::Loading || p.state == State::Loaded) return;
    if (std::find(s.levelsQueue.begin(), s.levelsQueue.end(), index) != s.levelsQueue.end()) return;
    // The gauntlet the player is looking at goes first.
    p.state = State::Loading;
    s.levelsQueue.insert(s.levelsQueue.begin(), index);
    s.nextLevels();
}

void loadAllLevels() {
    auto& s = Store::get();
    for (size_t i = 0; i < s.packs.size(); i++) {
        auto& p = s.packs[i];
        if (p.state == State::Loading || p.state == State::Loaded) continue;
        if (std::find(s.levelsQueue.begin(), s.levelsQueue.end(), i) != s.levelsQueue.end()) continue;
        p.state = State::Loading;
        s.levelsQueue.push_back(i);
    }
    s.nextLevels();
}

bool loadingLevels() {
    auto& s = Store::get();
    return s.levelsFor != SIZE_MAX || !s.levelsQueue.empty();
}

float levelsProgress() {
    auto& s = Store::get();
    if (s.packs.empty()) return 1.f;
    size_t done = std::count_if(s.packs.begin(), s.packs.end(), [](Pack const& p) {
        return p.state == State::Loaded || p.state == State::Failed;
    });
    return static_cast<float>(done) / s.packs.size();
}

void refresh(Pack& p) {
    auto stats = GameStatsManager::sharedState();
    p.completed = 0;
    for (int id : p.levelIDs) {
        if (stats->hasCompletedGauntletLevel(id)) p.completed++;
    }
    p.claimed = stats->isGauntletChestUnlocked(p.id);
    // A level's GD copy appears once it's been played: pick it up. The
    // levels open in order: each once the one before is beaten.
    bool previousBeaten = true;
    for (auto& e : p.levels) {
        auto level = e.level ? levels::withSavedCopy(e.level) : nullptr;
        if (level && level != e.level.data()) {
            level->m_gauntletLevel = true;
            auto fresh = levels::fromLevel(level, false);
            fresh.pack = e.pack;
            fresh.gauntlet = true;
            e = fresh;
        } else if (e.level) {
            e.normalPercent = e.level->m_normalPercent.value();
            e.practicePercent = e.level->m_practicePercent;
            e.resolved = false;
        }
        bool beaten = stats->hasCompletedGauntletLevel(e.id) || e.normalPercent >= 100;
        e.locked = !previousBeaten;
        previousBeaten = beaten;
    }
}

bool canClaim(Pack const& p) {
    if (!p.pack || p.claimed || p.levelIDs.empty()) return false;
    return p.completed >= static_cast<int>(p.levelIDs.size());
}

void claim(Pack& p) {
    if (!lazer::gauntlets::canClaim(p)) return;
    // What GD's gauntlet node does on its chest: GD hands the chest's
    // contents out (GameStatsManager::unlockGauntletChest) and shows them.
    // The node is GD's, so its popup places itself.
    auto node = GauntletNode::create(p.pack);
    if (!node) return;
    Ref<GauntletNode> keep = node;
    node->setVisible(false);
    if (auto scene = CCDirector::get()->getRunningScene()) scene->addChild(node, -1000);
    node->onClaimReward();
    p.claimed = GameStatsManager::sharedState()->isGauntletChestUnlocked(p.id);
    log::info("Gauntlet {} chest claimed ({})", p.name, p.claimed);
    Loader::get()->queueInMainThread([keep] { keep->removeFromParent(); });
}

int indexOf(int gauntletID) {
    auto& packs = Store::get().packs;
    for (size_t i = 0; i < packs.size(); i++) {
        if (packs[i].id == gauntletID) return static_cast<int>(i);
    }
    return -1;
}

void setListener(std::function<void()> listener) {
    Store::get().listener = std::move(listener);
}

} // namespace lazer::gauntlets
