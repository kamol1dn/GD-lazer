#include "Updater.hpp"

#include "../ui/core/Text.hpp"
#include "../ui/core/Theme.hpp"
#include "../ui/overlays/Dialog.hpp"

#include <Geode/utils/web.hpp>
#include <atomic>
#include <cstring>
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

    // The dialog showing the update, kept so a check or download can report
    // into it. Null (or closing) once the player closed it.
    Ref<Dialog> g_dialog;
    std::atomic<size_t> g_downloaded {0};
    std::atomic<size_t> g_downloadTotal {0};

    Dialog* openDialog() {
        return g_dialog && !g_dialog->closing() && g_dialog->getParent() ? g_dialog.data() : nullptr;
    }

    // Shows `content` in the open update dialog, or a new one.
    void present(Dialog::Content content) {
        if (auto dialog = openDialog()) {
            dialog->stopAllActions(); // the download's progress polling
            dialog->setContent(std::move(content));
        }
        else g_dialog = Dialog::show(std::move(content));
    }

    std::string megabytes(size_t bytes) {
        return fmt::format("{:.1f} MB", bytes / (1024.0 * 1024.0));
    }

    // changelog.md's markdown as dialog items: version headings and bullets.
    std::vector<CCNode*> notesItems(std::string const& notes) {
        std::vector<CCNode*> items;
        float k = unitScale();
        float width = Dialog::listWidth();
        std::istringstream in(notes);
        std::string line;
        while (std::getline(in, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            // Drop markdown emphasis; the dialog's text is plain.
            for (auto mark : {"**", "__", "`"}) {
                for (size_t at; (at = line.find(mark)) != std::string::npos;) line.erase(at, std::strlen(mark));
            }
            if (line.starts_with("## ")) {
                auto holder = CCNode::create();
                auto title = makeText(line.substr(3), Weight::SemiBold, 19 * k);
                title->setAnchorPoint({0, 1});
                title->setPosition({0, items.empty() ? 0.f : -10 * k});
                holder->addChild(title);
                holder->setContentSize({width, (items.empty() ? 28 : 38) * k});
                items.push_back(holder);
            } else if (line.starts_with("- ") || line.starts_with("* ")) {
                auto holder = CCNode::create();
                auto bullet = makeText("\xE2\x80\xA2", Weight::Bold, 15 * k); // U+2022
                bullet->setColor(theme::LIGHT1);
                bullet->setAnchorPoint({0, 1});
                holder->addChild(bullet);
                auto text = makeWrappedText(line.substr(2), 15 * k, width - 16 * k, theme::CONTENT2);
                text->setPositionX(16 * k);
                holder->addChild(text);
                holder->setContentSize({width, text->getContentSize().height + 6 * k});
                items.push_back(holder);
            } else if (!line.empty()) {
                auto text = makeWrappedText(line, 15 * k, width, theme::CONTENT2);
                text->setContentSize({width, text->getContentSize().height + 6 * k});
                items.push_back(text);
            }
        }
        return items;
    }

    void showRestartPrompt() {
        Dialog::Content content;
        content.icon = icon::CLOUD_DOWN;
        content.header = "Lazer UI updated";
        content.body = fmt::format("{} is installed. Restart Geometry Dash to use it.", g_latest);
        content.buttons = {
            {"Restart now", Dialog::Kind::Ok, [] { game::restart(true); }},
            {"Later", Dialog::Kind::Cancel, nullptr},
        };
        present(std::move(content));
    }

    void showFailed(std::string const& header, std::string const& body, bool releasesLink) {
        Dialog::Content content;
        content.icon = icon::TRIANGLE_EXCLAMATION;
        content.header = header;
        content.body = body;
        if (releasesLink) {
            content.buttons.push_back({"Open the releases page", Dialog::Kind::Ok, [] { web::openLinkInBrowser(RELEASES_PAGE); }});
        }
        content.buttons.push_back({"OK", Dialog::Kind::Cancel, nullptr});
        present(std::move(content));
    }

    void showDownloading() {
        Dialog::Content content;
        content.icon = icon::CLOUD_DOWN;
        content.header = fmt::format("Downloading {}", g_latest);
        content.body = "It installs when the download finishes, and applies on the next start.";
        content.progress = true;
        // Hidden, the download carries on and reports back in a new dialog.
        content.buttons = {{"Hide", Dialog::Kind::Cancel, nullptr}};
        present(std::move(content));

        // Poll the worker's progress while the dialog shows it.
        if (auto dialog = openDialog()) {
            Ref<Dialog> watched = dialog;
            dialog->runAction(CCRepeatForever::create(CCSequence::create(
                CCDelayTime::create(0.05f),
                CallFuncExt::create([watched] {
                    size_t done = g_downloaded, total = g_downloadTotal;
                    if (total > 0) {
                        watched->setProgress(float(done) / float(total), fmt::format("{} of {}", megabytes(done), megabytes(total)));
                    } else {
                        watched->setProgress(0.f, done > 0 ? megabytes(done) : "Connecting...");
                    }
                }),
                nullptr
            )));
        }
    }

    void install() {
        if (g_state != State::Available) return;
        g_state = State::Downloading;
        g_downloaded = 0;
        g_downloadTotal = 0;
        std::string version = g_latest;
        auto target = Mod::get()->getPackagePath();
        showDownloading();

        std::thread([version, target] {
            // Blocking request on a worker thread (the coroutine web API crashes this MSVC's compiler).
            auto res = web::WebRequest()
                .timeout(std::chrono::seconds(120))
                .onProgress([](web::WebProgress const& progress) {
                    g_downloaded = progress.downloaded();
                    g_downloadTotal = progress.downloadTotal();
                })
                .getSync(fmt::format(RELEASE_URL, version));
            std::string error;
            if (res.code() == 404) {
                error = "the release for this version isn't published yet. Try again in a few minutes.";
            } else if (!res.ok()) {
                error = fmt::format("download failed (HTTP {})", res.code());
            } else {
                // Write next to the package, check it really is our mod at that version, then swap it in.
                auto temp = target;
                temp += ".download";
                // The archive is closed again before the swap: Windows won't move a file that's open.
                auto hasBinary = [&temp](char const* name) {
                    auto archive = file::Unzip::create(temp);
                    return archive && archive.unwrap().hasEntry(name);
                };
                if (auto written = file::writeBinary(temp, res.data()); !written) {
                    error = written.unwrapErr();
                } else if (auto meta = ModMetadata::createFromGeodeFile(temp); meta.hasErrors()) {
                    error = "the download isn't a valid .geode";
                } else if (meta.getID() != Mod::get()->getID()) {
                    error = "the download is a different mod";
                } else if (meta.getVersion() != VersionInfo::parse(version).unwrap()) {
                    error = "the download is a different version";
                } else if (auto supported = meta.checkPlatformSupported(); !supported) {
                    error = supported.unwrapErr();
                } else if (auto compatible = meta.checkGameVersion(); !compatible) {
                    error = compatible.unwrapErr();
                } else if (auto compatible = meta.checkGeodeVersion(); !compatible) {
                    error = compatible.unwrapErr();
                } else if (!hasBinary(meta.getBinaryName().data())) {
                    error = "the download has no binary for this platform";
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
                    showFailed("Update failed", fmt::format("Couldn't install {}: {}", g_latest, error), true);
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
        Dialog::Content content;
        content.icon = icon::CLOUD_DOWN;
        content.header = fmt::format("Lazer UI {} is out", g_latest);
        content.body = fmt::format("You have {}.", Mod::get()->getVersion().toVString());
        content.items = notesItems(g_notes.empty() ? "No release notes for this version." : g_notes);
        content.listHeight = 300;
        content.buttons = {
            {"Update", Dialog::Kind::Ok, [] { install(); }, false},
            {"Later", Dialog::Kind::Cancel, nullptr},
        };
        present(std::move(content));
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
    if (g_state == State::Downloading) return showDownloading();
    if (g_state == State::Installed) return showRestartPrompt();

    Dialog::Content checking;
    checking.icon = icon::CLOUD_DOWN;
    checking.header = "Checking for updates";
    checking.body = "Looking for a newer Lazer UI on GitHub...";
    checking.buttons = {{"Cancel", Dialog::Kind::Cancel, nullptr}};
    present(std::move(checking));

    check([] {
        // Cancelled while checking: say nothing (an update still shows next start).
        if (!openDialog()) return;
        switch (g_state) {
            case State::Available:
                showUpdatePrompt();
                break;
            case State::UpToDate: {
                Dialog::Content content;
                content.icon = icon::CLOUD_DOWN;
                content.header = "You're up to date";
                content.body = fmt::format("{} is the newest Lazer UI.", Mod::get()->getVersion().toVString());
                content.buttons = {{"OK", Dialog::Kind::Cancel, nullptr}};
                present(std::move(content));
                break;
            }
            case State::Failed:
                showFailed("Update check failed", fmt::format("Couldn't check for updates: {}", g_error), true);
                break;
            default:
                break;
        }
    });
}

} // namespace lazer::updater
