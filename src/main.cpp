#include <Geode/Geode.hpp>
#include <Geode/modify/MenuLayer.hpp>

#include "audio/MusicPlayer.hpp"
#include "settings/SettingsContent.hpp"
#include "ui/AchievementsOverlay.hpp"
#include "ui/ButtonSystem.hpp"
#include "ui/LevelThumbnails.hpp"
#include "ui/MenuBackground.hpp"
#include "ui/NowPlayingOverlay.hpp"
#include "ui/RewardsOverlay.hpp"
#include "ui/SettingsOverlay.hpp"
#include "ui/SideFlashes.hpp"
#include "ui/SongTicker.hpp"
#include "ui/StatsOverlay.hpp"
#include "ui/Text.hpp"
#include "ui/Toolbar.hpp"

#include <unordered_map>

using namespace geode::prelude;
using lazer::ButtonSystem;
namespace icon = lazer::icon;

namespace {
    // Set when a button leaves the menu, so coming back re-opens the button bar like osu!.
    bool g_returnToTopLevel = false;

    // Vanilla menus whose buttons move into the toolbar. Mods often add buttons here too.
    constexpr std::array TOOLBAR_SOURCE_MENUS {
        "bottom-menu", "right-side-menu", "side-menu", "top-right-menu", "profile-menu",
    };
    // Everything else we hide.
    constexpr std::array HIDDEN_NODES {
        "main-menu", "main-title", "player-username", "social-media-menu",
        "more-games-menu", "close-menu",
    };

    struct KnownButton {
        char const* icon;
        char const* tooltip;
    };
    // Vanilla buttons get proper icons; anything unknown (other mods) keeps its own sprite.
    std::unordered_map<std::string, KnownButton> const KNOWN_BUTTONS {
        {"achievements-button", {icon::TROPHY, "achievements"}},
        {"stats-button", {icon::CHART, "statistics"}},
        {"newgrounds-button", {icon::MUSIC, "now playing"}},
        {"daily-chest-button", {icon::GIFT, "daily chests"}},
        {"geode.loader/geode-button", {icon::PUZZLE, "mods"}},
        // Globed (multiplayer mod): its button just says "main menu" otherwise.
        {"dankmeme.globed2/main-menu-button", {icon::GLOBE, "multiplayer"}},
    };
    // Already covered elsewhere (button bar / user section), so not duplicated.
    constexpr std::array SKIPPED_BUTTONS {"settings-button", "profile-button"};

    std::string prettyId(std::string id) {
        // "some.mod/cool-button" -> "cool"
        if (auto slash = id.rfind('/'); slash != std::string::npos) id = id.substr(slash + 1);
        if (id.ends_with("-button")) id.resize(id.size() - 7);
        for (auto& c : id) if (c == '-' || c == '_') c = ' ';
        return id;
    }
}

class $modify(LazerMenuLayer, MenuLayer) {
    struct Fields {
        ButtonSystem* buttons = nullptr;
        lazer::Toolbar* toolbar = nullptr;
        lazer::MenuBackground* background = nullptr;
        lazer::SettingsOverlay* settings = nullptr;
        lazer::RewardsOverlay* rewards = nullptr;
        lazer::AchievementsOverlay* achievements = nullptr;
        lazer::StatsOverlay* stats = nullptr;
        lazer::NowPlayingOverlay* nowPlaying = nullptr;
        lazer::SongTicker* ticker = nullptr;
        int backgroundRequest = 0; // newest thumbnail request; older results are dropped
    };

