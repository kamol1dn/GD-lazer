#include <Geode/Geode.hpp>
#include <Geode/modify/MenuLayer.hpp>

#include "settings/SettingsContent.hpp"
#include "ui/ButtonSystem.hpp"
#include "ui/MenuBackground.hpp"
#include "ui/RewardsOverlay.hpp"
#include "ui/SettingsOverlay.hpp"
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
        lazer::MenuBackground* background = nullptr;
        lazer::SettingsOverlay* settings = nullptr;
        lazer::RewardsOverlay* rewards = nullptr;
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

        toolbar->addLeft({lazer::makeIcon(icon::GEAR, 1), "settings", [this] { this->toggleSettings(); }});
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
            if (m_fields->rewards) m_fields->rewards->close();
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
            if (m_fields->settings) m_fields->settings->close();
            rewards->open();
        }
    }

    void keyBackClicked() {
        // Escape closes overlays, then collapses the button bar (like osu!), then GD's quit prompt.
        if (m_fields->settings && m_fields->settings->back()) return;
        if (m_fields->rewards && m_fields->rewards->back()) return;
        if (m_fields->buttons && m_fields->buttons->back()) return;
        MenuLayer::keyBackClicked();
    }
};
