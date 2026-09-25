#pragma once

#include "../core/Easing.hpp"
#include "../core/RoundedBox.hpp"

#include <Geode/cocos/include/cocos2d.h>
#include <functional>
#include <string>
#include <vector>

namespace lazer {

// A square toolbar button with hover highlight and a tooltip that drops
// below the bar (osu.Game/Overlays/Toolbar/ToolbarButton.cs).
class ToolbarButton : public cocos2d::CCNode, public cocos2d::CCTouchDelegate {
public:
    // `icon` is either an icon-font glyph node or any sprite; it gets scaled to fit.
    static ToolbarButton* create(cocos2d::CCNode* icon, std::string const& tooltip, float height,
                                 std::function<void()> action);

    void update(float dt) override;
    void onEnter() override;
    void onExit() override;
    bool ccTouchBegan(cocos2d::CCTouch* touch, cocos2d::CCEvent*) override;
    void ccTouchEnded(cocos2d::CCTouch* touch, cocos2d::CCEvent*) override;

    void setEnabled(bool e) { m_enabled = e; }

protected:
    bool init(cocos2d::CCNode* icon, std::string const& tooltip, float height, std::function<void()> action);
    bool containsWorldPoint(cocos2d::CCPoint p);
    bool interactive();

    std::function<void()> m_action;
    RoundedBox* m_hoverBg = nullptr;
    cocos2d::CCNode* m_tooltip = nullptr;
    Tweened<float> m_hoverAlpha {0.f};
    Tweened<float> m_flash {0.f};
    bool m_hovered = false;
    bool m_pressed = false;
    bool m_enabled = true;
};

// osu!'s top toolbar (osu.Game/Overlays/Toolbar/Toolbar.cs), shown while the
// button system is open. Gathers GD's side/bottom menu buttons, including
// buttons other mods added there, so nothing is lost when they're hidden.
class Toolbar : public cocos2d::CCNode {
public:
    struct Item {
        cocos2d::CCNode* icon;
        std::string tooltip;
        std::function<void()> action;
    };

    static Toolbar* create();

    void addLeft(Item item);
    void addRight(Item item);
    // User section at the far right: name + avatar circle (`avatar` is drawn
    // inside it, e.g. the player's icon; nullptr shows a generic user glyph).
    void setUser(std::string const& name, cocos2d::CCNode* avatar, std::function<void()> action);

    void show();
    void hide();
    float height() const { return m_height; }

    void update(float dt) override;

protected:
    bool init() override;
    void layout();

    float m_height = 0;
    cocos2d::CCNode* m_content = nullptr;
    std::vector<ToolbarButton*> m_left;
    std::vector<ToolbarButton*> m_right;
    cocos2d::CCNode* m_user = nullptr;
    cocos2d::CCLabelBMFont* m_clock = nullptr;
    cocos2d::CCLabelBMFont* m_running = nullptr;
    float m_runningSeconds = 0;
    float m_rightEdge = 0;

    Tweened<float> m_offset {1.f}; // 0 = shown, 1 = fully above the screen
    Tweened<float> m_alpha {0.f};
};

// Snapshot any node (e.g. a mod's menu button sprite) into a plain sprite.
cocos2d::CCSprite* snapshotNode(cocos2d::CCNode* node);

} // namespace lazer
