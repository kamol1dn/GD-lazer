// GD's main menu as osu!'s: the button system, the toolbar, the song ticker
// and a background that follows the music, over GD's hidden menu. The intro
// plays over the first menu after the game starts.

#include "MenuLayerInternal.hpp"

#include "../../audio/Sfx.hpp"
#include "../../integrations/LevelThumbnails.hpp"
#include "../../update/Updater.hpp"
#include "../core/Quips.hpp"
#include "../core/Text.hpp"
#include "../core/Theme.hpp"
#include "../select/SongSelect.hpp"
#include "../startup/IntroSequence.hpp"
#include "SideFlashes.hpp"

#include <Geode/Geode.hpp>

using namespace geode::prelude;
using lazer::ButtonSystem;
namespace icon = lazer::icon;

namespace {
    // The intro plays once, on the first menu after the game starts.
    bool g_introPlayed = false;
}

void showScene(CCScene* scene) {
    CCDirector::get()->replaceScene(CCTransitionFade::create(0.5f, scene));
}

bool LazerMenuLayer::init() {
    auto mod = Mod::get();
    bool intro = !g_introPlayed && mod->getSettingValue<bool>("enabled") && mod->getSettingValue<bool>("intro");
    g_introPlayed = true;
    // GD starts the menu music during init: hold the first song for the intro.
    if (intro) lazer::MusicPlayer::get().holdForIntro();

    if (!MenuLayer::init()) return false;
    // Not on the Geode index: look for updates on GitHub (even with the Lazer menu off).
    lazer::updater::onMenu(this, intro ? 4.f : 1.f);
    g_newLevelFlow = false;
    // Back at the menu: gameplay no longer returns to song select.
    lazer::SongSelect::returnsHere() = false;
    lazer::SongSelect::browsingOnline() = false;
    lazer::SongSelect::onlineReturn().reset();
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

    std::vector<ButtonSystem::ButtonDef> defs {
        {"settings", icon::GEAR, {85, 85, 85}, [this] { this->toggleSettings(); }, false, defaultSound, State::TopLevel, true},

        {"play", icon::PLAY, {102, 68, 204}, open(State::Play), false, lazer::sfx::sound::MENU_PLAY_SELECT},
        {"create", icon::PEN, {238, 170, 0}, [this] {
            lazer::quips::say("create", 0.2f);
            if (m_fields->buttons) m_fields->buttons->setState(State::Create);
        }, false, lazer::sfx::sound::MENU_PLAY_SELECT},
        {"browse", icon::COMPASS, {165, 204, 0}, [this] {
            lazer::quips::say("browse", 0.2f);
            if (m_fields->buttons) m_fields->buttons->setState(State::Browse);
        }, false},
        {"icons", icon::SHIRT, {0, 160, 200}, leave(State::TopLevel, [this] {
            lazer::quips::say("icons", 0.5f);
            this->onGarage(nullptr);
        })},
        {"exit", icon::CIRCLE_XMARK, {238, 51, 153}, [this] { this->onQuit(this); }, false},

        // play: everything you can play right away
        // Song select, one per kind of level: RobTop's levels and your saved ones in one list.
        {"classic", icon::CUBE, {102, 68, 204}, leave(State::Play, [] {
            showScene(lazer::SongSelect::scene(lazer::levels::Kind::Classic));
        }), true, lazer::sfx::sound::MENU_PLAY_SELECT, State::Play},
        {"platformer", icon::RUNNING, {102, 68, 204}, leave(State::Play, [] {
            showScene(lazer::SongSelect::scene(lazer::levels::Kind::Platformer));
        }), true, lazer::sfx::sound::MENU_PLAY_SELECT, State::Play},
        // The daily, weekly and event levels: song select with the current
        // one on top and the safe's history under it.
        {"daily", icon::CALENDAR_DAY, PLAY_SUB, leave(State::Play, [] {
            lazer::quips::say("daily", 0.35f);
            showScene(lazer::SongSelect::timelyScene(GJTimedLevelType::Daily));
        }), true, lazer::sfx::sound::MENU_PLAY_SELECT, State::Play},
        {"weekly", icon::CALENDAR_WEEK, PLAY_SUB, leave(State::Play, [] {
            lazer::quips::say("weekly", 0.35f);
            showScene(lazer::SongSelect::timelyScene(GJTimedLevelType::Weekly));
        }), true, lazer::sfx::sound::MENU_PLAY_SELECT, State::Play},
        {"event", icon::BOLT, PLAY_SUB, leave(State::Play, [] {
            lazer::quips::say("event", 0.35f);
            showScene(lazer::SongSelect::timelyScene(GJTimedLevelType::Event));
        }), true, lazer::sfx::sound::MENU_PLAY_SELECT, State::Play},

        // create: your own levels
        {"my levels", icon::FOLDER_OPEN, {238, 170, 0}, creator(State::Create, &CreatorLayer::onMyLevels), true, defaultSound, State::Create},
        {"new level", icon::SQUARE_PLUS, CREATE_SUB, leave(State::Create, [] {
            g_newLevelFlow = true;
            showScene(EditLevelLayer::scene(GameLevelManager::get()->createNewLevel()));
        }), true, defaultSound, State::Create},
        {"my lists", icon::LIST, CREATE_SUB, leave(State::Create, [] {
            showScene(LevelBrowserLayer::scene(GJSearchObject::create(SearchType::MyLists)));
        }), true, defaultSound, State::Create},

        // browse: other people's levels, as song select's online pages
        {"search", icon::SEARCH, {165, 204, 0}, leave(State::Browse, [] {
            showScene(lazer::SongSelect::onlineScene(lazer::browse::searchRequest("")));
        }), true, defaultSound, State::Browse},
        {"featured", icon::STAR, BROWSE_SUB, leave(State::Browse, [] {
            showScene(lazer::SongSelect::onlineScene(lazer::browse::pageRequest(SearchType::Featured)));
        }), true, defaultSound, State::Browse},
        {"lists", icon::LAYERS, BROWSE_SUB, leave(State::Browse, [] {
            showScene(lazer::SongSelect::onlineScene(lazer::browse::pageRequest(SearchType::Featured, true)));
        }), true, defaultSound, State::Browse},
        {"hall of fame", icon::AWARD, BROWSE_SUB, leave(State::Browse, [] {
            showScene(lazer::SongSelect::onlineScene(lazer::browse::pageRequest(SearchType::HallOfFame)));
        }), true, defaultSound, State::Browse},
        {"magic", icon::WAND_MAGIC, BROWSE_SUB, leave(State::Browse, [] {
            showScene(lazer::SongSelect::onlineScene(lazer::browse::pageRequest(SearchType::Magic)));
        }), true, defaultSound, State::Browse},
        {"recent", icon::CLOCK, BROWSE_SUB, leave(State::Browse, [] {
            showScene(lazer::SongSelect::onlineScene(lazer::browse::pageRequest(SearchType::Recent)));
        }), true, defaultSound, State::Browse},
    };
    // Levels sent for a rating: GD only shows that list to players with rating power.
    if (GameManager::get()->m_hasRP.value() > 0) {
        defs.push_back({"sent", icon::PAPER_PLANE, BROWSE_SUB, leave(State::Browse, [] {
            showScene(lazer::SongSelect::onlineScene(lazer::browse::pageRequest(SearchType::Sent)));
        }), true, defaultSound, State::Browse});
    }
    auto buttons = ButtonSystem::create(std::move(defs));
    buttons->setID("button-system"_spr);
    this->addChild(buttons, 10);
    m_fields->buttons = buttons;

    auto toolbar = lazer::Toolbar::create();
    toolbar->setID("toolbar"_spr);
    this->addChild(toolbar, 20);
    m_fields->toolbar = toolbar;

    toolbar->addLeft({lazer::makeIcon(icon::GEAR, 1), "settings", [this] { this->toggleSettings(); }});
    // Home closes whatever is open (like osu!'s CloseAllOverlays); with nothing open, back one menu.
    toolbar->addLeft({lazer::makeIcon(icon::HOUSE, 1), "home", [this, buttons] {
        if (!this->closeAllOverlays()) buttons->back();
    }});
    // Gauntlets open as song select's pack list (each with its five levels);
    // back from it lands on the menu you left.
    toolbar->addLeft({lazer::makeIcon(icon::FIST, 1), "gauntlets", [this] {
        lazer::quips::say("gauntlets", 0.6f);
        g_returnState = m_fields->buttons ? m_fields->buttons->getState() : ButtonSystem::State::TopLevel;
        showScene(lazer::SongSelect::gauntletScene());
    }});
    // Map packs open as song select's pack list; back from it lands on the menu you left.
    toolbar->addLeft({lazer::makeIcon(icon::BOXES, 1), "map packs", [this] {
        g_returnState = m_fields->buttons ? m_fields->buttons->getState() : ButtonSystem::State::TopLevel;
        showScene(lazer::SongSelect::scene(lazer::levels::Kind::MapPacks));
    }});

    buttons->setStateCallback([toolbar](ButtonSystem::State state) {
        // Back in a menu: nothing left to restore on the next menu load.
        if (state != ButtonSystem::State::EnteringMode) g_returnState = ButtonSystem::State::Initial;
        // osu! shows the toolbar once the logo lands in the button bar.
        if (state == ButtonSystem::State::Initial) toolbar->hide();
        else toolbar->show();
    });

    // Song ticker at the top right, under the toolbar.
    auto win = CCDirector::sharedDirector()->getWinSize();
    float k = lazer::unitScale();
    auto ticker = lazer::SongTicker::create(k);
    ticker->setPosition({win.width - 15 * k, win.height - toolbar->height() - 5 * k});
    this->addChild(ticker, 12);
    m_fields->ticker = ticker;

    // A word to the testers along the bottom edge (osu!'s development build notice).
    auto notice = lazer::makeText("You're one of few valuable testers, expect rare crashes", lazer::Weight::Regular, 16 * k);
    notice->setColor({255, 255, 255});
    notice->setOpacity(190);
    notice->setPosition({win.width / 2, 12 * k});
    notice->setID("tester-notice"_spr);
    this->addChild(notice, 12);

    // Follow the music: new song -> ticker + that level's thumbnail as the background.
    this->addChild(lazer::MusicListener::create([this](auto track, auto) { this->onTrackChanged(track); }));
    if (lazer::MusicPlayer::get().isActive()) this->onTrackChanged(lazer::MusicPlayer::get().current());

    // Collect vanilla + mod buttons next frame, after other mods' MenuLayer hooks ran.
    Loader::get()->queueInMainThread([self = Ref(this)] {
        static_cast<LazerMenuLayer*>(self.data())->collectToolbarButtons();
    });

    log::debug("Menu init: return {}", static_cast<int>(g_returnState));
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

void LazerMenuLayer::setupBackground() {
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

void LazerMenuLayer::onTrackChanged(lazer::MusicPlayer::Track const* track) {
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
    auto show = [self, request](CCTexture2D* texture) {
        auto layer = static_cast<LazerMenuLayer*>(self.data());
        if (layer->m_fields->backgroundRequest != request || !layer->m_fields->background) return;
        layer->m_fields->background->setImage(texture);
    };
    if (track->songID < 0) {
        // A main level's song: that level's screenshot (level N uses audio track N - 1).
        lazer::thumbnails::fetchOfficial(-track->songID, show);
        return;
    }
    lazer::thumbnails::fetchFirst(track->levelIDs(), [show](CCTexture2D* texture, int) { show(texture); });
}
