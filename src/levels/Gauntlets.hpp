#pragma once

#include "LevelLibrary.hpp"
#include "MapPacks.hpp"

#include <Geode/Geode.hpp>
#include <functional>
#include <vector>

// GD's gauntlets (GauntletSelectLayer: GJMapPacks flagged as gauntlets,
// fetched by GameLevelManager::getGauntlets, each gauntlet's five levels by
// getGauntletLevels), kept for the session like the map packs, as packs
// (MapPacks.hpp) so song select shows them the same way.
//
// A gauntlet's levels are played in order: each opens once the one before
// is beaten (GameStatsManager::hasCompletedGauntletLevel). GD keeps copies
// of its own for them, with their own progress, apart from the same levels
// played from search. Beating all five unlocks the gauntlet's chest.
namespace lazer::gauntlets {

using packs::Pack;
using packs::State;

State state();
std::vector<Pack>& all();
// Starts loading the list (GD's own copy, if it has one, fills it in at
// once); nothing while it's loading or loaded. The listener is told when
// it's here.
void load();
// Loads a gauntlet's levels; nothing while they're loading or loaded. One
// request at a time: another gauntlet's waits its turn.
void loadLevels(size_t index);
// Loads every gauntlet's levels (for searching them).
void loadAllLevels();
bool loadingLevels();
float levelsProgress();
// Counts the gauntlet's beaten levels again (after a play), which of its
// levels are open, and whether its chest was taken.
void refresh(Pack& pack);
// Every level beaten and the chest not yet opened.
bool canClaim(Pack const& pack);
// Opens the chest: GD's own reward popup shows what's in it.
void claim(Pack& pack);
// The gauntlet with this GD ID, or -1.
int indexOf(int gauntletID);

void setListener(std::function<void()> listener);

} // namespace lazer::gauntlets
