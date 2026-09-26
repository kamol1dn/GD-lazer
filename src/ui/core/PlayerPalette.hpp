#pragma once

#include <Geode/cocos/include/cocos2d.h>

namespace lazer {

// The logo's colours, taken from the player's icon: primary and secondary
// become the disc's gradient, the glow colour the rim and the visualiser.
// Picked colours are nudged so any combination still reads: too-dark colours
// are lifted, near-identical gradient ends are pulled apart, and a rim that
// would vanish against the disc falls back to one that contrasts.
struct PlayerPalette {
    cocos2d::ccColor3B gradientA;  // primary
    cocos2d::ccColor3B gradientB;  // secondary
    cocos2d::ccColor3B rim;        // glow, readable on the disc
    cocos2d::ccColor3B visualiser; // glow, bright enough for additive drawing
    cocos2d::ccColor3B iconA;      // the cube in the middle
    cocos2d::ccColor3B iconB;

    // The current player's icon colours (GameManager).
    static PlayerPalette current();
    static PlayerPalette from(cocos2d::ccColor3B primary, cocos2d::ccColor3B secondary, cocos2d::ccColor3B glow);
};

inline cocos2d::ccColor4B withAlpha(cocos2d::ccColor3B c, GLubyte a = 255) { return {c.r, c.g, c.b, a}; }

} // namespace lazer
