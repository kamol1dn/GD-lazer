// GD's pause menu as osu!'s pause overlay (PauseMenu), and its "exit
// level?" check (GD's Confirm Exit option) as one of osu!'s dialogs instead
// of GD's alert.

#include "../core/Text.hpp"
#include "Dialog.hpp"
#include "PauseMenu.hpp"

#include <Geode/Geode.hpp>
#include <Geode/modify/PauseLayer.hpp>

using namespace geode::prelude;

namespace {
    // GD's Confirm Exit option: PauseLayer::tryQuit asks first when it's on.
    constexpr auto CONFIRM_EXIT = "0167";
}

class $modify(LazerPauseLayer, PauseLayer) {
    struct Fields {
        lazer::PauseMenu* menu = nullptr;
    };

    static void onModify(auto& self) {
        // Straight after GD's own setup, before other mods add theirs: only
        // GD's nodes get hidden, and mods' buttons are gathered a frame later.
        (void)self.setHookPriorityPost("PauseLayer::customSetup", Priority::VeryEarlyPost);
        // Hide Pause Menu interprets our hidden vanilla background as a
        // hidden pause screen and consumes tryQuit to reveal it instead.
        (void)self.setHookPriorityPre("PauseLayer::tryQuit", Priority::VeryEarlyPre);
    }

    void customSetup() {
        PauseLayer::customSetup();
        if (!Mod::get()->getSettingValue<bool>("restyle-gameplay")) return;
        auto menu = lazer::PauseMenu::create(this);
        if (!menu) return;
        this->addChild(menu, 100);
        m_fields->menu = menu;
    }

    void tryQuit(CCObject* sender) {
        if (!m_fields->menu || !Mod::get()->getSettingValue<bool>("enabled")
            || !Mod::get()->getSettingValue<bool>("restyle-gameplay")) {
            return PauseLayer::tryQuit(sender);
        }
        if (lazer::Dialog::isOpen()) return;
        if (!GameManager::get()->getGameVariable(CONFIRM_EXIT)) {
            this->onQuit(sender);
            return;
        }
        std::string level;
        if (auto play = PlayLayer::get(); play && play->m_level) level = play->m_level->m_levelName;
        Ref<PauseLayer> self = this;
        lazer::Dialog::show(lazer::icon::TRIANGLE_EXCLAMATION, "Are you sure you want to exit the level?", level, {
            {"Let me out!", lazer::Dialog::Kind::Ok, [self] { self->onQuit(nullptr); }},
            {"Just a little more...", lazer::Dialog::Kind::Cancel, nullptr},
        });
    }

    // The dialog takes Escape (cancel) and keys: the pause menu underneath
    // would resume.
    void keyBackClicked() {
        if (lazer::Dialog::isOpen()) return;
        PauseLayer::keyBackClicked();
    }

    void keyDown(enumKeyCodes key, double timestamp) {
        if (lazer::Dialog::isOpen()) return;
        // Up / down / enter pick and press osu!'s buttons; the rest (Escape,
        // space, other mods' keybinds) stay GD's. Pressing one may close the
        // menu: nothing after it.
        if (auto menu = m_fields->menu; menu && menu->handleKey(key)) return;
        PauseLayer::keyDown(key, timestamp);
    }
};
