#pragma once

#include <Geode/cocos/include/cocos2d.h>
#include <algorithm>

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

    // HSL -> RGB, like osu-framework's Colour4.FromHSL.
    inline cocos2d::ccColor4B fromHSL(float h, float s, float l) {
        auto hue2rgb = [](float p, float q, float t) {
            if (t < 0) t += 1;
            if (t > 1) t -= 1;
            if (t < 1.f / 6) return p + (q - p) * 6 * t;
            if (t < 1.f / 2) return q;
            if (t < 2.f / 3) return p + (q - p) * (2.f / 3 - t) * 6;
            return p;
        };
        float q = l < 0.5f ? l * (1 + s) : l + s - l * s;
        float p = 2 * l - q;
        auto byte = [](float v) { return static_cast<GLubyte>(std::clamp(v, 0.f, 1.f) * 255.f + 0.5f); };
        return {byte(hue2rgb(p, q, h + 1.f / 3)), byte(hue2rgb(p, q, h)), byte(hue2rgb(p, q, h - 1.f / 3)), 255};
    }

    // OverlayColourProvider for an arbitrary scheme hue (osu!: Purple 255, Orange 45, Blue 200, ...).
    struct Scheme {
        float hue;
        cocos2d::ccColor4B get(float s, float l) const { return fromHSL(hue / 360.f, s, l); }
        cocos2d::ccColor4B highlight1() const { return get(1, 0.7f); }
        cocos2d::ccColor4B colour3() const { return get(0.6f, 0.5f); }
        cocos2d::ccColor4B light3() const { return get(0.4f, 0.7f); }
        cocos2d::ccColor4B light4() const { return get(0.4f, 0.5f); }
        cocos2d::ccColor4B dark3() const { return get(0.2f, 0.25f); }
        cocos2d::ccColor4B dark4() const { return get(0.2f, 0.2f); }
        cocos2d::ccColor4B background4() const { return get(0.1f, 0.2f); }
        cocos2d::ccColor4B background5() const { return get(0.1f, 0.15f); }
        cocos2d::ccColor4B background6() const { return get(0.1f, 0.1f); }
        cocos2d::ccColor4B content2() const { return get(0.4f, 0.9f); }
    };
    inline cocos2d::ccColor4B lerp(cocos2d::ccColor4B a, cocos2d::ccColor4B b, float t) {
        auto mix = [t](GLubyte x, GLubyte y) { return static_cast<GLubyte>(x + (y - x) * t); };
        return {mix(a.r, b.r), mix(a.g, b.g), mix(a.b, b.b), mix(a.a, b.a)};
    }
}

// Set while a full overlay (settings, etc.) is open, so the menu underneath
// stops reacting to hover. Touches are already swallowed by the overlay.
inline bool g_overlayOpen = false;

} // namespace lazer
