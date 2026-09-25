#pragma once

#include "../../audio/Sfx.hpp"
#include "../core/Easing.hpp"
#include "LazerLogo.hpp"
#include "MenuButton.hpp"
#include "../core/RoundedBox.hpp"

#include <Geode/cocos/include/cocos2d.h>
#include <functional>
#include <vector>

namespace lazer {

// Logo + button bar state machine, after osu.Game/Screens/Menu/ButtonSystem.cs.
//   Initial:      big logo centred, no buttons
//   TopLevel:     logo shrinks and slides left, bar and buttons unfold
//   Play / Create / Browse: a submenu; the top-level buttons explode away and
//                 the submenu's unfold from the logo, with "back" on the left
//   EnteringMode: a button was chosen and the game is leaving the menu
// The order matters: like osu!, a button whose state is behind the current
// one explodes, one whose state is ahead stays folded.
class ButtonSystem : public cocos2d::CCNode {
public:
    enum class State { Initial, TopLevel, Play, Create, Browse, EnteringMode };

    struct ButtonDef {
        std::string label;
        std::string icon;
        cocos2d::ccColor3B color;
        std::function<void()> action;
        bool leavesMenu = true; // explode + enter mode, vs. open a popup / submenu
        char const* sound = sfx::sound::MENU_DEFAULT_SELECT;
        State visibleIn = State::TopLevel;
        bool left = false;      // left of the logo (settings), instead of right
    };

    // Buttons flow outwards from the logo in the order given.
    static ButtonSystem* create(std::vector<ButtonDef> buttons);

    void setState(State state);
    State getState() const { return m_state; }

    // Called on every state change (e.g. to show / hide the toolbar).
    void setStateCallback(std::function<void(State)> cb) { m_stateCallback = std::move(cb); }

    // Coming back to the menu: logo already parked, straight into `state`.
    void resume(State state);

    // Quitting the game (osu!'s ButtonSystemState.Exit + IntroScreen outro):
    // buttons fold away, the logo returns to the centre and slowly turns.
    void playExit(float durationMs);

    float logoRadius() const { return m_logoRadius; }

    // Escape / back: submenu -> top level -> logo. Returns true if handled.
    bool back();

    void update(float dt) override;
    // Screens pushed over the menu pop back to this same node, still leaving:
    // unfold the menu we left from.
    void onEnter() override;

protected:
    struct Entry {
        MenuButton* button;
        State min;
        State max;
    };

    bool init(std::vector<ButtonDef> buttons);
    MenuButton* makeButton(ButtonDef const& def);
    void onLogoClicked();
    void layoutButtons();
    void updateButtons(State state);
    void schedule(float delayMs, std::function<void()> fn, bool isLogoAction = false);
    static bool isMenu(State s) { return s != State::Initial && s != State::EnteringMode; }

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
    std::vector<Entry> m_left;  // nearest the logo first
    std::vector<Entry> m_right;

    Tweened<cocos2d::CCPoint> m_logoPos;
    Tweened<float> m_logoScale {1.f};
    Tweened<float> m_logoRotation {0.f};
    bool m_exiting = false;
    State m_leftFrom = State::TopLevel; // menu a leaving button was pressed in
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
