#pragma once

#include "LevelLibrary.hpp"

#include <Geode/Geode.hpp>
#include <functional>
#include <string>
#include <vector>

// RobTop's map packs (GD's map pack page: GJMapPack, fetched by
// GameLevelManager::getMapPacks in pages of ten). Kept for the session, so
// song select opens them again without a request; GD caches the pages on
// disk too, which is what fills in the first open.
namespace lazer::packs {

enum class State { Unloaded, Loading, Loaded, Failed };

struct Pack {
    geode::Ref<GJMapPack> pack;
    // An online level list, shown the same way (OnlineBrowse.hpp): its
    // levels under it, its reward in diamonds.
    geode::Ref<GJLevelList> list;
    std::string creator;
    int downloads = 0, likes = 0;
    int diamonds = 0;              // a list's reward, for beating levelsToClaim of its levels
    int levelsToClaim = 0;
    int id = 0;
    std::string name;
    int difficulty = 0;            // GJDifficultySprite frame (see levels::Entry)
    int stars = 0;                 // the reward for finishing every level
    int coins = 0;
    cocos2d::ccColor3B textColor {255, 255, 255};
    cocos2d::ccColor3B barColor {255, 255, 255};
    std::vector<int> levelIDs;     // in the pack's order
    std::vector<levels::Entry> levels; // once loaded, in that order (saved copies where you have them)
    State state = State::Unloaded;
    int completed = 0;             // levels beaten (refresh())
    bool claimed = false;          // reward taken
    std::string search;            // lower-cased name, for filtering
    // A gauntlet (Gauntlets.hpp): GD's GauntletType, and the frame of its
    // icon in GD's sheet. Its reward is a chest, its levels open in order.
    bool gauntlet = false;
    int gauntletType = 0;
    std::string frame;
};

// The list of packs, in GD's order.
State state();
std::vector<Pack>& all();
// Starts loading the list (every page, one after the other); nothing while
// it's loading or loaded. The listener is told after every page.
void load();
// Loads a pack's levels; nothing while they're loading or loaded. Requests
// go one at a time: another pack's waits its turn.
void loadLevels(size_t index);
// Loads every pack's levels (for searching them), a few packs per request.
void loadAllLevels();
// Whether any levels are on their way, and how many packs have theirs (0..1).
bool loadingLevels();
float levelsProgress();
// Counts the pack's beaten levels again (after a play) and whether its
// reward was taken.
void refresh(Pack& pack);
// Every level beaten and the reward not yet taken.
bool canClaim(Pack const& pack);
// GD awards the pack's stars and coins (GameStatsManager::completedMapPack).
void claim(Pack& pack);

// Who to tell when the list or a pack's levels change (one at a time: the
// song select that's up). Clear it with nullptr when leaving.
void setListener(std::function<void()> listener);

} // namespace lazer::packs
