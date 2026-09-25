#pragma once

#include "Easing.hpp"

#include <Geode/cocos/include/cocos2d.h>

namespace lazer {

// Soft blue glows at the screen edges that flash with the music
// (osu.Game/Screens/Menu/MenuSideFlashes.cs, in its kiai style: sides
// alternate on every beat, brightness follows the loudness).
class SideFlashes : public cocos2d::CCNode {
public:
    static SideFlashes* create();
    void update(float dt) override;

protected:
    bool init() override;
    void flash(int side, float amplitude, float beatLength);

    cocos2d::CCLayerGradient* m_boxes[2] {};
    Tweened<float> m_alpha[2] {Tweened<float> {0.f}, Tweened<float> {0.f}};
    float m_fadeOutMs[2] {-1, -1}; // countdown to the fade-out phase
    float m_beatLength[2] {500, 500};
    int m_lastBeat = 0;
};

} // namespace lazer
