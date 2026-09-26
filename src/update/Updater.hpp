#pragma once

#include <Geode/Geode.hpp>
#include <functional>
#include <string>

namespace lazer::updater {

// Lazer UI isn't on the Geode index, so it updates itself from GitHub:
//   - the newest version is the "version" in mod.json on the repo's main branch
//   - CI publishes a release tagged with that version (see .github/workflows/build.yml)
//   - installing downloads that release's .geode over our own package; it loads
//     on the next start, the same way Geode's own updater works.

enum class State { Idle, Checking, UpToDate, Available, Downloading, Installed, Failed };

State state();
std::string const& latestVersion(); // e.g. "v0.2.1", once a check found it
std::string const& error();         // why the last check or download failed

// Checks main for a newer version. `done` runs on the main thread afterwards.
void check(std::function<void()> done = nullptr);

// Called whenever the main menu loads: checks once per session (setting
// "check-updates"), and offers the update when one turns up. `delay` holds the
// prompt back, e.g. until the intro has played.
void onMenu(cocos2d::CCNode* menu, float delay);

// "Check for updates" in settings: checks and always reports the result.
void checkManually();

} // namespace lazer::updater
