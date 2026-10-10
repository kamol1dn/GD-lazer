#include "RingArc.hpp"

#include <algorithm>
#include <cmath>

using namespace cocos2d;

namespace lazer {

namespace {
    constexpr auto PROGRAM_KEY = "lazer.ring-arc";

    constexpr auto VERT = R"(
attribute vec4 a_position;
varying vec2 v_pos;
void main() {
    gl_Position = CC_MVPMatrix * a_position;
    v_pos = a_position.xy;
}
)";

    // Signed distance to the arc: the ring band (outer radius u_radii.x, inner
    // u_radii.y) cut to the angles [u_arc.x, u_arc.x + u_arc.y] (turns,
    // clockwise from the top), then a 1px anti-aliasing band and a glow that
    // falls off outside it.
    constexpr auto FRAG = R"(
#ifdef GL_ES
precision highp float;
#endif
varying vec2 v_pos;
uniform vec2 u_center;
uniform vec2 u_radii;    // outer, inner
uniform vec2 u_arc;      // start, sweep (turns)
uniform float u_pxPerUnit;
uniform vec4 u_fill;
uniform float u_glow;
uniform vec4 u_glowColor;

void main() {
    vec2 p = v_pos - u_center;
    float r = length(p);
    float d = max(u_radii.y - r, r - u_radii.x);
    if (u_arc.y < 1.0) {
        float turns = atan(p.x, p.y) / 6.28318530718;
        float a = fract(turns - u_arc.x);
        float along = 6.28318530718 * max(r, 0.001);
        float dAng = a <= u_arc.y ? -min(a, u_arc.y - a) * along : min(a - u_arc.y, 1.0 - a) * along;
        d = max(d, dAng);
    }
    float fill = clamp(0.5 - d * u_pxPerUnit, 0.0, 1.0);

    vec4 body = vec4(u_fill.rgb * u_fill.a, u_fill.a) * fill;
    vec4 glow = vec4(0.0);
    if (u_glow > 0.0) {
        float g = 1.0 - smoothstep(0.0, u_glow, max(d, 0.0));
        glow = vec4(u_glowColor.rgb * u_glowColor.a, u_glowColor.a) * g * g * (1.0 - fill);
    }
    gl_FragColor = body + glow;
}
)";

    struct Uniforms {
        GLuint program = 0;
        GLint center, radii, arc, pxPerUnit, fill, glow, glowColor;
    } g_uniforms;

    void fetchUniforms(GLuint id) {
        if (g_uniforms.program == id) return;
        g_uniforms = {
            id,
            glGetUniformLocation(id, "u_center"),
            glGetUniformLocation(id, "u_radii"),
            glGetUniformLocation(id, "u_arc"),
            glGetUniformLocation(id, "u_pxPerUnit"),
            glGetUniformLocation(id, "u_fill"),
            glGetUniformLocation(id, "u_glow"),
            glGetUniformLocation(id, "u_glowColor"),
        };
    }

    void uniformColor(GLint loc, ccColor4B c, float alphaMul, ccColor3B tint) {
        glUniform4f(loc,
            c.r / 255.f * tint.r / 255.f,
            c.g / 255.f * tint.g / 255.f,
            c.b / 255.f * tint.b / 255.f,
            c.a / 255.f * alphaMul);
    }
}

CCGLProgram* RingArc::program() {
    auto cache = CCShaderCache::sharedShaderCache();
    if (auto p = cache->programForKey(PROGRAM_KEY)) return p;

    auto p = new CCGLProgram();
    p->initWithVertexShaderByteArray(VERT, FRAG);
    p->addAttribute(kCCAttributeNamePosition, kCCVertexAttrib_Position);
    p->link();
    p->updateUniforms();

    cache->addProgram(p, PROGRAM_KEY);
    p->release();
    return p;
}

RingArc* RingArc::create(float diameter, float thickness, ccColor4B color) {
    auto ret = new RingArc();
    if (ret->init(diameter, thickness, color)) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

bool RingArc::init(float diameter, float thickness, ccColor4B color) {
    if (!CCNodeRGBA::init()) return false;
    this->setContentSize({diameter, diameter});
    this->setAnchorPoint({0.5f, 0.5f});
    this->setCascadeOpacityEnabled(true);
    this->setCascadeColorEnabled(true);
    m_thickness = thickness;
    m_fill = color;
    this->setShaderProgram(program());
    return true;
}

void RingArc::draw() {
    auto size = this->getContentSize();
    if (size.width <= 0 || size.height <= 0 || m_sweep <= 0) return;

    auto t = this->nodeToWorldTransform();
    float worldScale = std::sqrt(t.a * t.a + t.b * t.b);
    float pxPerUnit = worldScale * CCEGLView::sharedOpenGLView()->getScaleX();

    float pad = m_glowSize + 1.f;
    GLfloat verts[] = {
        -pad, -pad,
        size.width + pad, -pad,
        -pad, size.height + pad,
        size.width + pad, size.height + pad,
    };

    CC_NODE_DRAW_SETUP();
    fetchUniforms(this->getShaderProgram()->getProgram());
    ccGLBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

    float alpha = this->getDisplayedOpacity() / 255.f;
    auto tint = this->getDisplayedColor();
    float outer = std::min(size.width, size.height) * 0.5f;
    float inner = std::max(0.f, outer - m_thickness);

    glUniform2f(g_uniforms.center, size.width / 2, size.height / 2);
    glUniform2f(g_uniforms.radii, outer, inner);
    glUniform2f(g_uniforms.arc, m_start - std::floor(m_start), std::min(m_sweep, 1.f));
    glUniform1f(g_uniforms.pxPerUnit, pxPerUnit);
    uniformColor(g_uniforms.fill, m_fill, alpha, tint);
    glUniform1f(g_uniforms.glow, m_glowSize);
    uniformColor(g_uniforms.glowColor, m_glowColor, alpha, {255, 255, 255});

    ccGLEnableVertexAttribs(kCCVertexAttribFlag_Position);
    glVertexAttribPointer(kCCVertexAttrib_Position, 2, GL_FLOAT, GL_FALSE, 0, verts);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

#ifndef GEODE_IS_MACOS
    // macOS GD does not export cocos2d's diagnostic draw counter.
    CC_INCREMENT_GL_DRAWS(1);
#endif
}

} // namespace lazer
