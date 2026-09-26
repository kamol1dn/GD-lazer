#include "Updater.hpp"

#include <Geode/ui/MDPopup.hpp>
#include <Geode/utils/web.hpp>
#include <sstream>
#include <thread>

using namespace geode::prelude;

namespace lazer::updater {

namespace {
    constexpr auto MOD_JSON_URL = "https://raw.githubusercontent.com/kamol1dn/GD-lazer/main/mod.json";
    constexpr auto RELEASE_URL = "https://github.com/kamol1dn/GD-lazer/releases/download/{}/kamol1dn.lazer-ui.geode";
    constexpr auto CHANGELOG_URL = "https://raw.githubusercontent.com/kamol1dn/GD-lazer/main/changelog.md";
    constexpr auto RELEASE_API_URL = "https://api.github.com/repos/kamol1dn/GD-lazer/releases/tags/{}";
    constexpr auto RELEASES_PAGE = "https://github.com/kamol1dn/GD-lazer/releases";

    State g_state = State::Idle;
    std::string g_latest;
    std::string g_error;
    std::string g_notes; // changelog sections between our version and the latest, markdown
    bool g_checkedThisSession = false;
    bool g_prompted = false; // asked once per session; "later" means next start

    // The "## vX.Y.Z" sections of changelog.md newer than what's running, up to `latest`.
    std::string notesSince(std::string const& changelog, VersionInfo const& latest) {
        auto current = Mod::get()->getVersion();
        std::istringstream in(changelog);
        std::string line, notes;
        bool keep = false;
        while (std::getline(in, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.starts_with("## ")) {
                auto version = VersionInfo::parse(line.substr(3));
                keep = version && current < version.unwrap() && version.unwrap() <= latest;
            } else if (line.starts_with("# ")) {
                keep = false;
            }
            if (keep) notes += line + "\n";
        }
        return notes;
    }

    void finishCheck(State state, std::string latest, std::string error, std::function<void()> done) {
        g_state = state;
        g_latest = std::move(latest);
        g_error = std::move(error);
        if (state == State::Available) log::info("Update available: {} (running {})", g_latest, Mod::get()->getVersion().toVString());
        else if (state == State::Failed) log::warn("Update check failed: {}", g_error);
        if (done) done();
    }

    // Shows a popup in the Lazer style (PopupStyle restyles popups carrying this marker).
    template <class T>
    T* lazerStyled(T* popup) {
        popup->setUserObject("restyle"_spr, CCBool::create(true));
        popup->show();
        return popup;
    }

    void showRestartPrompt() {
        lazerStyled(createQuickPopup(
            "Lazer UI updated",
            fmt::format("<cg>{}</c> is installed. Restart Geometry Dash to use it.", g_latest),
            "Later", "Restart",
            [](auto, bool restart) {
                if (restart) game::restart(true);
            },
            false
        ));
    }

    void install() {
        if (g_state != State::Available) return;
        g_state = State::Downloading;
        std::string version = g_latest;
        auto target = Mod::get()->getPackagePath();

        std::thread([version, target] {
            // Blocking request on a worker thread (the coroutine web API crashes this MSVC's compiler).
            auto res = web::WebRequest().timeout(std::chrono::seconds(120)).getSync(fmt::format(RELEASE_URL, version));
            std::string error;
            if (res.code() == 404) {
                error = "the release for this version isn't published yet. Try again in a few minutes.";
            } else if (!res.ok()) {
                error = fmt::format("download failed (HTTP {})", res.code());
            } else {
                // Write next to the package, check it really is our mod at that version, then swap it in.
                auto temp = target;
                temp += ".download";
                if (auto written = file::writeBinary(temp, res.data()); !written) {
                    error = written.unwrapErr();
                } else if (auto meta = ModMetadata::createFromGeodeFile(temp); meta.hasErrors()) {
                    error = "the download isn't a valid .geode";
                } else if (meta.getID() != Mod::get()->getID()) {
                    error = "the download is a different mod";
                } else {
                    std::error_code ec;
                    std::filesystem::rename(temp, target, ec);
                    if (ec) {
                        error = fmt::format("couldn't replace {}: {}", target.filename().string(), ec.message());
                    } else {
                        // Geode only re-extracts a .geode whose modified time differs from the one
                        // it last unpacked, and on Android the swapped-in file can keep the old
                        // time: the new binary never loaded. Drop Geode's record so it unpacks again.
                        std::filesystem::last_write_time(target, std::filesystem::file_time_type::clock::now(), ec);
                        std::filesystem::remove(dirs::getModRuntimeDir() / Mod::get()->getID() / "modified-at", ec);
                    }
                }
                std::error_code ec;
                std::filesystem::remove(temp, ec);
            }

            Loader::get()->queueInMainThread([error] {
                if (!error.empty()) {
                    g_state = State::Available; // still out there, can retry
                    g_error = error;
                    log::warn("Update download failed: {}", error);
                    lazerStyled(createQuickPopup(
                        "Update failed", fmt::format("Couldn't install {}: {}", g_latest, error),
                        "OK", "Open releases",
                        [](auto, bool open) {
                            if (open) web::openLinkInBrowser(RELEASES_PAGE);
                        },
                        false
                    ));
                    return;
                }
                g_state = State::Installed;
                log::info("Installed {}, applies on restart", g_latest);
                showRestartPrompt();
            });
        }).detach();
    }

