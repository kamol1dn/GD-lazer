#include "SongSelectInternal.hpp"

#include <Geode/modify/GameManager.hpp>
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
    struct Fields {
        bool returnToSongSelect = false;
    };

    bool init(GJGameLevel* level, bool challenge) {
        // Consume this navigation intent once. A page opened later from a
        // creator profile belongs to that profile, not to the original page.
        bool fromSongSelect = lazer::SongSelect::returnsHere();
        lazer::SongSelect::returnsHere() = false;
        if (!LevelInfoLayer::init(level, challenge)) {
            lazer::SongSelect::returnsHere() = fromSongSelect;
            return false;
        }
        m_fields->returnToSongSelect = fromSongSelect;
        return true;
    }
    void onBack(CCObject* sender) {
        if (m_fields->returnToSongSelect && Mod::get()->getSettingValue<bool>("enabled")) {
            CCDirector::get()->replaceScene(CCTransitionFade::create(0.5f, lazer::SongSelect::scene()));
            return;
        }
        LevelInfoLayer::onBack(sender);
    }

#ifndef GEODE_IS_ANDROID
    // On Android, keyBackClicked is just onBack(nullptr), which is hooked above;
    // hooking a function that small spills the patch into the next one.
    void keyBackClicked() {
        if (m_fields->returnToSongSelect && Mod::get()->getSettingValue<bool>("enabled")) {
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

