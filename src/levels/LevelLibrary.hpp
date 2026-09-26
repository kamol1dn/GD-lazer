#pragma once

#include <Geode/Geode.hpp>
#include <string>
#include <vector>

namespace lazer::levels {

// A level you can play from song select: one of RobTop's (official) or a
// saved online level.
struct Entry {
    geode::Ref<GJGameLevel> level;
    bool official = false;
    int id = 0;
    std::string name;
    std::string creator;
    std::string songTitle;
    std::string songArtist;
    std::string songPath;      // empty if the song isn't downloaded
    int difficulty = 0;        // GJDifficultySprite frame (-1 auto, 0 N/A, 1-5, 6-10 demons)
    int stars = 0;
    int normalPercent = 0;
    int practicePercent = 0;
    int coins = 0;             // coins in the level
    int coinsCollected = 0;
    bool coinsVerified = false;
    int length = 0;            // 0 tiny .. 4 XL, 5 platformer
    bool platformer = false;   // rated in moons instead of stars
    int bestTime = 0;          // platformer best time, ms (0 = none)
    std::string search;        // lower-cased name, creator and song, for filtering
};

// GD 2.2 has two kinds of level: classic (stars) and platformer (moons).
enum class Kind { Classic, Platformer };

// RobTop's levels of that kind in order, then every saved online level of that kind.
// Classic: the main levels. Platformer: the Tower's levels (vanilla hides them
// behind the last page of the main levels), each once the one before is beaten.
std::vector<Entry> all(Kind kind);

// Official levels first by number, saved ones newest-saved first (GD's order).
enum class Sort { Default, Title, Difficulty, Progress };

cocos2d::ccColor3B difficultyColor(int difficulty);
char const* lengthName(int length);

// Whether the level can go straight to gameplay (level data and song present);
// otherwise GD's level page downloads what's missing.
bool readyToPlay(Entry const& entry);

} // namespace lazer::levels
