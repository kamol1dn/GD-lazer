#include "SettingsContent.hpp"

#include "../ui/MenuBackground.hpp"
#include "../ui/SettingsOverlay.hpp"
#include "../ui/SettingsRows.hpp"
#include "../ui/Text.hpp"
#include "Account.hpp"
#include "GDOptions.hpp"

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
    // A hidden OptionsLayer to reuse GD's own handlers (help, rate, soundtracks...).
    auto options = std::make_shared<Ref<OptionsLayer>>(OptionsLayer::create());
    overlay->keepAlive(options);

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
    lastPageName.clear();
    for (int page : pagesFor(Home::Audio)) addGDPage(page);
    overlay->addSubsection("Songs");
    overlay->addRow(ButtonRow::create("Song browser", w, k, [gd] { gd->layer()->onSongBrowser(nullptr); }));
    overlay->addRow(ButtonRow::create("Soundtracks", w, k, [options] { (*options)->onSoundtracks(nullptr); }));

    // --- Graphics ---
    overlay->beginSection("Graphics", icon::DESKTOP);
    overlay->addSubsection("Display");
    overlay->addRow(ButtonRow::create("Resolution, fullscreen & texture quality", w, k, [] {
        VideoOptionsLayer::create()->show();
    }));
    lastPageName.clear();
    for (int page : pagesFor(Home::Graphics)) addGDPage(page);

    // --- Gameplay ---
    overlay->beginSection("Gameplay", icon::GAMEPAD);
    lastPageName.clear();
    for (int page : pagesFor(Home::Gameplay)) addGDPage(page);

    // --- Input ---
    overlay->beginSection("Input", icon::KEYBOARD);
    overlay->addSubsection("Keyboard");
    overlay->addRow(ButtonRow::create("Keybindings", w, k, [gd] { gd->layer()->onKeybindings(nullptr); }));

    // --- Other GD pages ---
    auto others = pagesFor(Home::Other);
    if (!others.empty()) {
        overlay->beginSection("Other", icon::SLIDERS);
        lastPageName.clear();
        for (int page : others) addGDPage(page);
        overlay->addSubsection("Parental");
        overlay->addRow(ButtonRow::create("Parental control", w, k, [gd] { gd->layer()->onParental(nullptr); }));
    }

    // --- Lazer UI + Geode ---
    overlay->beginSection("Lazer UI", icon::STAR);
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

    overlay->addSubsection("Mods");
    overlay->addRow(ButtonRow::create("All Lazer UI settings", w, k, [mod] { openSettingsPopup(mod); }));
    overlay->addRow(ButtonRow::create("Geode mods", w, k, [] { openModsList(); }));

    overlay->addSubsection("Classic");
    overlay->addRow(ButtonRow::create("Open classic GD settings", w, k, [menu] { menu->onOptions(nullptr); }));

    overlay->finish();
}

} // namespace lazer
