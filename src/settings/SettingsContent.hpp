#pragma once

#include <Geode/Geode.hpp>

namespace lazer {

class SettingsOverlay;
class MenuBackground;

// Fills the settings overlay: GD's options (read live from GD), audio and
// video controls, Lazer UI's own settings and shortcuts to Geode.
void buildSettings(SettingsOverlay* overlay, MenuLayer* menu, MenuBackground* background);

} // namespace lazer
