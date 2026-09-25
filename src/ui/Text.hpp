#pragma once

#include <Geode/cocos/include/cocos2d.h>
#include <string>

namespace lazer {

enum class Weight { Regular, SemiBold, Bold };

// Outfit label, rendered from a high-res bitmap font generated at build time
// (see mod.json "resources.fonts") and scaled down, so it stays crisp.
// `size` is the line height in GD units.
cocos2d::CCLabelBMFont* makeText(std::string const& text, Weight weight, float size);

// Font Awesome Free glyphs baked into the icon font. Generated together with the
// "icons" charset in mod.json: add new glyphs to both.
namespace icon {
    constexpr auto MUSIC = "\xEF\x80\x81";          // f001
    constexpr auto SEARCH = "\xEF\x80\x82";         // f002
    constexpr auto STAR = "\xEF\x80\x85";           // f005
    constexpr auto USER = "\xEF\x80\x87";           // f007
    constexpr auto CHECK = "\xEF\x80\x8C";          // f00c
    constexpr auto XMARK = "\xEF\x80\x8D";          // f00d
    constexpr auto GEAR = "\xEF\x80\x93";           // f013
    constexpr auto HOUSE = "\xEF\x80\x95";          // f015
    constexpr auto LOCK = "\xEF\x80\xA3";           // f023
    constexpr auto VOLUME = "\xEF\x80\xA8";         // f028
    constexpr auto STEP_BACKWARD = "\xEF\x81\x88";  // f048
    constexpr auto PLAY = "\xEF\x81\x8B";           // f04b
    constexpr auto STEP_FORWARD = "\xEF\x81\x91";   // f051
    constexpr auto CHEVRON_LEFT = "\xEF\x81\x93";   // f053
    constexpr auto CHEVRON_RIGHT = "\xEF\x81\x94";  // f054
    constexpr auto CIRCLE_XMARK = "\xEF\x81\x97";   // f057
    constexpr auto CIRCLE_INFO = "\xEF\x81\x9A";    // f05a
    constexpr auto GIFT = "\xEF\x81\xAB";           // f06b
    constexpr auto EYE = "\xEF\x81\xAE";            // f06e
    constexpr auto SHUFFLE = "\xEF\x81\xB4";        // f074
    constexpr auto CHART = "\xEF\x82\x80";          // f080
    constexpr auto KEY = "\xEF\x82\x84";            // f084
    constexpr auto GEARS = "\xEF\x82\x85";          // f085
    constexpr auto TROPHY = "\xEF\x82\x91";         // f091
    constexpr auto GLOBE = "\xEF\x82\xAC";          // f0ac
    constexpr auto WRENCH = "\xEF\x82\xAD";         // f0ad
    constexpr auto USERS = "\xEF\x83\x80";          // f0c0
    constexpr auto BELL = "\xEF\x83\xB3";           // f0f3
    constexpr auto GAMEPAD = "\xEF\x84\x9B";        // f11b
    constexpr auto KEYBOARD = "\xEF\x84\x9C";       // f11c
    constexpr auto PUZZLE = "\xEF\x84\xAE";         // f12e
    constexpr auto SHIELD = "\xEF\x84\xB2";         // f132
    constexpr auto CIRCLE_PLAY = "\xEF\x85\x84";    // f144
    constexpr auto SLIDERS = "\xEF\x87\x9E";        // f1de
    constexpr auto CIRCLE_PAUSE = "\xEF\x8A\x8B";   // f28b
    constexpr auto PEN = "\xEF\x8C\x84";            // f304
    constexpr auto DESKTOP = "\xEF\x8E\x90";        // f390
    constexpr auto GEM = "\xEF\x8E\xA5";            // f3a5
    constexpr auto BOX_OPEN = "\xEF\x92\x9E";       // f49e
    constexpr auto COINS = "\xEF\x94\x9E";          // f51e
    constexpr auto SHIRT = "\xEF\x95\x93";          // f553
    constexpr auto MEDAL = "\xEF\x96\xA2";          // f5a2
}

cocos2d::CCLabelBMFont* makeIcon(char const* glyph, float size);

// Regular-weight text wrapped to `maxWidth` (greedy, by words). The node's
// origin is the block's top-left corner; lines hang below it. Its content
// size is the size of the whole block.
cocos2d::CCNode* makeWrappedText(std::string const& text, float size, float maxWidth, cocos2d::ccColor3B color);

} // namespace lazer
