#include <Geode/Geode.hpp>
#include <Geode/modify/CreatorLayer.hpp>
#include <Geode/modify/MenuLayer.hpp>

#include "audio/MusicPlayer.hpp"
#include "integrations/LevelThumbnails.hpp"
#include "integrations/ModIntegrations.hpp"
#include "settings/Account.hpp"
#include "settings/SettingsContent.hpp"
#include "ui/core/Text.hpp"
#include "ui/menu/AccountPanel.hpp"
#include "ui/menu/ButtonSystem.hpp"
#include "ui/menu/MenuBackground.hpp"
#include "ui/menu/NowPlayingOverlay.hpp"
#include "ui/menu/SideFlashes.hpp"
#include "ui/menu/SongTicker.hpp"
#include "ui/menu/Toolbar.hpp"
#include "ui/overlays/AchievementsOverlay.hpp"
#include "ui/overlays/RewardsOverlay.hpp"
#include "ui/overlays/SettingsOverlay.hpp"
#include "ui/overlays/StatsOverlay.hpp"
#include "ui/startup/IntroSequence.hpp"

#include <Geode/fmod/fmod.hpp>
#include <unordered_map>

using namespace geode::prelude;
using lazer::ButtonSystem;
namespace icon = lazer::icon;

namespace {
    // Set when a button leaves the menu, so coming back re-opens the menu it
    // was in (top level or a submenu). Initial = nothing to restore.
    ButtonSystem::State g_returnState = ButtonSystem::State::Initial;
    // The intro plays once, on the first menu after the game starts.
    bool g_introPlayed = false;
    // GD's "quit game?" popup, so its "yes" can play the outro first.
    FLAlertLayer* g_quitAlert = nullptr;
    bool g_exiting = false;

    constexpr float OUTRO_MS = 1200;

    // The outro (osu!'s IntroScreen.OnResuming without the voice): the music
    // fades out while the screen goes to black, then `done` quits.
    class Outro : public CCLayerColor {
    public:
        static Outro* create(std::function<void()> done) {
            auto ret = new Outro();
            ret->m_done = std::move(done);
            ret->initWithColor({0, 0, 0, 0});
            ret->autorelease();
            ret->setTouchEnabled(true);
            ret->scheduleUpdate();
            return ret;
        }

        void registerWithTouchDispatcher() override {
            CCDirector::get()->getTouchDispatcher()->addTargetedDelegate(this, -600, true);
        }
        bool ccTouchBegan(CCTouch*, CCEvent*) override { return true; }

        void update(float dt) override {
            m_ms += dt * 1000.f;
            float t = std::min(1.f, m_ms / OUTRO_MS);
            this->setOpacity(static_cast<GLubyte>(lazer::ease(lazer::Easing::InSine, t) * 255));
            if (auto channel = FMODAudioEngine::get()->getActiveMusicChannel(0)) {
                channel->setVolume(1.f - static_cast<float>(lazer::ease(lazer::Easing::Out, t)));
            }
            if (t >= 1.f && m_done) {
                auto done = std::move(m_done);
                m_done = nullptr;
                done();
            }
        }

