#pragma once

#include <Geode/cocos/include/cocos2d.h>

namespace lazer {

// An arc of a ring, anti-aliased, with an optional soft glow around it: osu!'s
// CircularProgress (the volume meters' rings), drawn with a signed-distance
// shader like RoundedBox. The node is a square of `diameter`; the ring's outer
// edge sits on the square's inscribed circle. Angles are in turns (0..1):
// `start` is measured clockwise from the top, `sweep` is how far the arc goes
// on clockwise from there.
class RingArc : public cocos2d::CCNodeRGBA {
public:
    static RingArc* create(float diameter, float thickness, cocos2d::ccColor4B color);

    void setArc(float start, float sweep) { m_start = start; m_sweep = sweep; }
    float getSweep() const { return m_sweep; }
    void setThickness(float t) { m_thickness = t; }
    void setFillColor(cocos2d::ccColor4B c) { m_fill = c; }
    // A glow `size` units wide outside and inside the ring, fading from `color`.
    void setGlow(float size, cocos2d::ccColor4B color) { m_glowSize = size; m_glowColor = color; }

    void draw() override;

protected:
    bool init(float diameter, float thickness, cocos2d::ccColor4B color);
    static cocos2d::CCGLProgram* program();

    float m_thickness = 2;
    float m_start = 0;
    float m_sweep = 1;
    cocos2d::ccColor4B m_fill {255, 255, 255, 255};
    float m_glowSize = 0;
    cocos2d::ccColor4B m_glowColor {255, 255, 255, 0};
};

} // namespace lazer
