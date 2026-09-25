#pragma once

#include <Geode/Geode.hpp>
#include <optional>

namespace lazer::integrations {

// A player's cube as a SimplePlayer, about `size` units tall. Player 2 comes
// from Separate Dual Icons ("2P Skins"); nullptr when that mod isn't loaded.
cocos2d::CCNode* playerIcon(bool player2, float size);

// Player 2's whole icon set and colours from 2P Skins (nullopt without the mod).
struct PlayerLook {
    int cube, ship, ball, ufo, wave, robot, spider, swing, jetpack;
    int color1, color2, glowColor;
    bool glow;
};
std::optional<PlayerLook> player2Look();

// Better Progression's level, from its saved total EXP (same formulas as the mod).
struct Progression {
    int level;
    long long exp;
    long long levelStart; // EXP where this level began
    long long levelEnd;   // EXP where the next one begins
};
std::optional<Progression> betterProgression();
// The same for any player, from their public stats (the mod's calculateTotalEXP).
std::optional<Progression> betterProgression(GJUserScore* score);

// Better Progression's badge for `level` with the number on top, about `size`
// units tall; nullptr if its sprites aren't available.
cocos2d::CCNode* progressionBadge(int level, float size);

} // namespace lazer::integrations