    private:
        std::function<void()> m_done;
        float m_ms = 0;
    };

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

// GD's CreatorLayer is the old hub for everything online. Its pages are now
// reached from the button system and the toolbar, through a hidden instance
// (its handlers show GD's own screens and popups).
void creatorAction(void (CreatorLayer::*handler)(CCObject*)) {
    static Ref<CreatorLayer> layer;
    layer = CreatorLayer::create();
    if (!layer) return;
    // Handlers poke at sprites that only exist in the visible hub (the quests
    // badge, the vault door): give them stand-ins.
    for (auto sprite : {&layer->m_questsSprite, &layer->m_secretDoorSprite}) {
        if (!*sprite) {
            *sprite = CCSprite::create();
            layer->addChild(*sprite);
        }
    }
    (layer.data()->*handler)(nullptr);
}

void showScene(CCScene* scene) {
    CCDirector::get()->replaceScene(CCTransitionFade::create(0.5f, scene));
}

// Screens that go "back" to CreatorLayer come back to the menu instead.
class $modify(LazerCreatorLayer, CreatorLayer) {
    static CCScene* scene() {
        auto mod = Mod::get();
        if (!mod->getSettingValue<bool>("enabled")) return CreatorLayer::scene();
        if (g_returnState == ButtonSystem::State::Initial) g_returnState = ButtonSystem::State::TopLevel;
        return MenuLayer::scene(false);
    }
};

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
        lazer::AccountPanel* account = nullptr;
        lazer::SongTicker* ticker = nullptr;
        int backgroundRequest = 0; // newest thumbnail request; older results are dropped
    };

    bool init() {
        auto mod = Mod::get();
        bool intro = !g_introPlayed && mod->getSettingValue<bool>("enabled") && mod->getSettingValue<bool>("intro");
        g_introPlayed = true;
        // GD starts the menu music during init: hold the first song for the intro.
        if (intro) lazer::MusicPlayer::get().holdForIntro();

        if (!MenuLayer::init()) return false;
        if (!mod->getSettingValue<bool>("enabled")) return true;

        for (auto id : HIDDEN_NODES) {
            // Hide (not remove) vanilla nodes so other mods hooking them keep working.
            if (auto node = this->getChildByID(id)) node->setVisible(false);
        }

        this->setupBackground();

        using State = ButtonSystem::State;
        // A button that leaves the menu, remembering which menu to come back to.
        auto leave = [](State from, auto fn) {
            return [from, fn] {
                g_returnState = from;
                fn();
            };
        };
        auto creator = [leave](State from, void (CreatorLayer::*handler)(CCObject*)) {
            return leave(from, [handler] { creatorAction(handler); });
        };

        // The ButtonSystem* is only known after create(); submenu buttons reach it through the fields.
        auto open = [this](State state) {
            return [this, state] { if (m_fields->buttons) m_fields->buttons->setState(state); };
        };

        constexpr ccColor3B PLAY_SUB {94, 63, 186};
        constexpr ccColor3B CREATE_SUB {220, 160, 0};
        constexpr ccColor3B BROWSE_SUB {140, 180, 0};
        auto defaultSound = lazer::sfx::sound::MENU_DEFAULT_SELECT;

        auto buttons = ButtonSystem::create({
            {"settings", icon::GEAR, {85, 85, 85}, [this] { this->toggleSettings(); }, false, defaultSound, State::TopLevel, true},

            {"play", icon::PLAY, {102, 68, 204}, open(State::Play), false, lazer::sfx::sound::MENU_PLAY_SELECT},
            {"create", icon::PEN, {238, 170, 0}, open(State::Create), false, lazer::sfx::sound::MENU_PLAY_SELECT},
            {"browse", icon::COMPASS, {165, 204, 0}, open(State::Browse), false},
            {"icons", icon::SHIRT, {0, 160, 200}, leave(State::TopLevel, [this] { this->onGarage(nullptr); })},
            {"exit", icon::CIRCLE_XMARK, {238, 51, 153}, [this] { this->onQuit(nullptr); }, false},

            // play: everything you can play right away
            {"solo", icon::RUNNING, {102, 68, 204}, leave(State::Play, [this] { this->onPlay(nullptr); }), true,
             lazer::sfx::sound::MENU_PLAY_SELECT, State::Play},
            {"saved", icon::BOOKMARK, PLAY_SUB, creator(State::Play, &CreatorLayer::onSavedLevels), true, defaultSound, State::Play},
            {"daily", icon::CALENDAR_DAY, PLAY_SUB, [] { creatorAction(&CreatorLayer::onDailyLevel); }, false, defaultSound, State::Play},
            {"gauntlets", icon::FIST, PLAY_SUB, creator(State::Play, &CreatorLayer::onGauntlets), true, defaultSound, State::Play},
            {"map packs", icon::BOXES, PLAY_SUB, creator(State::Play, &CreatorLayer::onMapPacks), true, defaultSound, State::Play},
            {"the tower", icon::CHESS_ROOK, PLAY_SUB, creator(State::Play, &CreatorLayer::onAdventureMap), true, defaultSound, State::Play},

            // create: your own levels
            {"my levels", icon::FOLDER_OPEN, {238, 170, 0}, creator(State::Create, &CreatorLayer::onMyLevels), true, defaultSound, State::Create},
            {"new level", icon::SQUARE_PLUS, CREATE_SUB, leave(State::Create, [] {
                showScene(EditLevelLayer::scene(GameLevelManager::get()->createNewLevel()));
            }), true, defaultSound, State::Create},
            {"my lists", icon::LIST, CREATE_SUB, leave(State::Create, [] {
                showScene(LevelBrowserLayer::scene(GJSearchObject::create(SearchType::MyLists)));
            }), true, defaultSound, State::Create},

            // browse: other people's levels
            {"search", icon::SEARCH, {165, 204, 0}, creator(State::Browse, &CreatorLayer::onOnlineLevels), true, defaultSound, State::Browse},
            {"featured", icon::STAR, BROWSE_SUB, creator(State::Browse, &CreatorLayer::onFeaturedLevels), true, defaultSound, State::Browse},
            {"lists", icon::LAYERS, BROWSE_SUB, creator(State::Browse, &CreatorLayer::onTopLists), true, defaultSound, State::Browse},
            {"hall of fame", icon::AWARD, BROWSE_SUB, leave(State::Browse, [] {
                showScene(LevelBrowserLayer::scene(GJSearchObject::create(SearchType::HallOfFame)));
            }), true, defaultSound, State::Browse},
        });
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

        if (g_returnState != ButtonSystem::State::Initial) {
            buttons->resume(g_returnState);
            g_returnState = ButtonSystem::State::Initial;
        }

        if (intro) {
            auto sequence = lazer::IntroSequence::create(buttons->logoRadius(), [this] {
                // The ticker ran behind the intro: show it again now it can be seen.
                auto& player = lazer::MusicPlayer::get();
                if (m_fields->ticker && player.isActive()) m_fields->ticker->show(player.current());
            });
            sequence->setID("intro"_spr);
            this->addChild(sequence, 1000);
        }
        return true;
    }

    // Some screens (solo's level select) are pushed over the menu rather than
    // replacing it, so going back returns to this same layer mid-"leaving".
    void onEnter() {
        MenuLayer::onEnter();
        auto buttons = m_fields->buttons;
        if (buttons && buttons->getState() == ButtonSystem::State::EnteringMode) {
            buttons->resume(g_returnState);
            g_returnState = ButtonSystem::State::Initial;
        }
    }

    void onQuit(CCObject* sender) {
        MenuLayer::onQuit(sender);
        // Remember GD's quit popup (the newest alert in the scene).
        g_quitAlert = nullptr;
        if (auto scene = CCDirector::get()->getRunningScene()) {
            for (auto child : CCArrayExt<CCNode*>(scene->getChildren())) {
                if (auto alert = typeinfo_cast<FLAlertLayer*>(child)) g_quitAlert = alert;
            }
        }
    }

    void FLAlert_Clicked(FLAlertLayer* layer, bool btn2) {
        auto mod = Mod::get();
        bool outro = btn2 && layer && layer == g_quitAlert && !g_exiting
            && mod->getSettingValue<bool>("enabled") && mod->getSettingValue<bool>("intro");
        g_quitAlert = nullptr;
        if (!outro) return MenuLayer::FLAlert_Clicked(layer, btn2);

        g_exiting = true;
        auto& f = m_fields;
        this->closeOverlaysExcept(nullptr);
        if (f->nowPlaying) f->nowPlaying->close();
        if (f->account) f->account->close();
        if (f->ticker) f->ticker->hide();
        if (f->buttons) f->buttons->playExit(OUTRO_MS);

        Ref<FLAlertLayer> alert = layer;
        Ref<MenuLayer> self = this;
        this->addChild(Outro::create([self, alert] {
            self->MenuLayer::FLAlert_Clicked(alert, true);
        }), 1000);
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

        // The rest of GD's creator hub.
        auto hub = [this](void (CreatorLayer::*handler)(CCObject*)) {
            return [this, handler] {
                g_returnState = m_fields->buttons ? m_fields->buttons->getState() : ButtonSystem::State::TopLevel;
                creatorAction(handler);
            };
        };
        toolbar->addRight({lazer::makeIcon(icon::RANKING_STAR, 1), "leaderboards", hub(&CreatorLayer::onLeaderboards)});
        toolbar->addRight({lazer::makeIcon(icon::LIST_CHECK, 1), "quests", hub(&CreatorLayer::onChallenge)});
        toolbar->addRight({lazer::makeIcon(icon::ROUTE, 1), "paths", hub(&CreatorLayer::onPaths)});
        toolbar->addRight({lazer::makeIcon(icon::CALENDAR_WEEK, 1), "weekly demon", hub(&CreatorLayer::onWeeklyLevel)});
        toolbar->addRight({lazer::makeIcon(icon::BOLT, 1), "event level", hub(&CreatorLayer::onEventLevel)});
        toolbar->addRight({lazer::makeIcon(icon::VAULT, 1), "vault", hub(&CreatorLayer::onSecretVault)});
        toolbar->addRight({lazer::makeIcon(icon::DUNGEON, 1), "treasure room", hub(&CreatorLayer::onTreasureRoom)});

        // Profile: the vanilla button lives in profile-menu (or main-menu on some setups).
        CCMenuItem* profile = nullptr;
        for (auto menuId : {"profile-menu", "main-menu"}) {
            if (auto menu = this->getChildByID(menuId)) {
                if (auto p = typeinfo_cast<CCMenuItem*>(menu->getChildByID("profile-button"))) profile = p;
            }
        }
        // The user button opens our account card; GD's profile page is one of its items.
        Ref<CCMenuItem> profileRef = profile;
        auto panel = lazer::AccountPanel::create(toolbar->height(), {
            [profileRef] { if (profileRef) profileRef->activate(); },
            [this] {
                g_returnState = m_fields->buttons ? m_fields->buttons->getState() : ButtonSystem::State::TopLevel;
                this->onGarage(nullptr);
            },
        });
        panel->setID("account"_spr);
        this->addChild(panel, 18);
        m_fields->account = panel;
        float avatar = toolbar->height() * 0.62f;
        toolbar->setUser(lazer::account::username(), lazer::integrations::playerIcon(false, avatar * 0.62f), [this] {
            if (m_fields->nowPlaying) m_fields->nowPlaying->close();
            m_fields->account->toggle();
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
        if (m_fields->account) m_fields->account->close();
        nowPlaying->toggle();
        if (nowPlaying->isOpen() && m_fields->ticker) m_fields->ticker->hide();
    }

    void keyBackClicked() {
        if (g_exiting) return;
        // Escape closes overlays, then collapses the button bar (like osu!), then GD's quit prompt.
        if (m_fields->nowPlaying && m_fields->nowPlaying->back()) return;
        if (m_fields->account && m_fields->account->back()) return;
        if (m_fields->settings && m_fields->settings->back()) return;
        if (m_fields->rewards && m_fields->rewards->back()) return;
        if (m_fields->achievements && m_fields->achievements->back()) return;
        if (m_fields->stats && m_fields->stats->back()) return;
        if (m_fields->buttons && m_fields->buttons->back()) return;
        MenuLayer::keyBackClicked();
    }
};
