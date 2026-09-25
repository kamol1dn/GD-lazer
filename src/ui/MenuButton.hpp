#pragma once

#include "Easing.hpp"
#include "RoundedBox.hpp"

#include <Geode/cocos/include/cocos2d.h>
#include <functional>
#include <string>

namespace lazer {

// A sheared main-menu button, after osu.Game/Screens/Menu/MainMenuButton.cs.
// Its width animates (0 -> full -> 1.5x on hover -> 2x on click) and the
// ButtonSystem lays buttons out by their *current* width every frame, which is
// what makes the row unfold out from behind the logo.
class MenuButton : public cocos2d::CCNode, public cocos2d::CCTouchDelegate {
public:
    enum class State { Contracted, Expanded, Exploded };

    struct Style {
        float width;
        float height;
        float wedge; // horizontal shear offset between bottom and top edge
    };

    static MenuButton* create(
        std::string const& label, std::string const& icon, cocos2d::ccColor3B color,
        Style style, std::function<void()> callback
    );

    // `contractStyle` 1 = slower contraction used when leaving the menu.
    void setState(State state, int contractStyle = 0);
    State getState() const { return m_state; }

    // Width the button currently occupies in the flow.
    float flowWidth() const { return m_width.get(); }

    // Activate as if clicked: flash, explode (if it leaves the menu), run the callback.
    void trigger();

    // Whether clicking explodes the button (screen change) or just flashes it (popup).
    void setExplodes(bool v) { m_explodes = v; }

    void update(float dt) override;
    void onEnter() override;
    void onExit() override;

    bool ccTouchBegan(cocos2d::CCTouch* touch, cocos2d::CCEvent*) override;
    void ccTouchEnded(cocos2d::CCTouch* touch, cocos2d::CCEvent*) override;

protected:
    bool init(std::string const& label, std::string const& icon, cocos2d::ccColor3B color,
              Style style, std::function<void()> callback);
    bool containsWorldPoint(cocos2d::CCPoint p);
    bool acceptsInput();
    void setHovered(bool hovered);
    void onBeat(float beatLength);

    Style m_style {};
    cocos2d::ccColor3B m_color {};
    std::function<void()> m_callback;
    State m_state = State::Contracted;
    bool m_explodes = true;
    bool m_hovered = false;
    bool m_pressed = false;
    bool m_rightward = false;
    float m_beatTimer = 0;       // fallback beat when no music is playing
    float m_halfBeatMs = -1;     // countdown to the second half of the bounce
    float m_beatLength = 500;
    int m_lastBeat = 0;

    RoundedBox* m_bg = nullptr;
    cocos2d::CCNode* m_content = nullptr;
    cocos2d::CCNode* m_iconHolder = nullptr;
    cocos2d::CCLabelBMFont* m_icon = nullptr;
    cocos2d::CCLabelBMFont* m_label = nullptr;

    Tweened<float> m_width {0.f};
    Tweened<float> m_alpha {0.f};
    Tweened<float> m_hoverFlash {0.f};
    Tweened<float> m_iconScaleX {1.f};
    Tweened<float> m_iconScaleY {1.f};
    Tweened<float> m_iconRotation {0.f};
    Tweened<float> m_iconY {0.f};
};

} // namespace lazer