    bool init() {
        if (!MenuLayer::init()) return false;
        auto mod = Mod::get();
        if (!mod->getSettingValue<bool>("enabled")) return true;

        for (auto id : HIDDEN_NODES) {
            // Hide (not remove) vanilla nodes so other mods hooking them keep working.
            if (auto node = this->getChildByID(id)) node->setVisible(false);
        }

        this->setupBackground();

        auto leave = [](auto fn) {
            return [fn] {
                g_returnToTopLevel = true;
                fn();
            };
        };

        auto buttons = ButtonSystem::create(
            {
                {"settings", icon::GEAR, {85, 85, 85}, [this] { this->toggleSettings(); }, false},
            },
            {
                {"play", icon::PLAY, {102, 68, 204}, leave([this] { this->onPlay(nullptr); }), true, lazer::sfx::sound::MENU_PLAY_SELECT},
                {"create", icon::PEN, {238, 170, 0}, leave([this] { this->onCreator(nullptr); })},
                {"icons", icon::SHIRT, {165, 204, 0}, leave([this] { this->onGarage(nullptr); })},
                {"exit", icon::CIRCLE_XMARK, {238, 51, 153}, [this] { this->onQuit(nullptr); }, false},
            }
        );
        buttons->setID("button-system"_spr);
        this->addChild(buttons, 10);
        m_fields->buttons = buttons;

        auto toolbar = lazer::Toolbar::create();
        toolbar->setID("toolbar"_spr);
        this->addChild(toolbar, 20);
        m_fields->toolbar = toolbar;

        toolbar->addLeft({lazer::makeIcon(icon::GEAR, 1), "settings", [this] { this->toggleSettings(); }});
        toolbar->addLeft({lazer::makeIcon(icon::HOUSE, 1), "home", [buttons] { buttons->back(); }});

        buttons->setStateCallback([toolbar](ButtonSystem::State state) {
            // osu! shows the toolbar once the logo lands in the button bar.
            if (state == ButtonSystem::State::Initial) toolbar->hide();
            else toolbar->show();
        });

        // Song ticker at the top right, under the toolbar.
        auto win = CCDirector::sharedDirector()->getWinSize();
        float k = win.height / 768.f;
        auto ticker = lazer::SongTicker::create(k);
        ticker->setPosition({win.width - 15 * k, win.height - toolbar->height() - 5 * k});
        this->addChild(ticker, 12);
        m_fields->ticker = ticker;

        // Follow the music: new song -> ticker + that level's thumbnail as the background.
        this->addChild(lazer::MusicListener::create([this](auto track, auto) { this->onTrackChanged(track); }));
        if (lazer::MusicPlayer::get().isActive()) this->onTrackChanged(lazer::MusicPlayer::get().current());

        // Collect vanilla + mod buttons next frame, after other mods' MenuLayer hooks ran.
        Loader::get()->queueInMainThread([self = Ref(this)] {
            static_cast<LazerMenuLayer*>(self.data())->collectToolbarButtons();
        });

        if (g_returnToTopLevel) {
            g_returnToTopLevel = false;
            buttons->resumeTopLevel();
        }
        return true;
    }

    void setupBackground() {
        auto mod = Mod::get();
        auto source = this->getChildByID("main-menu-bg");
        if (!source) return;

        auto bg = lazer::MenuBackground::create(
            source,
            mod->getSettingValue<int64_t>("background-dim") / 100.f,
            mod->getSettingValue<bool>("background-blur"),
            mod->getSettingValue<bool>("background-triangles")
        );
        bg->setID("background"_spr);
        // Beat flashes at the screen edges, over the background.
        bg->addChild(lazer::SideFlashes::create(), 3);
        m_fields->background = bg;
        // Draw right after GD's background, before everything else at the same z.
        this->addChild(bg, source->getZOrder());
        bg->setOrderOfArrival(source->getOrderOfArrival());
    }

    void collectToolbarButtons() {
        auto toolbar = m_fields->toolbar;
        if (!toolbar) return;

        for (auto menuId : TOOLBAR_SOURCE_MENUS) {
            auto menu = this->getChildByID(menuId);
            if (!menu) continue;

            for (auto item : CCArrayExt<CCMenuItem*>(menu->getChildren())) {
                if (!typeinfo_cast<CCMenuItem*>(item) || !item->isVisible()) continue;
                std::string id = item->getID();
                if (std::find(SKIPPED_BUTTONS.begin(), SKIPPED_BUTTONS.end(), id) != SKIPPED_BUTTONS.end()) continue;

                CCNode* iconNode = nullptr;
                std::string tooltip;
                if (auto known = KNOWN_BUTTONS.find(id); known != KNOWN_BUTTONS.end()) {
                    iconNode = lazer::makeIcon(known->second.icon, 1);
                    tooltip = known->second.tooltip;
                } else {
                    // Another mod's button: keep its own look.
                    if (auto sprite = typeinfo_cast<CCMenuItemSprite*>(item)) {
                        if (auto normal = sprite->getNormalImage()) iconNode = lazer::snapshotNode(normal);
                    }
                    tooltip = id.empty() ? "" : prettyId(id);
                }

                Ref<CCMenuItem> target = item;
                std::function<void()> action = [target] { target->activate(); };
                // Daily chests get our own overlay instead of GD's popup.
                if (id == "daily-chest-button") action = [this] { this->toggleRewards(); };
                if (id == "achievements-button") action = [this] { this->toggleAchievements(); };
                if (id == "stats-button") action = [this] { this->toggleStats(); };
                // The music button opens the player (GD's song browser is in settings > audio).
                if (id == "newgrounds-button" && Mod::get()->getSettingValue<bool>("music-player")) {
                    action = [this] { this->toggleNowPlaying(); };
                }
                toolbar->addRight({iconNode, tooltip, action});
            }
            menu->setVisible(false);
        }

        // Profile: the vanilla button lives in profile-menu (or main-menu on some setups).
        CCMenuItem* profile = nullptr;
        for (auto menuId : {"profile-menu", "main-menu"}) {
            if (auto menu = this->getChildByID(menuId)) {
                if (auto p = typeinfo_cast<CCMenuItem*>(menu->getChildByID("profile-button"))) profile = p;
            }
        }
        std::string name = GJAccountManager::get()->m_username;
        if (name.empty()) name = GameManager::get()->m_playerName;
        if (name.empty()) name = "guest";
        Ref<CCMenuItem> profileRef = profile;
        toolbar->setUser(name, [profileRef] {
            if (profileRef) profileRef->activate();
        });
    }

