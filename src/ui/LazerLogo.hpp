#pragma once

#include "Easing.hpp"
#include "LogoVisualisation.hpp"
#include "RoundedBox.hpp"

#include <Geode/cocos/include/cocos2d.h>
#include <functional>

namespace lazer {

// The pulsing main-menu logo, after osu.Game/Screens/Menu/OsuLogo.cs.
// Nested containers each own one kind of motion so they compose cleanly:
//   bounce (press / release) -> beat -> amplitude (music) -> hover -> visuals
class LazerLogo : public cocos2d::CCNode, public cocos2d::CCTouchDelegate {
public:
    static LazerLogo* create(float radius);

    void setCallback(std::function<void()> cb) { m_callback = std::move(cb); }

    // Expanding ring flash, played when the logo is clicked or lands in the button bar.
    void playImpact();
    void onBeat(float amplitude, float beatLength);

    void update(float dt) override;
    void onEnter() override;
    void onExit() override;

    bool ccTouchBegan(cocos2d::CCTouch* touch, cocos2d::CCEvent*) override;
    void ccTouchEnded(cocos2d::CCTouch* touch, cocos2d::CCEvent*) override;
    void ccTouchCancelled(cocos2d::CCTouch* touch, cocos2d::CCEvent*) override;

protected:
    bool init(float radius);
    bool containsWorldPoint(cocos2d::CCPoint p);

    float m_radius = 0;
    std::function<void()> m_callback;

    cocos2d::CCNode* m_bounce = nullptr;
    cocos2d::CCNode* m_beat = nullptr;
    cocos2d::CCNode* m_hover = nullptr;
    cocos2d::CCNode* m_amplitudeNode = nullptr;
    RoundedBox* m_disc = nullptr;
    RoundedBox* m_impact = nullptr;
    RoundedBox* m_ripple = nullptr;
    LogoVisualisation* m_visualiser = nullptr;

    Tweened<float> m_bounceScale {1.f};
    Tweened<float> m_hoverScale {1.f};
    Tweened<float> m_impactScale {1.f};
    Tweened<float> m_impactAlpha {0.f};
    Tweened<float> m_beatScale {1.f};
    Tweened<float> m_rippleScale {1.f};
    Tweened<float> m_rippleAlpha {0.f};
    float m_beatSecondPhaseMs = -1; // countdown to the slow return of the beat squash
    float m_beatLength = 500;
    float m_amplitudeScale = 1.f;
    int m_lastBeat = 0;

    bool m_hovered = false;
    bool m_pressed = false;
};

} // namespace lazer
