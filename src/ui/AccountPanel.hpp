#pragma once

#include "Easing.hpp"
#include "RoundedBox.hpp"

#include <Geode/cocos/include/cocos2d.h>
#include <functional>
#include <string>
#include <vector>

namespace lazer {

// The card that drops from the toolbar's user button, after osu!'s
// LoginOverlay / user panel: your icon (and player 2's, with 2P Skins), name,
// Better Progression level, a few stats, and account actions.
class AccountPanel : public cocos2d::CCNode, public cocos2d::CCTouchDelegate {
public:
    struct Actions {
        std::function<void()> viewProfile;
        std::function<void()> iconKit;
    };

    static AccountPanel* create(float toolbarHeight, Actions actions);

    void open();
    void close();
    void toggle() { m_open ? close() : open(); }
    bool isOpen() const { return m_open; }
    bool back();

    void update(float dt) override;
    void onEnter() override;
    void onExit() override;
    bool ccTouchBegan(cocos2d::CCTouch* touch, cocos2d::CCEvent*) override;
    void ccTouchEnded(cocos2d::CCTouch* touch, cocos2d::CCEvent*) override;
    void ccTouchCancelled(cocos2d::CCTouch* touch, cocos2d::CCEvent* e) override { ccTouchEnded(touch, e); }

protected:
    struct Item {
        cocos2d::CCNode* node;
        RoundedBox* hover;
        std::function<void()> action;
        bool danger = false;
        bool hovered = false;
        Tweened<float> highlight {0.f};
    };

    bool init(float toolbarHeight, Actions actions);
    void rebuild();
    float buildHeader(float width, float top);
    float buildStats(float width, float top);
    float buildItems(float width, float top);
    void addItem(char const* glyph, std::string const& label, std::function<void()> action, bool danger = false);
    Item* itemAt(cocos2d::CCPoint world);

    Actions m_actions;
    float m_k = 1;
    float m_toolbarHeight = 0;
    bool m_open = false;
    bool m_builtLoggedIn = false;

    cocos2d::CCNodeRGBA* m_panel = nullptr;
    cocos2d::CCLabelBMFont* m_status = nullptr;
    std::vector<Item> m_items;
    Item* m_pressed = nullptr;
    float m_itemsTop = 0;

    Tweened<float> m_alpha {0.f};
    Tweened<float> m_scale {0.9f};
};

} // namespace lazer
