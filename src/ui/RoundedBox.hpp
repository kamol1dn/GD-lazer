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

    void setRadius(float r) { m_radius = r; m_corners = {-1, -1, -1, -1}; }
    // Individual corners (negative = use the shared radius).
    void setCornerRadii(float topLeft, float topRight, float bottomLeft, float bottomRight) {
        m_corners = {topLeft, topRight, bottomLeft, bottomRight};
    }
    float getRadius() const { return m_radius; }

    void setFillColor(cocos2d::ccColor4B c) { m_fill = c; }
    void setBorder(float width, cocos2d::ccColor4B color) { m_borderWidth = width; m_borderColor = color; }
    // Shadow extends `size` units outside the box, fading from `color`.
    void setShadow(float size, cocos2d::ccColor4B color) { m_shadowSize = size; m_shadowColor = color; }
    // Fill with a texture instead (cover-fit, cropped to the box, tinted by the
    // fill colour). nullptr goes back to a plain fill.
    void setTexture(cocos2d::CCTexture2D* texture);
    // Slides the texture sideways by `shift` box widths; what leaves the box is clipped.
    void setTextureShift(float shift) { m_textureShift = shift; }

    ~RoundedBox() override { CC_SAFE_RELEASE(m_texture); }

    void draw() override;

protected:
    bool init(cocos2d::CCSize size, float radius, cocos2d::ccColor4B color);

    static cocos2d::CCGLProgram* program();

    float m_radius = 0;
    struct { float tl, tr, bl, br; } m_corners {-1, -1, -1, -1};
    cocos2d::ccColor4B m_fill {255, 255, 255, 255};
    float m_borderWidth = 0;
    cocos2d::ccColor4B m_borderColor {255, 255, 255, 255};
    float m_shadowSize = 0;
    cocos2d::ccColor4B m_shadowColor {0, 0, 0, 100};
    cocos2d::CCTexture2D* m_texture = nullptr;
    float m_textureShift = 0;
};

} // namespace lazer
