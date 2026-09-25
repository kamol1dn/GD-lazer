#pragma once

#include <Geode/cocos/include/cocos2d.h>

namespace lazer {

// A rectangle with anti-aliased rounded corners, an optional border and a soft
// drop shadow, drawn in one pass with a signed-distance-field shader.
// Cocos' stencil clipping can't do smooth corners; this is the building block
// for panels, buttons and circles (radius = min(w, h) / 2).
class RoundedBox : public cocos2d::CCNodeRGBA {
public:
    static RoundedBox* create(cocos2d::CCSize size, float radius, cocos2d::ccColor4B color);

    void setRadius(float r) { m_radius = r; }
    float getRadius() const { return m_radius; }

    void setFillColor(cocos2d::ccColor4B c) { m_fill = c; }
    void setBorder(float width, cocos2d::ccColor4B color) { m_borderWidth = width; m_borderColor = color; }
    // Shadow extends `size` units outside the box, fading from `color`.
    void setShadow(float size, cocos2d::ccColor4B color) { m_shadowSize = size; m_shadowColor = color; }

    void draw() override;

protected:
    bool init(cocos2d::CCSize size, float radius, cocos2d::ccColor4B color);

    static cocos2d::CCGLProgram* program();

    float m_radius = 0;
    cocos2d::ccColor4B m_fill {255, 255, 255, 255};
    float m_borderWidth = 0;
    cocos2d::ccColor4B m_borderColor {255, 255, 255, 255};
    float m_shadowSize = 0;
    cocos2d::ccColor4B m_shadowColor {0, 0, 0, 100};
};

} // namespace lazer
