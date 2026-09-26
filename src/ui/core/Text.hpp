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
    constexpr auto VAULT = "\xEE\x8B\x85";          // e2c5
    constexpr auto RANKING_STAR = "\xEE\x95\xA1";   // e561
    constexpr auto MUSIC = "\xEF\x80\x81";          // f001
    constexpr auto SEARCH = "\xEF\x80\x82";         // f002
    constexpr auto STAR = "\xEF\x80\x85";           // f005
    constexpr auto USER = "\xEF\x80\x87";           // f007
    constexpr auto CHECK = "\xEF\x80\x8C";          // f00c
    constexpr auto XMARK = "\xEF\x80\x8D";          // f00d
    constexpr auto GEAR = "\xEF\x80\x93";           // f013
    constexpr auto HOUSE = "\xEF\x80\x95";          // f015
    constexpr auto CLOCK = "\xEF\x80\x97";          // f017
    constexpr auto ROTATE = "\xEF\x80\xA1";         // f021
    constexpr auto LOCK = "\xEF\x80\xA3";           // f023
    constexpr auto VOLUME = "\xEF\x80\xA8";         // f028
    constexpr auto BOOKMARK = "\xEF\x80\xAE";       // f02e
    constexpr auto LIST = "\xEF\x80\xBA";           // f03a
    constexpr auto STEP_BACKWARD = "\xEF\x81\x88";  // f048
    constexpr auto PLAY = "\xEF\x81\x8B";           // f04b
    constexpr auto STEP_FORWARD = "\xEF\x81\x91";   // f051
    constexpr auto CHEVRON_LEFT = "\xEF\x81\x93";   // f053
    constexpr auto CHEVRON_RIGHT = "\xEF\x81\x94";  // f054
    constexpr auto CIRCLE_XMARK = "\xEF\x81\x97";   // f057
    constexpr auto CIRCLE_INFO = "\xEF\x81\x9A";    // f05a
    constexpr auto BAN = "\xEF\x81\x9E";            // f05e
    constexpr auto ARROW_UP = "\xEF\x81\xA2";       // f062
    constexpr auto PLUS = "\xEF\x81\xA7";           // f067
    constexpr auto GIFT = "\xEF\x81\xAB";           // f06b
    constexpr auto EYE = "\xEF\x81\xAE";            // f06e
    constexpr auto SHUFFLE = "\xEF\x81\xB4";        // f074
    constexpr auto FOLDER_OPEN = "\xEF\x81\xBC";    // f07c
    constexpr auto CHART = "\xEF\x82\x80";          // f080
    constexpr auto KEY = "\xEF\x82\x84";            // f084
    constexpr auto GEARS = "\xEF\x82\x85";          // f085
    constexpr auto TROPHY = "\xEF\x82\x91";         // f091
    constexpr auto GLOBE = "\xEF\x82\xAC";          // f0ac
    constexpr auto WRENCH = "\xEF\x82\xAD";         // f0ad
    constexpr auto LIST_CHECK = "\xEF\x82\xAE";     // f0ae
    constexpr auto USERS = "\xEF\x83\x80";          // f0c0
    constexpr auto LINK = "\xEF\x83\x81";           // f0c1
    constexpr auto COPY = "\xEF\x83\x85";           // f0c5
    constexpr auto ENVELOPE = "\xEF\x83\xA0";       // f0e0
    constexpr auto BOLT = "\xEF\x83\xA7";           // f0e7
    constexpr auto CLOUD_DOWN = "\xEF\x83\xAD";     // f0ed
    constexpr auto CLOUD_UP = "\xEF\x83\xAE";       // f0ee
    constexpr auto BELL = "\xEF\x83\xB3";           // f0f3
    constexpr auto SQUARE_PLUS = "\xEF\x83\xBE";    // f0fe
    constexpr auto GAMEPAD = "\xEF\x84\x9B";        // f11b
    constexpr auto KEYBOARD = "\xEF\x84\x9C";       // f11c
    constexpr auto PUZZLE = "\xEF\x84\xAE";         // f12e
    constexpr auto SHIELD = "\xEF\x84\xB2";         // f132
    constexpr auto CIRCLE_CHEVRON_LEFT = "\xEF\x84\xB7";// f137
    constexpr auto CIRCLE_PLAY = "\xEF\x85\x84";    // f144
    constexpr auto COMPASS = "\xEF\x85\x8E";        // f14e
    constexpr auto THUMBS_UP = "\xEF\x85\xA4";      // f164
    constexpr auto MOON = "\xEF\x86\x86";           // f186
    constexpr auto CUBE = "\xEF\x86\xB2";           // f1b2
    constexpr auto SLIDERS = "\xEF\x87\x9E";        // f1de
    constexpr auto BELL_SLASH = "\xEF\x87\xB6";     // f1f6
    constexpr auto USER_PLUS = "\xEF\x88\xB4";      // f234
    constexpr auto CIRCLE_PAUSE = "\xEF\x8A\x8B";   // f28b
    constexpr auto ID_CARD = "\xEF\x8B\x82";        // f2c2
    constexpr auto SIGN_OUT = "\xEF\x8B\xB5";       // f2f5
    constexpr auto SIGN_IN = "\xEF\x8B\xB6";        // f2f6
    constexpr auto PEN = "\xEF\x8C\x84";            // f304
    constexpr auto DESKTOP = "\xEF\x8E\x90";        // f390
    constexpr auto GEM = "\xEF\x8E\xA5";            // f3a5
    constexpr auto CHESS_ROOK = "\xEF\x91\x87";     // f447
    constexpr auto BOXES = "\xEF\x91\xA8";          // f468
    constexpr auto BOX_OPEN = "\xEF\x92\x9E";       // f49e
    constexpr auto ROUTE = "\xEF\x93\x97";          // f4d7
    constexpr auto USER_CHECK = "\xEF\x93\xBC";     // f4fc
    constexpr auto USER_CLOCK = "\xEF\x93\xBD";     // f4fd
    constexpr auto USER_GEAR = "\xEF\x93\xBE";      // f4fe
    constexpr auto COINS = "\xEF\x94\x9E";          // f51e
    constexpr auto SHIRT = "\xEF\x95\x93";          // f553
    constexpr auto AWARD = "\xEF\x95\x99";          // f559
    constexpr auto MEDAL = "\xEF\x96\xA2";          // f5a2
    constexpr auto LAYERS = "\xEF\x97\xBD";         // f5fd
    constexpr auto DUNGEON = "\xEF\x9B\x99";        // f6d9
    constexpr auto FIST = "\xEF\x9B\x9E";           // f6de
    constexpr auto RUNNING = "\xEF\x9C\x8C";        // f70c
    constexpr auto CALENDAR_DAY = "\xEF\x9E\x83";   // f783
    constexpr auto CALENDAR_WEEK = "\xEF\x9E\x84";  // f784
}

cocos2d::CCLabelBMFont* makeIcon(char const* glyph, float size);

// Regular-weight text wrapped to `maxWidth` (greedy, by words). The node's
// origin is the block's top-left corner; lines hang below it. Its content
// size is the size of the whole block.
cocos2d::CCNode* makeWrappedText(std::string const& text, float size, float maxWidth, cocos2d::ccColor3B color);

} // namespace lazer