    void showUpdatePrompt() {
        g_prompted = true;
        auto text = fmt::format("Lazer UI **{}** is out. You have {}.\n\n", g_latest, Mod::get()->getVersion().toVString());
        text += g_notes.empty() ? "No release notes for this version." : g_notes;
        lazerStyled(MDPopup::create(fmt::format("Update to {}", g_latest), text, "Later", "Update", [](bool update) {
            if (update) install();
        }));
    }
}

State state() { return g_state; }
std::string const& latestVersion() { return g_latest; }
std::string const& error() { return g_error; }

void check(std::function<void()> done) {
    if (g_state == State::Checking || g_state == State::Downloading) return;
    if (g_state == State::Installed) {
        if (done) done();
        return;
    }
    g_state = State::Checking;
    g_checkedThisSession = true;

    std::thread([done = std::move(done)] {
        auto res = web::WebRequest().timeout(std::chrono::seconds(15)).getSync(MOD_JSON_URL);
        State state = State::Failed;
        std::string latest, error, notes;
        if (!res.ok()) {
            error = fmt::format("couldn't reach GitHub (HTTP {})", res.code());
        } else if (auto json = res.json(); !json) {
            error = "mod.json on main isn't valid JSON";
        } else if (auto str = json.unwrap()["version"].asString(); !str) {
            error = "mod.json on main has no version";
        } else if (auto parsed = VersionInfo::parse(str.unwrap()); !parsed) {
            error = fmt::format("can't read version \"{}\"", str.unwrap());
        } else {
            latest = parsed.unwrap().toVString();
            state = Mod::get()->getVersion() < parsed.unwrap() ? State::Available : State::UpToDate;
            // CI publishes the release a few minutes after the version lands on main:
            // until it's there, there's nothing to install yet.
            if (state == State::Available) {
                auto release = web::WebRequest().timeout(std::chrono::seconds(15)).getSync(fmt::format(RELEASE_API_URL, latest));
                if (release.code() == 404) {
                    log::info("{} is on main but not released yet", latest);
                    state = State::UpToDate;
                }
            }
            if (state == State::Available) {
                auto changelog = web::WebRequest().timeout(std::chrono::seconds(15)).getSync(CHANGELOG_URL);
                if (auto text = changelog.string(); changelog.ok() && text) notes = notesSince(text.unwrap(), parsed.unwrap());
            }
        }
        Loader::get()->queueInMainThread([state, latest, error, notes, done] {
            g_notes = notes;
            finishCheck(state, latest, error, done);
        });
    }).detach();
}

void onMenu(CCNode* menu, float delay) {
    if (!Mod::get()->getSettingValue<bool>("check-updates")) return;

    // Prompt from the menu itself, after `delay`, as long as it's still on screen.
    Ref<CCNode> self = menu;
    auto promptLater = [self, delay] {
        if (g_prompted || g_state != State::Available || !self->getParent()) return;
        self->runAction(CCSequence::create(
            CCDelayTime::create(delay),
            CallFuncExt::create([] {
                if (!g_prompted && g_state == State::Available) showUpdatePrompt();
            }),
            nullptr
        ));
    };
    if (!g_checkedThisSession) check(promptLater);
    else promptLater();
}

void checkManually() {
    if (g_state == State::Downloading) return;
    if (g_state == State::Installed) return showRestartPrompt();
    check([] {
        switch (g_state) {
            case State::Available:
                showUpdatePrompt();
                break;
            case State::UpToDate:
                lazerStyled(createQuickPopup(
                    "No updates", fmt::format("You have the latest version ({}).", Mod::get()->getVersion().toVString()),
                    "OK", nullptr, [](auto, bool) {}, false
                ));
                break;
            case State::Failed:
                lazerStyled(createQuickPopup("Update check failed", fmt::format("Couldn't check for updates: {}", g_error),
                                             "OK", nullptr, [](auto, bool) {}, false));
                break;
            default:
                break;
        }
    });
}

} // namespace lazer::updater