    void toggleSettings() {
        auto& settings = m_fields->settings;
        if (!settings) {
            // Built on first use: it reads GD's option list from a hidden options layer.
            settings = lazer::SettingsOverlay::create(m_fields->toolbar ? m_fields->toolbar->height() : 0);
            settings->setID("settings"_spr);
            lazer::buildSettings(settings, this, m_fields->background);
            this->addChild(settings, 15);
        }
        if (settings->isOpen()) {
            settings->close();
        } else {
            closeOverlaysExcept(settings);
            settings->open();
        }
    }

    void toggleRewards() {
        auto& rewards = m_fields->rewards;
        if (!rewards) {
            rewards = lazer::RewardsOverlay::create(m_fields->toolbar ? m_fields->toolbar->height() : 0);
            rewards->setID("rewards"_spr);
            this->addChild(rewards, 16);
        }
        if (rewards->isOpen()) {
            rewards->close();
        } else {
            closeOverlaysExcept(rewards);
            rewards->open();
        }
    }

    void toggleAchievements() {
        auto& achievements = m_fields->achievements;
        if (!achievements) {
            achievements = lazer::AchievementsOverlay::create(m_fields->toolbar ? m_fields->toolbar->height() : 0);
            achievements->setID("achievements"_spr);
            this->addChild(achievements, 16);
        }
        if (achievements->isOpen()) {
            achievements->close();
        } else {
            closeOverlaysExcept(achievements);
            achievements->open();
        }
    }

    void toggleStats() {
        auto& stats = m_fields->stats;
        if (!stats) {
            stats = lazer::StatsOverlay::create(m_fields->toolbar ? m_fields->toolbar->height() : 0);
            stats->setID("statistics"_spr);
            this->addChild(stats, 16);
        }
        if (stats->isOpen()) {
            stats->close();
        } else {
            closeOverlaysExcept(stats);
            stats->open();
        }
    }

    // Full-screen overlays replace each other, like osu!'s.
    void closeOverlaysExcept(CCNode* keep) {
        auto& f = m_fields;
        if (f->settings && f->settings != keep) f->settings->close();
        if (f->rewards && f->rewards != keep) f->rewards->close();
        if (f->achievements && f->achievements != keep) f->achievements->close();
        if (f->stats && f->stats != keep) f->stats->close();
    }

    void onTrackChanged(lazer::MusicPlayer::Track const* track) {
        auto nowPlaying = m_fields->nowPlaying;
        if (m_fields->ticker && !(nowPlaying && nowPlaying->isOpen())) m_fields->ticker->show(track);

        if (!m_fields->background) return;
        int request = ++m_fields->backgroundRequest;
        if (!track) {
            m_fields->background->setImage(nullptr);
            return;
        }
        // No thumbnail for any of the song's levels: keep GD's own menu scene.
        Ref<MenuLayer> self = this;
        lazer::thumbnails::fetchFirst(track->levelIDs(), [self, request](CCTexture2D* texture, int) {
            auto layer = static_cast<LazerMenuLayer*>(self.data());
            if (layer->m_fields->backgroundRequest != request || !layer->m_fields->background) return;
            layer->m_fields->background->setImage(texture);
        });
    }

    void toggleNowPlaying() {
        auto& nowPlaying = m_fields->nowPlaying;
        if (!nowPlaying) {
            nowPlaying = lazer::NowPlayingOverlay::create(m_fields->toolbar ? m_fields->toolbar->height() : 0);
            nowPlaying->setID("now-playing"_spr);
            this->addChild(nowPlaying, 18);
        }
        nowPlaying->toggle();
        if (nowPlaying->isOpen() && m_fields->ticker) m_fields->ticker->hide();
    }

    void keyBackClicked() {
        // Escape closes overlays, then collapses the button bar (like osu!), then GD's quit prompt.
        if (m_fields->nowPlaying && m_fields->nowPlaying->back()) return;
        if (m_fields->settings && m_fields->settings->back()) return;
        if (m_fields->rewards && m_fields->rewards->back()) return;
        if (m_fields->achievements && m_fields->achievements->back()) return;
        if (m_fields->stats && m_fields->stats->back()) return;
        if (m_fields->buttons && m_fields->buttons->back()) return;
        MenuLayer::keyBackClicked();
    }
};
