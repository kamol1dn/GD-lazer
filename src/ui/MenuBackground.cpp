#include "MenuBackground.hpp"

#include "Triangles.hpp"

using namespace cocos2d;

namespace lazer {

namespace {
    constexpr auto PROGRAM_KEY = "lazer.blur";
    // The scene is captured at 1/4 of the screen resolution: cheap, and the
    // bilinear upscale adds to the blur.
    constexpr float DOWNSAMPLE = 4.f;

    // Standard textured-quad vertex shader. Written out here rather than using
    // cocos' ccPositionTextureColor_vert: that's a data export from libcocos2d.dll
    // which doesn't resolve correctly from a mod (it crashed the shader compiler).
    constexpr auto BLUR_VERT = R"(
attribute vec4 a_position;
attribute vec2 a_texCoord;
attribute vec4 a_color;
#ifdef GL_ES
varying lowp vec4 v_fragmentColor;
varying mediump vec2 v_texCoord;
#else
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
#endif
void main() {
    gl_Position = CC_MVPMatrix * a_position;
    v_fragmentColor = a_color;
    v_texCoord = a_texCoord;
}
)";

    // Two rings of 8 taps at 1.5 and 3.5 texels; with bilinear filtering each
    // tap averages 4 texels, giving a smooth, wide blur in a single pass.
    constexpr auto BLUR_FRAG = R"(
#ifdef GL_ES
precision mediump float;
#endif
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
uniform sampler2D CC_Texture0;
uniform vec2 u_texel;

void main() {
    vec4 sum = texture2D(CC_Texture0, v_texCoord) * 0.12;
    for (int ring = 0; ring < 2; ring++) {
        float r = ring == 0 ? 1.5 : 3.5;
        float w = ring == 0 ? 0.14 : 0.08;
        vec2 d = u_texel * r;
        sum += texture2D(CC_Texture0, v_texCoord + vec2( d.x, 0.0)) * w;
        sum += texture2D(CC_Texture0, v_texCoord + vec2(-d.x, 0.0)) * w;
        sum += texture2D(CC_Texture0, v_texCoord + vec2(0.0,  d.y)) * w;
        sum += texture2D(CC_Texture0, v_texCoord + vec2(0.0, -d.y)) * w;
        sum += texture2D(CC_Texture0, v_texCoord + d * 0.7071) * (w * 0.5);
        sum += texture2D(CC_Texture0, v_texCoord - d * 0.7071) * (w * 0.5);
        sum += texture2D(CC_Texture0, v_texCoord + vec2(d.x, -d.y) * 0.7071) * (w * 0.5);
        sum += texture2D(CC_Texture0, v_texCoord + vec2(-d.x, d.y) * 0.7071) * (w * 0.5);
    }
    gl_FragColor = sum * v_fragmentColor;
}
)";
}

CCGLProgram* MenuBackground::blurProgram() {
    auto cache = CCShaderCache::sharedShaderCache();
    if (auto p = cache->programForKey(PROGRAM_KEY)) return p;

    auto p = new CCGLProgram();
    p->initWithVertexShaderByteArray(BLUR_VERT, BLUR_FRAG);
    p->addAttribute(kCCAttributeNamePosition, kCCVertexAttrib_Position);
    p->addAttribute(kCCAttributeNameColor, kCCVertexAttrib_Color);
    p->addAttribute(kCCAttributeNameTexCoord, kCCVertexAttrib_TexCoords);
    p->link();
    p->updateUniforms();
    cache->addProgram(p, PROGRAM_KEY);
    p->release();
    return p;
}

MenuBackground* MenuBackground::create(CCNode* source, float dim, bool blur, bool triangles) {
    auto ret = new MenuBackground();
    if (ret->init(source, dim, blur, triangles)) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

bool MenuBackground::init(CCNode* source, float dim, bool blur, bool triangles) {
    if (!CCNode::init()) return false;
    auto win = CCDirector::sharedDirector()->getWinSize();
    this->setContentSize(win);
    m_source = source;

    if (blur && source) {
        float pxPerPoint = CCEGLView::sharedOpenGLView()->getScaleX();
        float csf = CC_CONTENT_SCALE_FACTOR();
        // CCRenderTexture sizes are in points and get multiplied by the content scale factor.
        CCSize rtSize = win * (pxPerPoint / DOWNSAMPLE / csf);
        m_rt = CCRenderTexture::create(
            std::max(1, int(rtSize.width)), std::max(1, int(rtSize.height)), kCCTexture2DPixelFormat_RGBA8888
        );
        if (m_rt) {
            m_rt->retain();
            auto tex = m_rt->getSprite()->getTexture();
            tex->setAntiAliasTexParameters();

            m_blurred = CCSprite::createWithTexture(tex);
            m_blurred->setFlipY(true); // render textures are upside down
            m_blurred->setAnchorPoint({0, 0});
            m_blurred->setScaleX(win.width / m_blurred->getContentSize().width);
            m_blurred->setScaleY(win.height / m_blurred->getContentSize().height);
            m_blurred->setShaderProgram(blurProgram());
            this->addChild(m_blurred, 0);

            source->setVisible(false);
        }
    }

    if (dim > 0) {
        auto dimLayer = CCLayerColor::create({0, 0, 0, static_cast<GLubyte>(std::clamp(dim, 0.f, 1.f) * 255)});
        dimLayer->setContentSize(win);
        this->addChild(dimLayer, 1);
    }

    if (triangles) {
        float k = win.height / 768.f;
        auto tri = Triangles::create(win, 100.f * k * 1.6f, 40);
        tri->setAlphaRange(0.01f, 0.05f);
        tri->setVelocity(0.35f);
        this->addChild(tri, 2);
    }
    return true;
}

void MenuBackground::visit() {
    if (!this->isVisible()) return;

    if (m_rt && m_source) {
        // Draw GD's live menu scene into the small texture...
        m_source->setVisible(true);
        m_rt->beginWithClear(0, 0, 0, 1);
        m_source->visit();
        m_rt->end();
        m_source->setVisible(false);

        // ...and tell the blur shader how big one texel is.
        auto program = m_blurred->getShaderProgram();
        auto tex = m_blurred->getTexture();
        program->use();
        glUniform2f(
            glGetUniformLocation(program->getProgram(), "u_texel"),
            1.f / tex->getPixelsWide(), 1.f / tex->getPixelsHigh()
        );
    }

    CCNode::visit();
}

} // namespace lazer
