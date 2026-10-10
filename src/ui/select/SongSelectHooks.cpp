#include "SongSelectInternal.hpp"

#include <Geode/modify/DailyLevelPage.hpp>
#include <Geode/modify/GameManager.hpp>
#include <Geode/modify/GauntletLayer.hpp>
#include <Geode/modify/GauntletSelectLayer.hpp>
#include <Geode/modify/LevelInfoLayer.hpp>
#include <Geode/modify/PlayLayer.hpp>

using namespace geode::prelude;

// Leaving gameplay or GD's level page returns to song select when that's where
// the player came from.
class $modify(SongSelectReturn, GameManager) {
    void returnToLastScene(GJGameLevel* level) {
        if (lazer::SongSelect::returnsHere() && Mod::get()->getSettingValue<bool>("enabled")) {
            CCDirector::get()->replaceScene(CCTransitionFade::create(0.5f, lazer::SongSelect::scene()));
            return;
        }
        GameManager::returnToLastScene(level);
    }
};

class $modify(SongSelectLevelPage, LevelInfoLayer) {
    // Only the level page song select opened returns to it. One reached from
    // there (a profile's levels, opened from the page's creator) is a step
    // away: its back is GD's, and so is leaving a level played from it.
    bool init(GJGameLevel* level, bool challenge) {
        if (lazer::SongSelect::openingLevelPage()) lazer::SongSelect::openingLevelPage() = false;
        else lazer::SongSelect::returnsHere() = false;
        return LevelInfoLayer::init(level, challenge);
    }

    void onBack(CCObject* sender) {
        if (lazer::SongSelect::returnsHere() && Mod::get()->getSettingValue<bool>("enabled")) {
            CCDirector::get()->replaceScene(CCTransitionFade::create(0.5f, lazer::SongSelect::scene()));
            return;
        }
        LevelInfoLayer::onBack(sender);
    }

#ifndef GEODE_IS_ANDROID
    // On Android, keyBackClicked is just onBack(nullptr), which is hooked above;
    // hooking a function that small spills the patch into the next one.
    void keyBackClicked() {
        if (lazer::SongSelect::returnsHere() && Mod::get()->getSettingValue<bool>("enabled")) {
            this->onBack(nullptr);
            return;
        }
        LevelInfoLayer::keyBackClicked();
    }
#endif
};

// A level built behind song select's loader isn't on screen until the push:
// GD pausing it when the game loses focus would put its pause menu over song
// select. Once it's entered, pausing is GD's as usual.
class $modify(SongSelectPreloadedLevel, PlayLayer) {
    void pauseGame(bool unfocused) {
        if (!this->isRunning()) return;
        PlayLayer::pauseGame(unfocused);
    }

    // Leaving the level: where its song is (paused, or still going after a
    // completion) is where song select's preview carries on. A level played
    // without its song has none: the preview's own position stands.
    void onQuit() {
        auto engine = FMODAudioEngine::sharedEngine();
        std::string path = engine->getActiveMusic(0);
        if (!path.empty() && lazer::SongSelect::returnsHere()) lazer::g_resume = {path, engine->getMusicTimeMS(0)};
        PlayLayer::onQuit();
    }
};


// GD's daily, weekly and event popups (the creator hub's buttons, other
// mods') open as our pages instead. The page GD made was already wired up as
// the server's delegate: unhooked before it goes.
class $modify(SongSelectDailyPage, DailyLevelPage) {
    void show() {
        if (this->getUserObject("hidden"_spr) || !Mod::get()->getSettingValue<bool>("enabled")) return DailyLevelPage::show();
        auto glm = GameLevelManager::sharedState();
        if (glm->m_GJDailyLevelDelegate == this) glm->m_GJDailyLevelDelegate = nullptr;
        if (glm->m_levelDownloadDelegate == this) glm->m_levelDownloadDelegate = nullptr;
        CCDirector::get()->replaceScene(CCTransitionFade::create(0.5f, lazer::SongSelect::timelyScene(m_type)));
    }
};

// And GD's gauntlet screens (the list, and one gauntlet's levels).
class $modify(SongSelectGauntlets, GauntletSelectLayer) {
    static CCScene* scene(int unused) {
        if (Mod::get()->getSettingValue<bool>("enabled")) return lazer::SongSelect::gauntletScene();
        return GauntletSelectLayer::scene(unused);
    }
};

class $modify(SongSelectGauntlet, GauntletLayer) {
    static CCScene* scene(GauntletType type) {
        if (Mod::get()->getSettingValue<bool>("enabled")) return lazer::SongSelect::gauntletScene(static_cast<int>(type));
        return GauntletLayer::scene(type);
    }
};
