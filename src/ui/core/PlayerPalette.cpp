#include "PlayerPalette.hpp"

#include <Geode/binding/GameManager.hpp>

#include <algorithm>
#include <cmath>

using namespace cocos2d;

namespace lazer {

namespace {
    struct HSV {
        float h, s, v; // h in [0, 1)
    };
    struct RGB {
        float r, g, b;
    };

    RGB toRGB(ccColor3B c) { return {c.r / 255.f, c.g / 255.f, c.b / 255.f}; }
    ccColor3B toByte(RGB c) {
        auto b = [](float v) { return static_cast<GLubyte>(std::clamp(v, 0.f, 1.f) * 255.f + 0.5f); };
        return {b(c.r), b(c.g), b(c.b)};
    }

    HSV toHSV(RGB c) {
        float mx = std::max({c.r, c.g, c.b}), mn = std::min({c.r, c.g, c.b});
        float d = mx - mn;
        float h = 0;
        if (d > 1e-5f) {
            if (mx == c.r) h = std::fmod((c.g - c.b) / d, 6.f);
            else if (mx == c.g) h = (c.b - c.r) / d + 2;
            else h = (c.r - c.g) / d + 4;
            h /= 6;
            if (h < 0) h += 1;
        }
        return {h, mx > 0 ? d / mx : 0, mx};
    }

    RGB fromHSV(HSV c) {
        float h = std::fmod(c.h, 1.f) * 6;
        if (h < 0) h += 6;
        float f = h - std::floor(h);
        float p = c.v * (1 - c.s), q = c.v * (1 - c.s * f), t = c.v * (1 - c.s * (1 - f));
        switch (static_cast<int>(h) % 6) {
            case 0: return {c.v, t, p};
            case 1: return {q, c.v, p};
            case 2: return {p, c.v, t};
            case 3: return {p, q, c.v};
            case 4: return {t, p, c.v};
            default: return {c.v, p, q};
        }
    }

    float luminance(RGB c) { return 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b; }

    float distance(RGB a, RGB b) {
        float dr = a.r - b.r, dg = a.g - b.g, db = a.b - b.b;
        return std::sqrt((dr * dr + dg * dg + db * db) / 3);
    }

    RGB mix(RGB a, RGB b, float t) {
        return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t};
    }

    constexpr float MIN_GRADIENT_V = 0.45f;
    constexpr float GRAY = 0.18f; // saturation below this counts as grey
}

PlayerPalette PlayerPalette::current() {
    auto gm = GameManager::get();
    return from(gm->colorForIdx(gm->getPlayerColor()), gm->colorForIdx(gm->getPlayerColor2()),
                gm->colorForIdx(gm->getPlayerGlowColor()));
}

PlayerPalette PlayerPalette::from(ccColor3B primary, ccColor3B secondary, ccColor3B glow) {
    HSV a = toHSV(toRGB(primary)), b = toHSV(toRGB(secondary)), g = toHSV(toRGB(glow));

    // Dark colours make a muddy disc: lift them, keeping the hue.
    a.v = std::max(a.v, MIN_GRADIENT_V);
    b.v = std::max(b.v, MIN_GRADIENT_V);

    // Ends too close together make no visible gradient: pull them apart.
    if (distance(fromHSV(a), fromHSV(b)) < 0.14f) {
        if (a.s < GRAY && b.s < GRAY) {
            // Greys: spread the brightness instead of inventing a hue.
            if (a.v > 0.75f) b.v = a.v - 0.3f;
            else b.v = std::min(1.f, a.v + 0.3f);
        } else {
            HSV base = a.s >= GRAY ? a : b;
            b = {base.h + 0.09f, std::max(base.s, 0.45f), base.v > 0.7f ? base.v - 0.22f : base.v + 0.2f};
            a = base;
        }
    }

    RGB ga = fromHSV(a), gb = fromHSV(b);
    RGB disc = mix(ga, gb, 0.5f);
    float discLum = luminance(disc);

    PlayerPalette p;
    p.gradientA = toByte(ga);
    p.gradientB = toByte(gb);

    // Visualiser bars are additive: dark colours would draw nothing.
    HSV vis = g;
    vis.v = std::max(vis.v, 0.9f);
    if (vis.s < GRAY) vis.s = 0; // near-grey glow -> white
    p.visualiser = toByte(fromHSV(vis));

    // Rim: the glow colour, as long as it stands out from the disc.
    HSV rim = g;
    rim.v = std::max(rim.v, 0.7f);
    RGB rimRGB = fromHSV(rim);
    float contrast = std::max(std::abs(luminance(rimRGB) - discLum), distance(rimRGB, disc) * 0.8f);
    if (contrast < 0.22f) {
        if (discLum < 0.7f) {
            rimRGB = mix(rimRGB, {1, 1, 1}, 0.75f);
        } else {
            // Light disc: a deep shade of it instead of a white-on-white rim.
            HSV deep = toHSV(disc);
            deep.v = 0.3f;
            if (deep.s >= GRAY) deep.s = std::max(deep.s, 0.3f);
            rimRGB = fromHSV(deep);
        }
    }
    p.rim = toByte(rimRGB);

    // The cube in the middle is white like osu!'s wordmark, unless the disc is
    // so light that white would vanish: then it takes the rim's colour.
    p.iconA = p.iconB = discLum < 0.72f ? ccColor3B {255, 255, 255} : p.rim;
    return p;
}

} // namespace lazer
