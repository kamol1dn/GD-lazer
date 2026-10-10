#include "LevelListingOverlay.hpp"

#include <Geode/Geode.hpp>
#include <Geode/modify/LevelBrowserLayer.hpp>

using namespace geode::prelude;

// GD's LevelBrowserLayer shows your levels and lists (and the ones you
// uploaded); for those it stays hidden under our page and keeps doing the
// work: fetching your uploads as the browser's LevelManagerDelegate, handing
// them to the page, and making new levels and lists. Nothing is removed, so
// other mods' hooks on it keep working.
class $modify(LazerLevelListing, LevelBrowserLayer) {
    struct Fields {
        lazer::LevelListingOverlay* page = nullptr;
        CCNode* backdrop = nullptr;
        // GD answered from its cache while the layer was still being built.
        Ref<CCArray> pendingLevels;
        std::string pendingKey;
        bool pendingLoaded = false;
        bool pendingFailed = false;
        std::string pendingInfo, pendingInfoKey;
    };

    bool init(GJSearchObject* search) {
        if (!LevelBrowserLayer::init(search)) return false;
        if (!lazer::LevelListingOverlay::wants(search)) return true;
        // GD's pickers (a level's "add to list" shows your lists over it) and
        // LevelListLayer (a browser too) keep GD's own screen.
        if (m_isOverlay || typeinfo_cast<LevelListLayer*>(static_cast<LevelBrowserLayer*>(this))) return true;
        auto page = lazer::LevelListingOverlay::create(this, search);
        if (!page) return true;
        auto& f = m_fields;

        // A dark stage for the waves to rise over (FullscreenOverlay's Background6).
        auto backdrop = CCLayerColor::create(lazer::LevelListingOverlay::schemeFor(search).background6());
        backdrop->setContentSize(CCDirector::get()->getWinSize());
        this->addChild(backdrop, 99);
        page->setID("level-listing"_spr);
        this->addChild(page, 100);
        f->page = page;
        f->backdrop = backdrop;
        this->hideVanilla();
        page->open();

        if (!f->pendingInfoKey.empty()) page->pageInfo(f->pendingInfo, f->pendingInfoKey.c_str());
        if (f->pendingLoaded) page->levelsLoaded(f->pendingLevels.data(), f->pendingKey.c_str());
        else if (f->pendingFailed) page->levelsFailed(f->pendingKey.c_str());
        f->pendingLevels = nullptr;
        return true;
    }

    // Hide (not remove) GD's nodes so other mods hooking them keep working.
    // Its list gets rebuilt with every page: hidden again each time.
    void hideVanilla() {
        auto& f = m_fields;
        for (auto child : CCArrayExt<CCNode*>(this->getChildren())) {
            if (child == f->page || child == f->backdrop) continue;
            child->setVisible(false);
        }
        // The hidden list would still scroll under a finger, and the loading
        // circle swallows every touch it gets.
        if (m_list && m_list->m_listView && m_list->m_listView->m_tableView) {
            m_list->m_listView->m_tableView->setTouchEnabled(false);
        }
        if (m_circle) m_circle->setTouchEnabled(false);
        // Its list took the mouse wheel as it was built (GD feeds the newest
        // delegate only): the page's scroll area takes it back.
        if (f->page) f->page->claimWheel();
    }

    // Your levels on this device aren't a download: GD fills its (hidden)
    // list straight away, without the delegate calls below.
    void setupLevelBrowser(CCArray* items) {
        LevelBrowserLayer::setupLevelBrowser(items);
        if (m_fields->page) this->hideVanilla();
    }

    void loadLevelsFinished(CCArray* levels, char const* key, int type) {
        LevelBrowserLayer::loadLevelsFinished(levels, key, type);
        auto& f = m_fields;
        if (f->page) {
            this->hideVanilla();
            f->page->levelsLoaded(levels, key);
        } else if (key) {
            f->pendingLevels = levels;
            f->pendingKey = key;
            f->pendingLoaded = true;
            f->pendingFailed = false;
        }
    }

    void loadLevelsFailed(char const* key, int type) {
        LevelBrowserLayer::loadLevelsFailed(key, type);
        auto& f = m_fields;
        if (f->page) {
            this->hideVanilla();
            f->page->levelsFailed(key);
        } else if (key) {
            f->pendingKey = key;
            f->pendingFailed = true;
            f->pendingLoaded = false;
        }
    }

    void setupPageInfo(gd::string info, char const* key) {
        LevelBrowserLayer::setupPageInfo(info, key);
        auto& f = m_fields;
        if (f->page) f->page->pageInfo(std::string(info), key);
        else if (key) {
            f->pendingInfo = std::string(info);
            f->pendingInfoKey = key;
        }
    }

    // Back goes where the player came from (the menu or song select), not to
    // GD's search screen.
    void onBack(CCObject* sender) {
        if (m_fields->page) return m_fields->page->goBack();
        LevelBrowserLayer::onBack(sender);
    }

#ifndef GEODE_IS_ANDROID
    // On Android, keyBackClicked is just onBack(nullptr), hooked above: too
    // small to hook (the hook's patch spills into the next function).
    void keyBackClicked() {
        if (m_fields->page) return m_fields->page->goBack();
        LevelBrowserLayer::keyBackClicked();
    }

    void keyDown(cocos2d::enumKeyCodes key, double timestamp) {
        if (auto page = m_fields->page) {
            // GD's arrow keys page through its hidden list: nothing else applies
            // here. Escape as GD's back (see LevelPageHooks.cpp).
            if (key == KEY_Escape) CCLayer::keyDown(key, timestamp);
            return;
        }
        LevelBrowserLayer::keyDown(key, timestamp);
    }
#endif
};
