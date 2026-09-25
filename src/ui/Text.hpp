#pragma once

#include <Geode/cocos/include/cocos2d.h>
#include <string>

namespace lazer {

enum class Weight { Regular, SemiBold, Bold };

// Outfit label, rendered from a high-res bitmap font generated at build time
// (see mod.json "resources.fonts") and scaled down, so it stays crisp.
// `size` is the line height in GD units.
cocos2d::CCLabelBMFont* makeText(std::string const& text, Weight weight, float size);

// Font Awesome Free glyphs baked into the icon font (codepoints listed in mod.json).
namespace icon {
    constexpr auto MUSIC = "\xEF\x80\x81";        // f001
    constexpr auto STAR = "\xEF\x80\x85";         // f005
    constexpr auto USER = "\xEF\x80\x87";         // f007
    constexpr auto GEAR = "\xEF\x80\x93";         // f013
    constexpr auto HOUSE = "\xEF\x80\x95";        // f015
    constexpr auto PLAY = "\xEF\x81\x8B";         // f04b
    constexpr auto CIRCLE_XMARK = "\xEF\x81\x97"; // f057
    constexpr auto GIFT = "\xEF\x81\xAB";         // f06b
    constexpr auto CHART = "\xEF\x82\x80";        // f080
    constexpr auto TROPHY = "\xEF\x82\x91";       // f091
    constexpr auto GLOBE = "\xEF\x82\xAC";        // f0ac
    constexpr auto BELL = "\xEF\x83\xB3";         // f0f3
    constexpr auto GAMEPAD = "\xEF\x84\x9B";      // f11b
    constexpr auto PUZZLE = "\xEF\x84\xAE";       // f12e
    constexpr auto PEN = "\xEF\x8C\x84";          // f304
    constexpr auto SHIRT = "\xEF\x95\x93";        // f553
}

cocos2d::CCLabelBMFont* makeIcon(char const* glyph, float size);

} // namespace lazer
