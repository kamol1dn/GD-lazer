#include "RoundedBox.hpp"

using namespace cocos2d;

namespace lazer {

namespace {
    constexpr auto PROGRAM_KEY = "lazer.rounded-box";

    // cocos prepends CC_MVPMatrix etc. and precision defines for desktop GL.
    constexpr auto VERT = R"(
attribute vec4 a_position;
varying vec2 v_pos;
void main() {
    gl_Position = CC_MVPMatrix * a_position;
    v_pos = a_position.xy;
}
)";

    constexpr auto FRAG = R"(
#ifdef GL_ES
precision highp float;
#endif
varying vec2 v_pos;
uniform vec2 u_size;
uniform float u_radius;
uniform float u_pxPerUnit;
uniform vec4 u_fill;
uniform float u_border;
uniform vec4 u_borderColor;
uniform float u_shadow;
uniform vec4 u_shadowColor;

float sdRoundBox(vec2 p, vec2 halfSize, float r) {
    vec2 q = abs(p) - halfSize + r;
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
}

void main() {
    vec2 halfSize = u_size * 0.5;
    float d = sdRoundBox(v_pos - halfSize, halfSize, u_radius);

    // 1px-wide anti-aliasing band, in screen pixels.
    float fill = clamp(0.5 - d * u_pxPerUnit, 0.0, 1.0);
    float inner = clamp(0.5 - (d + u_border) * u_pxPerUnit, 0.0, 1.0);

    // Premultiplied colours.
    vec4 body = mix(u_borderColor, u_fill, u_border > 0.0 ? inner : 1.0);
    body.rgb *= body.a;
    body *= fill;

    vec4 shadow = vec4(0.0);
    if (u_shadow > 0.0) {
        float s = 1.0 - smoothstep(0.0, u_shadow, max(d, 0.0));
        shadow = vec4(u_shadowColor.rgb * u_shadowColor.a, u_shadowColor.a) * s * s * (1.0 - fill);
    }

    gl_FragColor = body + shadow;
}
)";

    struct Uniforms {
        GLuint program = 0;
        GLint size, radius, pxPerUnit, fill, border, borderColor, shadow, shadowColor;
    } g_uniforms;

    // Looked up lazily so a re-linked program (e.g. after a GL context reset) is picked up.
    void fetchUniforms(GLuint id) {
        if (g_uniforms.program == id) return;
        g_uniforms = {
            id,
            glGetUniformLocation(id, "u_size"),
            glGetUniformLocation(id, "u_radius"),
            glGetUniformLocation(id, "u_pxPerUnit"),
            glGetUniformLocation(id, "u_fill"),
            glGetUniformLocation(id, "u_border"),
            glGetUniformLocation(id, "u_borderColor"),
            glGetUniformLocation(id, "u_shadow"),
            glGetUniformLocation(id, "u_shadowColor"),
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

CCGLProgram* RoundedBox::program() {
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

RoundedBox* RoundedBox::create(CCSize size, float radius, ccColor4B color) {
    auto ret = new RoundedBox();
    if (ret->init(size, radius, color)) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

bool RoundedBox::init(CCSize size, float radius, ccColor4B color) {
    if (!CCNodeRGBA::init()) return false;
    this->setContentSize(size);
    this->setAnchorPoint({0.5f, 0.5f});
    this->setCascadeOpacityEnabled(true);
    this->setCascadeColorEnabled(true);
    m_radius = radius;
    m_fill = color;
    this->setShaderProgram(program());
    return true;
}

void RoundedBox::draw() {
    auto size = this->getContentSize();
    if (size.width <= 0 || size.height <= 0) return;

    // Screen pixels per node unit, so the AA band stays 1px wide at any scale.
    auto t = this->nodeToWorldTransform();
    float worldScale = std::sqrt(t.a * t.a + t.b * t.b);
    float pxPerUnit = worldScale * CCEGLView::sharedOpenGLView()->getScaleX();

    float pad = m_shadowSize + 1.f;
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
    float radius = std::min(m_radius, std::min(size.width, size.height) * 0.5f);

    glUniform2f(g_uniforms.size, size.width, size.height);
    glUniform1f(g_uniforms.radius, radius);
    glUniform1f(g_uniforms.pxPerUnit, pxPerUnit);
    uniformColor(g_uniforms.fill, m_fill, alpha, tint);
    glUniform1f(g_uniforms.border, m_borderWidth);
    uniformColor(g_uniforms.borderColor, m_borderColor, alpha, tint);
    glUniform1f(g_uniforms.shadow, m_shadowSize);
    uniformColor(g_uniforms.shadowColor, m_shadowColor, alpha, {255, 255, 255});

    ccGLEnableVertexAttribs(kCCVertexAttribFlag_Position);
    glVertexAttribPointer(kCCVertexAttrib_Position, 2, GL_FLOAT, GL_FALSE, 0, verts);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    CC_INCREMENT_GL_DRAWS(1);
}

} // namespace lazer
