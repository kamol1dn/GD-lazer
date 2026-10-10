#include "LevelPage.hpp"
#include "LevelPageInternal.hpp"

#include <Geode/Geode.hpp>
#include <Geode/modify/LevelInfoLayer.hpp>

using namespace geode::prelude;

// GD's page for an online level (LevelInfoLayer: what a level opens into,
// from our listing or anywhere else) shows as our page instead. GD's layer
// stays, hidden underneath, and keeps doing the work: downloading the level,
// playing it, liking and favouriting, its lists picker. Nothing is removed,
// so other mods' hooks on it keep working; "GD's page" puts it in front.
class $modify(LazerLevelPage, LevelInfoLayer) {
    struct Fields {
        lazer::LevelPage* page = nullptr;
        CCNode* backdrop = nullptr;
    };

    bool init(GJGameLevel* level, bool challenge) {
        if (!LevelInfoLayer::init(level, challenge)) return false;
        if (!lazer::LevelPage::wants(level, challenge)) return true;
        auto page = lazer::LevelPage::create(this);
        if (!page) return true;
        auto& f = m_fields;

        // A dark stage for the waves to rise over (FullscreenOverlay's Background6).
        auto backdrop = CCLayerColor::create(lazer::levelpage::SCHEME.background6());
        backdrop->setContentSize(CCDirector::get()->getWinSize());
        backdrop->setID("level-page-backdrop"_spr);
        this->addChild(backdrop, 99);
        page->setID("level-page"_spr);
        this->addChild(page, 100);
        f->page = page;
        f->backdrop = backdrop;
        this->hideVanilla();
        page->open();
        return true;
    }

    // Hide (not remove) GD's nodes so other mods hooking them keep working.
    // GD adds and shows more as the level arrives: hidden again each time.
    void hideVanilla() {
        auto& f = m_fields;
        if (!f->page || f->page->vanillaShown()) return;
        for (auto child : CCArrayExt<CCNode*>(this->getChildren())) {
            if (child == f->page || child == f->backdrop) continue;
            child->setVisible(false);
        }
        // The loading circle swallows every touch it gets.
        if (m_circle) m_circle->setTouchEnabled(false);
    }

    void levelDownloadFinished(GJGameLevel* level) {
        LevelInfoLayer::levelDownloadFinished(level);
        if (auto page = m_fields->page) {
            this->hideVanilla();
            page->levelChanged();
        }
    }

    void levelDownloadFailed(int response) {
        LevelInfoLayer::levelDownloadFailed(response);
        if (auto page = m_fields->page) {
            this->hideVanilla();
            page->downloadFailed();
        }
    }

    void levelUpdateFinished(GJGameLevel* level, UpdateResponse response) {
        LevelInfoLayer::levelUpdateFinished(level, response);
        if (auto page = m_fields->page) {
            this->hideVanilla();
            page->levelChanged();
        }
    }

    void likedItem(LikeItemType type, int id, bool liked) {
        LevelInfoLayer::likedItem(type, id, liked);
        if (auto page = m_fields->page) {
            this->hideVanilla();
            page->levelChanged();
        }
    }

    void onEnterTransitionDidFinish() {
        LevelInfoLayer::onEnterTransitionDidFinish();
        this->hideVanilla();
    }

    // Back is GD's own (it knows where the page came from); ours only closes
    // the page first.
    void onBack(CCObject* sender) {
        auto page = m_fields->page;
        if (page && !page->leaving() && !page->vanillaShown()) return page->goBack();
        LevelInfoLayer::onBack(sender);
    }

#ifndef GEODE_IS_ANDROID
    // On Android, keyBackClicked is just onBack(nullptr), hooked above: too
    // small to hook (the hook's patch spills into the next function).
    void keyBackClicked() {
        auto page = m_fields->page;
        if (page && !page->leaving() && !page->vanillaShown()) return page->goBack();
        LevelInfoLayer::keyBackClicked();
    }

    void keyDown(cocos2d::enumKeyCodes key, double timestamp) {
        auto page = m_fields->page;
        if (page && !page->vanillaShown()) {
            // GD's keys work its hidden buttons: only escape applies here, as
            // GD's back: CCLayer::keyDown makes it a back press, which goes to
            // what's on top first (a popup, comments, a profile) and is held
            // back while a scene fades.
            if (key == KEY_Escape) CCLayer::keyDown(key, timestamp);
            return;
        }
        LevelInfoLayer::keyDown(key, timestamp);
    }
#endif
};
