#include "SettingsContent.hpp"

#include "../audio/MusicPlayer.hpp"
#include "../integrations/Ventilla.hpp"
#include "../ui/core/Text.hpp"
#include "../ui/menu/MenuBackground.hpp"
#include "../ui/overlays/BlockedSongsOverlay.hpp"
#include "../ui/overlays/SettingsOverlay.hpp"
#include "../ui/overlays/SettingsRows.hpp"
#include "../update/Updater.hpp"
#include "Account.hpp"
#include "GDOptions.hpp"
#include "OptionDialogs.hpp"

#include <Geode/ui/GeodeUI.hpp>
#include <algorithm>
#include <cctype>

using namespace geode::prelude;

namespace lazer {

namespace {

    std::string lower(std::string s) {
        std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
        return s;
    }

    bool contains(std::string const& haystack, char const* needle) {
        return lower(haystack).find(needle) != std::string::npos;
    }

    std::string percent(float v) { return fmt::format("{}%", int(std::round(v * 100))); }

    // Which of our sections a GD options page belongs to.
    enum class Home { Audio, Graphics, Gameplay, Other };
    Home homeFor(std::string const& page) {
        if (contains(page, "audio") || contains(page, "music") || contains(page, "sound")) return Home::Audio;
        if (contains(page, "perf") || contains(page, "graphic") || contains(page, "video") || contains(page, "visual")) {
            return Home::Graphics;
        }
        if (contains(page, "gameplay") || contains(page, "practice") || contains(page, "play")) return Home::Gameplay;
        return Home::Other;
    }
}

void buildSettings(SettingsOverlay* overlay, MenuLayer* menu, MenuBackground* background) {
    float w = overlay->rowWidth();
    float k = overlay->k();
    auto mod = Mod::get();

    auto gd = std::make_shared<GDOptions>();
    overlay->keepAlive(gd);

    // GD splits long categories over several pages with the same title ("Visual");
    // consecutive pages with the same title share one subsection.
    std::string lastPageName;
    auto addGDPage = [&](int page) {
        auto const& name = gd->pageNames()[page];
        if (name != lastPageName) overlay->addSubsection(name);
        lastPageName = name;
        for (auto const& t : gd->toggles()) {
            if (t.page != page) continue;
            auto toggle = t; // copy: rows outlive this loop
            auto row = ToggleRow::create(
                t.label, w, k,
                [gd, toggle] { return gd->isOn(toggle); },
                [gd, toggle] { return gd->toggle(toggle); }
            );
            row->setTooltip(t.description);
            overlay->addRow(row);
        }
    };
    auto pagesFor = [&](Home home) {
        std::vector<int> pages;
        for (size_t i = 0; i < gd->pageNames().size(); i++) {
            if (homeFor(gd->pageNames()[i]) == home) pages.push_back(int(i));
        }
        return pages;
    };

    // --- Lazer UI + Geode (first: it's what people open settings here for) ---
    overlay->beginSection("Lazer UI", icon::STAR);
    overlay->addSubsection("Layout");
    // Slider 0..1 covers 50%..200%, in steps of 5%.
    auto scaleRow = SliderRow::create(
        "UI scale", w, k,
        [mod] { return (mod->getSettingValue<int64_t>("ui-scale") - 50) / 150.f; },
        [mod](float v) { mod->setSettingValue<int64_t>("ui-scale", 50 + int64_t(std::round(v * 30)) * 5); },
        [](float v) { return fmt::format("{}%", 50 + int(std::round(v * 30)) * 5); }
    );
    scaleRow->setTooltip("Size of the menus. Takes effect next time the menu loads.");
    overlay->addRow(scaleRow);
    auto iconColors = ToggleRow::create("Use player cube colors", w, k,
        [mod] { return mod->getSettingValue<bool>("player-icon-colors"); },
        [mod] {
            bool value = !mod->getSettingValue<bool>("player-icon-colors");
            mod->setSettingValue<bool>("player-icon-colors", value);
            return value;
        });
    iconColors->setTooltip("Use your saved primary and secondary colors for the large cube. Turn off for the original contrast colors. Applies next time the menu loads.");
    overlay->addRow(iconColors);
    overlay->addSubsection("Background");
    auto dimRow = SliderRow::create(
        "Background dim", w, k,
        [mod] { return mod->getSettingValue<int64_t>("background-dim") / 100.f; },
        [mod, background](float v) {
            mod->setSettingValue<int64_t>("background-dim", int64_t(std::round(v * 100)));
            if (background) background->setDim(v);
        },
        percent
    );
    overlay->addRow(dimRow);
    auto blurRow = ToggleRow::create(
        "Blur background", w, k,
        [mod] { return mod->getSettingValue<bool>("background-blur"); },
        [mod] {
            bool v = !mod->getSettingValue<bool>("background-blur");
            mod->setSettingValue<bool>("background-blur", v);
            return v;
        }
    );
    blurRow->setTooltip("Applies the next time the menu loads.");
    overlay->addRow(blurRow);
    auto trianglesRow = ToggleRow::create(
        "Background triangles", w, k,
        [mod] { return mod->getSettingValue<bool>("background-triangles"); },
        [mod] {
            bool v = !mod->getSettingValue<bool>("background-triangles");
            mod->setSettingValue<bool>("background-triangles", v);
            return v;
        }
    );
    trianglesRow->setTooltip("Applies the next time the menu loads.");
    overlay->addRow(trianglesRow);

    auto introRow = ToggleRow::create(
        "Intro and outro", w, k,
        [mod] { return mod->getSettingValue<bool>("intro"); },
        [mod] {
            bool v = !mod->getSettingValue<bool>("intro");
            mod->setSettingValue<bool>("intro", v);
            return v;
        }
    );
    introRow->setTooltip("Animated intro when the game starts, and an outro when you quit.");
    overlay->addRow(introRow);

    overlay->addSubsection("Parallax");
    // Slider 0..1 covers 0%..8%, in steps of 0.5%. Applies live: the menu reads it every frame.
    auto parallaxRow = [&](char const* name, char const* key, char const* tooltip) {
        auto row = SliderRow::create(
            name, w, k,
            [mod, key] { return static_cast<float>(mod->getSettingValue<double>(key) / 8.0); },
            [mod, key](float v) { mod->setSettingValue<double>(key, std::round(v * 16) / 2.0); },
            [](float v) { return fmt::format("{}%", std::round(v * 16) / 2.0); }
        );
        row->setTooltip(tooltip);
        overlay->addRow(row);
    };
    parallaxRow("Background parallax", "parallax-background", "How far the background moves with the mouse (on phones, the tilt).");
    parallaxRow("Menu parallax", "parallax-menu", "How far the main menu's buttons move. Less than the background makes them float over it.");
#ifdef GEODE_IS_MOBILE
    auto tiltRow = ToggleRow::create(
        "Tilt parallax", w, k,
        [mod] { return mod->getSettingValue<bool>("tilt-parallax"); },
        [mod] {
            bool v = !mod->getSettingValue<bool>("tilt-parallax");
            mod->setSettingValue<bool>("tilt-parallax", v);
            return v;
        }
    );
    tiltRow->setTooltip("Move the menu with the phone's tilt (gyroscope and accelerometer).");
    overlay->addRow(tiltRow);
#endif

#ifdef GEODE_IS_WINDOWS
    overlay->addSubsection("Cursor");
    auto cursorRow = ToggleRow::create(
        "osu! cursor", w, k,
        [mod] { return mod->getSettingValue<bool>("custom-cursor"); },
        [mod] {
            bool v = !mod->getSettingValue<bool>("custom-cursor");
            mod->setSettingValue<bool>("custom-cursor", v);
            return v;
        }
    );
    cursorRow->setTooltip("osu!'s menu cursor instead of the system pointer.");
    overlay->addRow(cursorRow);
    // Slider 0..1 covers 0.5x..2x, in steps of 0.1.
    auto cursorSizeRow = SliderRow::create(
        "Cursor size", w, k,
        [mod] { return static_cast<float>((mod->getSettingValue<double>("cursor-size") - 0.5) / 1.5); },
        [mod](float v) { mod->setSettingValue<double>("cursor-size", std::round((0.5 + v * 1.5) * 10) / 10.0); },
        [](float v) { return fmt::format("{:.1f}x", std::round((0.5 + v * 1.5) * 10) / 10.0); }
    );
    cursorSizeRow->setShownIf([mod] { return mod->getSettingValue<bool>("custom-cursor"); });
    overlay->addRow(cursorSizeRow);
    auto cursorRotateRow = ToggleRow::create(
        "Cursor rotation", w, k,
        [mod] { return mod->getSettingValue<bool>("cursor-rotation"); },
        [mod] {
            bool v = !mod->getSettingValue<bool>("cursor-rotation");
            mod->setSettingValue<bool>("cursor-rotation", v);
            return v;
        }
    );
    cursorRotateRow->setTooltip("Tilts as it moves, and turns to follow the mouse while you drag.");
    cursorRotateRow->setShownIf([mod] { return mod->getSettingValue<bool>("custom-cursor"); });
    overlay->addRow(cursorRotateRow);
#endif

    overlay->addSubsection("Music");
    auto musicRow = ToggleRow::create(
        "Play level songs in the menu", w, k,
        [mod] { return mod->getSettingValue<bool>("music-player"); },
        [mod] {
            bool v = !mod->getSettingValue<bool>("music-player");
            mod->setSettingValue<bool>("music-player", v);
            return v;
        }
    );
    musicRow->setTooltip("Plays your downloaded levels' songs instead of the menu theme, with the level's thumbnail as the background. Applies the next time the menu loads.");
    overlay->addRow(musicRow);
    auto blockedRow = ButtonRow::create("Blocked songs", w, k, [overlay] {
        overlay->close();
        BlockedSongsOverlay::present();
    });
    blockedRow->setTooltip("Songs you blocked in the music player (the ban button): unblock them one by one or all at once.");
    blockedRow->setShownIf([] { return MusicPlayer::get().blockedCount() > 0; });
    overlay->addRow(blockedRow);

    overlay->addSubsection("Gameplay");
    auto gameplayRow = ToggleRow::create(
        "Lazer pause and results", w, k,
        [mod] { return mod->getSettingValue<bool>("restyle-gameplay"); },
        [mod] {
            bool v = !mod->getSettingValue<bool>("restyle-gameplay");
            mod->setSettingValue<bool>("restyle-gameplay", v);
            return v;
        }
    );
    gameplayRow->setTooltip("The pause menu and the level complete screen the osu! way. GD's options and other mods' pause buttons stay reachable from them.");
    overlay->addRow(gameplayRow);

    overlay->addSubsection("Updates");
    auto updatesRow = ToggleRow::create(
        "Check for updates on start", w, k,
        [mod] { return mod->getSettingValue<bool>("check-updates"); },
        [mod] {
            bool v = !mod->getSettingValue<bool>("check-updates");
            mod->setSettingValue<bool>("check-updates", v);
            return v;
        }
    );
    updatesRow->setTooltip("Lazer UI isn't on the Geode index: it checks GitHub for new versions and offers to install them.");
    overlay->addRow(updatesRow);
    overlay->addRow(ButtonRow::create(fmt::format("Check for updates ({})", mod->getVersion().toVString()), w, k,
                                      [] { updater::checkManually(); }));

    overlay->addSubsection("Mods");
    overlay->addRow(ButtonRow::create("All Lazer UI settings", w, k, [mod] { openSettingsPopup(mod); }));
    overlay->addRow(ButtonRow::create("Geode mods", w, k, [] { openModsList(); }));

    overlay->addSubsection("Classic");
    overlay->addRow(ButtonRow::create("Open classic GD settings", w, k, [menu] { menu->onOptions(nullptr); }));

    // --- General ---
    overlay->beginSection("General", icon::GEAR);
    overlay->addSubsection("Account");
    overlay->addRow(InfoRow::create(w, k, icon::USER,
        [] { return account::loggedIn() ? account::username() : std::string("not logged in"); },
        [] {
            if (account::busy()) return "working...";
            return account::loggedIn() ? "your progress can be saved online and loaded on other devices"
                              : "log in to keep your progress safe online";
        }
    ));
    auto accountButton = [&](char const* label, char const* tooltip, bool whenLoggedIn,
                             std::function<void()> action, bool dangerous = false) {
        auto row = ButtonRow::create(label, w, k, std::move(action), dangerous);
        row->setTooltip(tooltip);
        row->setShownIf([whenLoggedIn] { return account::loggedIn() == whenLoggedIn; });
        overlay->addRow(row);
    };
    accountButton("Save", "Back up your progress to your account.", true,
                  [] { account::save(); });
    accountButton("Load", "Replace the progress on this device with your online backup.", true,
                  [] { account::load(); });
    accountButton("Refresh login", "Log in again, e.g. after changing your password.", true,
                  [] { account::refreshLogin(); });
    accountButton("Manage account", "Open account management on the Geometry Dash website.", true,
                  [] { account::manage(); });
    accountButton("Unlink account", "Log out of your account on this device.", true,
                  [] { account::unlink(); }, true);
    accountButton("Log in", "Log in to an existing account.", false,
                  [] { account::logIn(); });
    accountButton("Register", "Create a new account.", false,
                  [] { account::registerAccount(); });

    // --- Audio ---
    overlay->beginSection("Audio", icon::VOLUME);
    overlay->addSubsection("Volume");
    overlay->addRow(SliderRow::create(
        "Music", w, k,
        [] { return GameManager::get()->m_bgVolume; },
        [](float v) {
            GameManager::get()->m_bgVolume = v;
            FMODAudioEngine::sharedEngine()->setBackgroundMusicVolume(v);
        },
        percent
    ));
    overlay->addRow(SliderRow::create(
        "Effects", w, k,
        [] { return GameManager::get()->m_sfxVolume; },
        [](float v) {
            GameManager::get()->m_sfxVolume = v;
            FMODAudioEngine::sharedEngine()->setEffectsVolume(v);
        },
        percent
    ));
    overlay->addRow(SliderRow::create(
        "Interface sounds", w, k,
        [mod] { return mod->getSettingValue<int64_t>("ui-sound-volume") / 100.f; },
        [mod](float v) { mod->setSettingValue<int64_t>("ui-sound-volume", int64_t(std::round(v * 100))); },
        percent
    ));
#if defined(GEODE_IS_WINDOWS) || defined(GEODE_IS_MACOS)
    auto wheelRow = ToggleRow::create(
        "Scroll to change volume", w, k,
        [mod] { return mod->getSettingValue<bool>("scroll-volume"); },
        [mod] {
            bool v = !mod->getSettingValue<bool>("scroll-volume");
            mod->setSettingValue<bool>("scroll-volume", v);
            return v;
        }
    );
    wheelRow->setTooltip("The mouse wheel turns the music up and down wherever nothing else scrolls, and anywhere in the menus with Alt held. Scroll over the effects or interface meter to change that one.");
    overlay->addRow(wheelRow);
#endif
    lastPageName.clear();
    for (int page : pagesFor(Home::Audio)) addGDPage(page);
    overlay->addSubsection("Songs");
    overlay->addRow(ButtonRow::create("Song browser", w, k, [gd] { gd->layer()->onSongBrowser(nullptr); }));
    // GD's own settings open this through OptionsLayer::onSoundtracks, which
    // hides that layer first and crashes when the layer was never shown.
    overlay->addRow(ButtonRow::create("Soundtracks", w, k, [] {
        // The way GD shows its drop-down layers (GJMoreGamesLayer): into the
        // scene first, then the slide-in.
        auto layer = SongsLayer::create();
        if (!layer) return;
        CCDirector::get()->getRunningScene()->addChild(layer, 100);
        layer->showLayer(false);
    }));
    // JoseII's Ventilla, when it's installed: its radio in place of the songs,
    // and its own options as rows here (written to its settings, which it
    // watches, so they take effect right away).
    if (auto ventillaMod = ventilla::mod()) {
        overlay->addSubsection("Ventilla radio");
        auto radioRow = ToggleRow::create(
            "Radio in the menus", w, k,
            [mod] { return mod->getSettingValue<bool>("ventilla-radio"); },
            [mod] {
                bool on = !mod->getSettingValue<bool>("ventilla-radio");
                MusicPlayer::get().setRadio(on);
                return on;
            }
        );
        radioRow->setTooltip("Ventilla's live radio as the menu music instead of your levels' songs. Off: your songs play and the radio keeps quiet in the menus. The music player has the same switch.");
        overlay->addRow(radioRow);

        // Ventilla's toggles: its setting key, our label, the tooltip.
        auto theirs = [&](char const* key, char const* label, char const* tooltip) {
            if (!ventillaMod->hasSetting(key)) return;
            std::string k2 = key;
            auto row = ToggleRow::create(
                label, w, k,
                [ventillaMod, k2] { return ventillaMod->getSettingValue<bool>(k2); },
                [ventillaMod, k2] {
                    bool v = !ventillaMod->getSettingValue<bool>(k2);
                    ventillaMod->setSettingValue<bool>(k2, v);
                    return v;
                }
            );
            if (tooltip && *tooltip) row->setTooltip(tooltip);
            overlay->addRow(row);
        };
        if (ventillaMod->hasSetting("volume")) {
            overlay->addRow(SliderRow::create(
                "Radio volume", w, k,
                [ventillaMod] { return ventillaMod->getSettingValue<int64_t>("volume") / 100.f; },
                [ventillaMod](float v) { ventillaMod->setSettingValue<int64_t>("volume", int64_t(std::round(v * 100))); },
                percent
            ));
        }
        theirs("fade-in", "Fade the radio in", "The radio fades in over a moment when it starts, instead of cutting in.");

        overlay->addSubsection("Radio outside the menus");
        theirs("play-in-normal-mode", "In levels", "The radio instead of the level's song in normal mode.");
        theirs("play-in-practice-mode", "In practice mode", "The radio instead of the level's song in practice mode.");
        theirs("play-in-pause-menu", "In the pause menu", "");
        theirs("play-in-editor", "In the editor", "");
#ifdef GEODE_IS_DESKTOP
        theirs("play-in-background", "While the game is in the background", "Keeps streaming when you switch to another window.");
#endif

        overlay->addSubsection("Radio in GD's other rooms");
        theirs("shop.mp3", "The shop", "");
        theirs("shop3.mp3", "The community shop", "");
        theirs("shop4.mp3", "The mechanic shop", "");
        theirs("shop5.mp3", "The diamond shop", "");
        theirs("secretShop.mp3", "The secret shop", "");
        theirs("secretLoop.mp3", "The treasure room and the vault", "");
        theirs("secretLoop02.mp3", "The vault of secrets", "");
        theirs("secretLoop03.mp3", "The chamber of time", "");
        theirs("secretLoop04.mp3", "The wraith", "");
        theirs("tower01.mp3", "The tower", "");
        theirs("extras", "Everywhere else", "Any other screen with its own music.");

        overlay->addSubsection("Ventilla's buttons");
        theirs("show-button-in-main-menu", "In the toolbar", "Ventilla's button (its popup: what's playing, its volume) in the main menu's toolbar. Takes effect the next time the menu loads.");
        theirs("show-button-in-pause-menu", "In the pause menu", "");
        theirs("show-button-in-editor-pause", "In the editor's pause menu", "");
    }

    // --- Graphics ---
    overlay->beginSection("Graphics", icon::DESKTOP);
#ifdef GEODE_IS_DESKTOP
    // GD's mobile builds have no video options (or keybindings) screen.
    overlay->addSubsection("Display");
    overlay->addRow(ButtonRow::create("Fullscreen, resolution, texture quality & FPS", w, k, [] {
#ifdef GEODE_IS_WINDOWS
        showGraphicsDialog();
#else
        VideoOptionsLayer::create()->show();
#endif
    }));
#endif
    lastPageName.clear();
    for (int page : pagesFor(Home::Graphics)) addGDPage(page);

    // --- Gameplay ---
    overlay->beginSection("Gameplay", icon::GAMEPAD);
    lastPageName.clear();
    for (int page : pagesFor(Home::Gameplay)) addGDPage(page);

    // --- Input ---
#ifdef GEODE_IS_DESKTOP
    overlay->beginSection("Input", icon::KEYBOARD);
    overlay->addSubsection("Keyboard");
    overlay->addRow(ButtonRow::create("Keybindings", w, k, [gd] { gd->layer()->onKeybindings(nullptr); }));
#endif

    // --- Other GD pages ---
    auto others = pagesFor(Home::Other);
    if (!others.empty()) {
        overlay->beginSection("Other", icon::SLIDERS);
        lastPageName.clear();
        for (int page : others) addGDPage(page);
        overlay->addSubsection("Parental");
        overlay->addRow(ButtonRow::create("Parental control", w, k, [gd] {
#ifdef GEODE_IS_WINDOWS
            showParentalDialog();
#else
            gd->layer()->onParental(nullptr);
#endif
        }));
    }

    overlay->finish();
}

} // namespace lazer
