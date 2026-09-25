#pragma once

#include <Geode/cocos/include/cocos2d.h>

namespace lazer {

// osu!'s OverlayColourProvider (Purple scheme, hue 255): colours are HSL
// with fixed saturation / lightness per role.
namespace theme {
    constexpr cocos2d::ccColor4B BACKGROUND4 {48, 46, 56, 255};   // (0.1, 0.20)
    constexpr cocos2d::ccColor4B BACKGROUND5 {36, 34, 42, 255};   // (0.1, 0.15)
    constexpr cocos2d::ccColor4B BACKGROUND6 {24, 23, 28, 255};   // (0.1, 0.10)
    constexpr cocos2d::ccColor4B DARK3 {57, 51, 77, 255};         // (0.2, 0.25)
    constexpr cocos2d::ccColor4B COLOUR3 {89, 51, 204, 255};      // (0.6, 0.50)
    constexpr cocos2d::ccColor4B HIGHLIGHT1 {140, 102, 255, 255}; // (1.0, 0.70)
    constexpr cocos2d::ccColor3B CONTENT1 {255, 255, 255};        // (0.4, 1.00)
    constexpr cocos2d::ccColor3B CONTENT2 {224, 219, 240};        // (0.4, 0.90)
    constexpr cocos2d::ccColor3B LIGHT1 {194, 184, 224};          // (0.4, 0.80)
    constexpr cocos2d::ccColor3B FOREGROUND1 {148, 143, 163};     // (0.1, 0.60)

    inline cocos2d::ccColor3B rgb(cocos2d::ccColor4B c) { return {c.r, c.g, c.b}; }
    inline cocos2d::ccColor4B lerp(cocos2d::ccColor4B a, cocos2d::ccColor4B b, float t) {
        auto mix = [t](GLubyte x, GLubyte y) { return static_cast<GLubyte>(x + (y - x) * t); };
        return {mix(a.r, b.r), mix(a.g, b.g), mix(a.b, b.b), mix(a.a, b.a)};
    }
}

// Set while a full overlay (settings, etc.) is open, so the menu underneath
// stops reacting to hover. Touches are already swallowed by the overlay.
inline bool g_overlayOpen = false;

} // namespace lazer
