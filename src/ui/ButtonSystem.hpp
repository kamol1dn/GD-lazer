#pragma once

#include "../audio/Sfx.hpp"
#include "Easing.hpp"
#include "LazerLogo.hpp"
#include "MenuButton.hpp"
#include "RoundedBox.hpp"

#include <Geode/cocos/include/cocos2d.h>
#include <functional>
#include <vector>

namespace lazer {

// Logo + button bar state machine, after osu.Game/Screens/Menu/ButtonSystem.cs.
//   Initial:     big logo centred, no buttons
//   TopLevel:    logo shrinks and slides left, bar and buttons unfold
//   EnteringMode: a button was chosen and the game is leaving the menu
class ButtonSystem : public cocos2d::CCNode {
public:
    enum class State { Initial, TopLevel, EnteringMode };

    struct ButtonDef {
        std::string label;
        std::string icon;
        cocos2d::ccColor3B color;
        std::function<void()> action;
        bool leavesMenu = true; // explode + enter mode, vs. open a popup over the menu
        char const* sound = sfx::sound::MENU_DEFAULT_SELECT;
    };

    // `left` buttons sit left of the logo (nearest first), `right` to its right.
    static ButtonSystem* create(std::vector<ButtonDef> left, std::vector<ButtonDef> right);

    void setState(State state);
    State getState() const { return m_state; }

    // Called on every state change (e.g. to show / hide the toolbar).
    void setStateCallback(std::function<void(State)> cb) { m_stateCallback = std::move(cb); }

    // Enter TopLevel without the logo animation (returning to the menu).
    void resumeTopLevel();

    // Escape / back. Returns true if handled (i.e. we collapsed back to Initial).
    bool back();

    void update(float dt) override;

protected:
    bool init(std::vector<ButtonDef> left, std::vector<ButtonDef> right);
    MenuButton* makeButton(ButtonDef const& def);
    void onLogoClicked();
    void layoutButtons();
    void schedule(float delayMs, std::function<void()> fn, bool isLogoAction = false);

    State m_state = State::Initial;
    std::function<void(State)> m_stateCallback;

    // Sizes in GD units, scaled from osu's 768px-tall layout.
    float m_buttonWidth = 0;
    float m_barHeight = 0;
    float m_wedge = 0;
    float m_logoRadius = 0;
    cocos2d::CCPoint m_center;
    cocos2d::CCPoint m_logoTarget;

    LazerLogo* m_logo = nullptr;
    cocos2d::CCNode* m_barHolder = nullptr;
    RoundedBox* m_bar = nullptr;
    std::vector<MenuButton*> m_left;
    std::vector<MenuButton*> m_right;

    Tweened<cocos2d::CCPoint> m_logoPos;
    Tweened<float> m_logoScale {1.f};
    Tweened<float> m_barAlpha {0.f};
    Tweened<float> m_barScaleX {2.f};
    Tweened<float> m_barScaleY {0.f};

    struct Pending {
        float remainingMs;
        std::function<void()> fn;
        bool isLogoAction;
    };
    std::vector<Pending> m_pending;
};

} // namespace lazer
