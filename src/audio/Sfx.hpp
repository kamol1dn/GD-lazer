#pragma once

namespace lazer::sfx {

// UI sounds, played through GD's effects channel (so GD's SFX volume applies).
// Names are files in resources/sounds without the extension. Sounds from
// osu-resources by ppy Pty Ltd (CC-BY-NC 4.0).
namespace sound {
    // Main menu (osu.Game/Screens/Menu)
    inline constexpr char const* MENU_BUTTON_HOVER = "menu-button-hover";
    inline constexpr char const* MENU_PLAY_SELECT = "menu-button-play-select";
    inline constexpr char const* MENU_DEFAULT_SELECT = "menu-button-default-select";
    inline constexpr char const* LOGO_SELECT = "menu-osu-logo-select";
    inline constexpr char const* LOGO_SWOOSH = "menu-osu-logo-swoosh";
    inline constexpr char const* BACK_TO_LOGO = "menu-back-to-logo";
    inline constexpr char const* LOGO_HEARTBEAT = "menu-osu-logo-heartbeat";
    inline constexpr char const* LOGO_DOWNBEAT = "menu-osu-logo-downbeat";

    // HoverSampleSet.Default / Button / ButtonSidebar
    inline constexpr char const* DEFAULT_HOVER = "ui-default-hover";
    inline constexpr char const* DEFAULT_SELECT = "ui-default-select";
    inline constexpr char const* BUTTON_HOVER = "ui-button-hover";
    inline constexpr char const* BUTTON_SELECT = "ui-button-select";
    inline constexpr char const* SIDEBAR_HOVER = "ui-button-sidebar-hover";
    inline constexpr char const* SIDEBAR_SELECT = "ui-button-sidebar-select";

    inline constexpr char const* CHECK_ON = "ui-check-on";
    inline constexpr char const* CHECK_OFF = "ui-check-off";
    inline constexpr char const* NOTCH_TICK = "ui-notch-tick";

    // SettingsPanel pops in with its own sample and out with the default one;
    // WaveContainer has its own pair.
    inline constexpr char const* SETTINGS_POP_IN = "ui-settings-pop-in";
    inline constexpr char const* OVERLAY_POP_IN = "ui-overlay-pop-in";
    inline constexpr char const* OVERLAY_POP_OUT = "ui-overlay-pop-out";
    inline constexpr char const* WAVE_POP_IN = "ui-wave-pop-in";
    inline constexpr char const* WAVE_POP_OUT = "ui-overlay-big-pop-out";
}

// Intro / outro audio, from osu-resources: the triangles theme's opening
// (with osu!'s voice taken out of the first second) and the "see you next
// time" voice line.
namespace cue {
    inline constexpr char const* INTRO = "intro-triangles";
    inline constexpr char const* SEEYA = "intro-seeya";
}

// Plays a cue at full volume (GD's SFX volume still applies, the UI sound
// setting doesn't).
void playCue(char const* name);

// Plays `name` at `frequency` (speed and pitch, like osu!'s channel Frequency),
// randomised by +-pitchVariation. Repeats of the same sound within 20 ms are
// dropped (OsuGameBase.SAMPLE_DEBOUNCE_TIME).
void play(char const* name, float pitchVariation = 0.f, float frequency = 1.f);

// HoverSounds: +-0.02 pitch, and one debounce shared by every hover sound.
void hover(char const* name);

// HoverClickSounds: +-0.01 pitch.
void click(char const* name);

} // namespace lazer::sfx
