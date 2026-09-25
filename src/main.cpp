#include <Geode/Geode.hpp>
#include <Geode/modify/MenuLayer.hpp>

#include "ui/ButtonSystem.hpp"
#include "ui/MenuBackground.hpp"
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
        {"newgrounds-button", {icon::MUSIC, "newgrounds"}},
        {"daily-chest-button", {icon::GIFT, "daily chests"}},
        {"geode.loader/geode-button", {icon::PUZZLE, "mods"}},
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
                {"settings", icon::GEAR, {85, 85, 85}, [this] { this->onOptions(nullptr); }, false},
            },
            {
                {"play", icon::PLAY, {102, 68, 204}, leave([this] { this->onPlay(nullptr); })},
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

        toolbar->addLeft({lazer::makeIcon(icon::GEAR, 1), "settings", [this] { this->onOptions(nullptr); }});
        toolbar->addLeft({lazer::makeIcon(icon::HOUSE, 1), "home", [buttons] { buttons->back(); }});

        buttons->setStateCallback([toolbar](ButtonSystem::State state) {
            // osu! shows the toolbar once the logo lands in the button bar.
            if (state == ButtonSystem::State::Initial) toolbar->hide();
            else toolbar->show();
        });

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
                toolbar->addRight({iconNode, tooltip, [target] { target->activate(); }});
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

    void keyBackClicked() {
        // Escape collapses the button bar first, like osu!; only then the quit prompt.
        if (m_fields->buttons && m_fields->buttons->back()) return;
        MenuLayer::keyBackClicked();
    }
};
