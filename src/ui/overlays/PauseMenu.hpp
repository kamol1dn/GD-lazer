#pragma once

#include "../core/Easing.hpp"

#include <Geode/cocos/include/cocos2d.h>
#include <Geode/utils/cocos.hpp>
#include <vector>

class PauseLayer;
class Slider;

namespace lazer {

class AnimatedButtonItem;
class SliderRow;

// osu!'s pause overlay (Screens/Play/PauseOverlay) drawn inside GD's
// PauseLayer, laid out the way GD's own pause menu is: a dimmed screen, a big
// "paused" title with the level and its progress bars under it, GD's round
// practice / play / retry buttons in the middle (the play button biggest), the
// retry count and song progress under those, GD's music and effects volume
// sliders, and a footer of pills: quit on its own at one end, then GD's
// extras (restart from the start, edit, level options) and other mods' pause
// buttons. GD's own nodes stay alive but hidden; every button runs GD's
// handler and the sliders drive GD's.
class PauseMenu : public cocos2d::CCLayerRGBA {
public:
    // Built at the end of GD's PauseLayer::customSetup.
    static PauseMenu* create(PauseLayer* layer);

    // osu!'s keyboard selection: the arrows pick a middle button, enter
    // presses it. True when the key was used.
    bool handleKey(cocos2d::enumKeyCodes key);

    void update(float dt) override;

    // Touches on the sliders (everything else is the menu's).
    void registerWithTouchDispatcher() override;
    bool ccTouchBegan(cocos2d::CCTouch* touch, cocos2d::CCEvent* event) override;
    void ccTouchMoved(cocos2d::CCTouch* touch, cocos2d::CCEvent* event) override;
    void ccTouchEnded(cocos2d::CCTouch* touch, cocos2d::CCEvent* event) override;
    void ccTouchCancelled(cocos2d::CCTouch* touch, cocos2d::CCEvent* event) override;

protected:
    bool init(PauseLayer* layer);
    void build();
    void layout();
    void takeModButtons();
    void select(int index);
    SliderRow* sliderAt(cocos2d::CCPoint world) const;

    PauseLayer* m_layer = nullptr;
    std::vector<geode::Ref<cocos2d::CCNode>> m_vanillaNodes;
    float m_k = 1;
    cocos2d::CCNode* m_titleBlock = nullptr;
    cocos2d::CCNode* m_barsBlock = nullptr;   // normal / practice progress (classic levels)
    cocos2d::CCNode* m_infoBlock = nullptr;
    cocos2d::CCNode* m_slidersBlock = nullptr;
    std::vector<SliderRow*> m_sliders;
    SliderRow* m_draggingSlider = nullptr;
    cocos2d::CCMenu* m_menu = nullptr;
    // The middle row: practice (or normal mode), continue, retry.
    std::vector<AnimatedButtonItem*> m_middle;
    std::vector<cocos2d::CCNode*> m_captions;
    int m_continueIndex = 0;
    AnimatedButtonItem* m_quit = nullptr;
    std::vector<AnimatedButtonItem*> m_extras;
    std::vector<AnimatedButtonItem*> m_mods;
    std::vector<AnimatedButtonItem*> m_footer; // quit, extras and mods, for hover
    Slider* m_musicSlider = nullptr;
    Slider* m_sfxSlider = nullptr;
    int m_selected = -1;
    bool m_scanned = false;
    cocos2d::CCPoint m_lastMouse {-1, -1};
    Tweened<float> m_alpha {0.f};
};

} // namespace lazer
