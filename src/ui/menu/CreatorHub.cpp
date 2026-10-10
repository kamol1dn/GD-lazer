// GD's creator hub, kept hidden: the menu reaches its pages, and the buttons
// other mods add to it, through hidden instances, and GD's screens that go
// back to the hub (or to "my levels" after a new level) come back to the menu
// instead.

#include "MenuLayerInternal.hpp"

#include "../overlays/LevelListingOverlay.hpp"
#include "../select/SongSelect.hpp"

#include <Geode/Geode.hpp>
#include <Geode/modify/CreatorLayer.hpp>
#include <Geode/modify/LevelBrowserLayer.hpp>

using namespace geode::prelude;
using lazer::ButtonSystem;

// GD's CreatorLayer is the old hub for everything online. Its pages are now
// reached from the button system and the toolbar, through a hidden instance
// (its handlers show GD's own screens and popups).
void creatorAction(void (CreatorLayer::*handler)(CCObject*)) {
    log::debug("Creator hub action");
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
    // Handlers use the button that was clicked (quests reads it straight
    // away): hand them the hub's own button wired to the same handler.
    // Fallback: a stand-in menu button with an image (quests reads the
    // button's image to clear its "!" badge).
    auto menu = CCMenu::create();
    auto standIn = CCMenuItemSpriteExtra::create(CCSprite::create(), nullptr, nullptr);
    menu->addChild(standIn);
    layer->addChild(menu);
    CCObject* sender = standIn;
    auto target = static_cast<SEL_MenuHandler>(handler);
    std::function<CCMenuItem*(CCNode*)> find = [&](CCNode* node) -> CCMenuItem* {
        for (auto child : CCArrayExt<CCNode*>(node->getChildren())) {
            if (auto item = typeinfo_cast<CCMenuItem*>(child); item && item->m_pfnSelector == target) return item;
            if (auto found = find(child)) return found;
        }
        return nullptr;
    };
    if (auto item = find(layer.data())) sender = item;
    (layer.data()->*handler)(sender);
}

// GD's own hub buttons (Geode's node IDs); anything else in a hub menu is a mod's.
constexpr std::array VANILLA_CREATOR_BUTTONS = {
    "create-button", "saved-button", "scores-button", "quests-button", "daily-button",
    "weekly-button", "event-button", "gauntlets-button", "featured-button", "lists-button",
    "paths-button", "map-packs-button", "search-button", "map-button", "versus-button",
    "exit-button", "back-button", "vault-button", "treasure-room-button", "secret-door-button", "leaderboards-button",
};

std::vector<CreatorModButton> scanCreatorModButtons(CreatorLayer* layer) {
    std::vector<CreatorModButton> found;
    std::vector<unsigned> path;
    std::function<void(CCNode*, bool)> walk = [&](CCNode* node, bool inMenu) {
        unsigned i = 0;
        for (auto child : CCArrayExt<CCNode*>(node->getChildren())) {
            path.push_back(i++);
            if (auto item = typeinfo_cast<CCMenuItem*>(child); item && inMenu) {
                std::string id = item->getID();
                bool vanilla = std::find(VANILLA_CREATOR_BUTTONS.begin(), VANILLA_CREATOR_BUTTONS.end(), id) != VANILLA_CREATOR_BUTTONS.end();
                if (!vanilla && item->isVisible()) {
                    CCNode* image = nullptr;
                    if (auto sprite = typeinfo_cast<CCMenuItemSprite*>(item)) image = sprite->getNormalImage();
                    found.push_back({path, id, image});
                }
            } else if (child->isVisible()) {
                walk(child, inMenu || typeinfo_cast<CCMenu*>(child));
            }
            path.pop_back();
        }
    };
    walk(layer, false);
    return found;
}

// Presses a mod's hub button in a fresh hidden hub (its handler may use the hub).
void creatorModAction(std::vector<unsigned> const& path) {
    static Ref<CreatorLayer> layer;
    layer = CreatorLayer::create();
    if (!layer) return;
    CCNode* node = layer;
    for (auto i : path) {
        auto children = node->getChildren();
        if (!children || i >= children->count()) return;
        node = static_cast<CCNode*>(children->objectAtIndex(i));
    }
    if (auto item = typeinfo_cast<CCMenuItem*>(node)) item->activate();
}

class $modify(LazerLevelBrowser, LevelBrowserLayer) {
    static CCScene* scene(GJSearchObject* search) {
        bool enabled = Mod::get()->getSettingValue<bool>("enabled");
        if (g_newLevelFlow && search && search->m_searchType == SearchType::MyLevels && enabled) {
            g_newLevelFlow = false;
            g_returnState = ButtonSystem::State::Create;
            return MenuLayer::scene(false);
        }
        // GD's online lists (featured, the lists, its search...) are song select.
        if (enabled && lazer::browse::Request::wants(search)) {
            return lazer::SongSelect::onlineScene(lazer::browse::Request::fromSearch(search));
        }
        // And so is the safe (the past dailies, weeklies, events).
        if (enabled && search && !search->m_searchIsOverlay) {
            switch (search->m_searchType) {
                case SearchType::DailySafe: return lazer::SongSelect::timelyScene(GJTimedLevelType::Daily);
                case SearchType::WeeklySafe: return lazer::SongSelect::timelyScene(GJTimedLevelType::Weekly);
                case SearchType::EventSafe: return lazer::SongSelect::timelyScene(GJTimedLevelType::Event);
                default: break;
            }
        }
        return LevelBrowserLayer::scene(search);
    }

    // GD builds some of those scenes itself (CreatorLayer's featured and
    // lists buttons, other mods): song select takes over on the next frame.
    bool init(GJSearchObject* search) {
        if (!LevelBrowserLayer::init(search)) return false;
        if (!Mod::get()->getSettingValue<bool>("enabled") || !lazer::browse::Request::wants(search)) return true;
        if (m_isOverlay || typeinfo_cast<LevelListLayer*>(static_cast<LevelBrowserLayer*>(this))) return true;
        Ref<GJSearchObject> keep = search;
        Loader::get()->queueInMainThread([keep] {
            auto scene = lazer::SongSelect::onlineScene(lazer::browse::Request::fromSearch(keep));
            CCDirector::get()->replaceScene(CCTransitionFade::create(0.5f, scene));
        });
        return true;
    }
};

// Screens that go "back" to CreatorLayer come back to the menu instead.
class $modify(LazerCreatorLayer, CreatorLayer) {
    static CCScene* scene() {
        auto mod = Mod::get();
        if (!mod->getSettingValue<bool>("enabled")) return CreatorLayer::scene();
        // Your levels and lists go back to the create buttons, even after the
        // editor or a level page in between.
        if (lazer::LevelListingOverlay::backToCreate()) {
            lazer::LevelListingOverlay::backToCreate() = false;
            lazer::SongSelect::browsingOnline() = false;
            g_returnState = ButtonSystem::State::Create;
            return MenuLayer::scene(false);
        }
        // GD's online screens opened from song select go back there.
        if (lazer::SongSelect::browsingOnline()) {
            lazer::SongSelect::browsingOnline() = false;
            return lazer::SongSelect::scene();
        }
        if (g_returnState == ButtonSystem::State::Initial) g_returnState = ButtonSystem::State::TopLevel;
        log::debug("CreatorLayer::scene -> menu (return {})", static_cast<int>(g_returnState));
        return MenuLayer::scene(false);
    }
};
