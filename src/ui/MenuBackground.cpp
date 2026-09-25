#include "MenuBackground.hpp"

#include "Triangles.hpp"

#include <Geode/utils/cocos.hpp>
#include <algorithm>
#include <cmath>

using namespace cocos2d;

namespace lazer {

namespace {
    constexpr auto PROGRAM_KEY = "lazer.blur";
    // The scene is captured at 1/4 of the screen resolution: cheap, and the
    // bilinear upscale adds to the blur.
    constexpr float DOWNSAMPLE = 4.f;

    // ParallaxContainer: DEFAULT_PARALLAX_AMOUNT and its easing times.
    constexpr float PARALLAX_AMOUNT = 0.02f;
    constexpr float PARALLAX_DURATION = 100.f;
    constexpr float PARALLAX_SCALE_DURATION = 1000.f;
    // BackgroundScreenDefault.displayNext: the old background fades out over the new one.
    constexpr float CROSSFADE_MS = 800.f;

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

    // One direction of a separable Gaussian blur (sigma = 4 texels of the
    // quarter-resolution capture); run horizontally, then vertically.
    constexpr auto BLUR_FRAG = R"(
#ifdef GL_ES
precision mediump float;
#endif
varying vec4 v_fragmentColor;
varying vec2 v_texCoord;
uniform sampler2D CC_Texture0;
uniform vec2 u_step; // one texel along the blur direction

const float SIGMA = 4.0;

void main() {
    vec4 sum = vec4(0.0);
    float total = 0.0;
    for (int i = -10; i <= 10; i++) {
        float x = float(i);
        float w = exp(-x * x / (2.0 * SIGMA * SIGMA));
        sum += texture2D(CC_Texture0, v_texCoord + u_step * x) * w;
        total += w;
    }
    gl_FragColor = sum / total * v_fragmentColor;
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
        m_rt2 = CCRenderTexture::create(
            std::max(1, int(rtSize.width)), std::max(1, int(rtSize.height)), kCCTexture2DPixelFormat_RGBA8888
        );
        if (m_rt && m_rt2) {
            m_rt->retain();
            m_rt2->retain();
            auto tex = m_rt->getSprite()->getTexture();
            tex->setAntiAliasTexParameters();
            m_rt2->getSprite()->getTexture()->setAntiAliasTexParameters();

            // Horizontal pass: capture -> second texture (drawn by hand in visit()).
            m_horizontal = CCSprite::createWithTexture(tex);
            m_horizontal->retain();
            m_horizontal->setFlipY(true); // render textures are upside down
            m_horizontal->setAnchorPoint({0, 0});
            m_horizontal->setShaderProgram(blurProgram());

            // Vertical pass: second texture -> screen.
            m_blurred = CCSprite::createWithTexture(m_rt2->getSprite()->getTexture());
            m_blurred->setFlipY(true); // render textures are upside down
            m_blurred->setPosition(win / 2);
            m_blurred->setScaleX(win.width / m_blurred->getContentSize().width);
            m_blurred->setScaleY(win.height / m_blurred->getContentSize().height);
            m_blurred->setShaderProgram(blurProgram());
            this->addChild(m_blurred, 0);

            source->setVisible(false);
        } else {
            CC_SAFE_RELEASE_NULL(m_rt);
            CC_SAFE_RELEASE_NULL(m_rt2);
        }
    }

    // Level images. Without blur they're drawn directly (and move with the
    // parallax); with blur they're only drawn into the render texture.
    m_images = CCNode::create();
    m_images->setContentSize(win);
    m_images->setAnchorPoint({0.5f, 0.5f});
    m_images->setPosition(win / 2);
    m_images->setVisible(!m_rt);
    this->addChild(m_images, -1);

    m_dim = CCLayerColor::create({0, 0, 0, 0});
    m_dim->setContentSize(win);
    this->addChild(m_dim, 1);
    setDim(dim);

    if (triangles) {
        float k = win.height / 768.f;
        auto tri = Triangles::create(win, 100.f * k * 1.6f, 40);
        tri->setAlphaRange(0.01f, 0.05f);
        tri->setVelocity(0.35f);
        this->addChild(tri, 2);
    }

    this->scheduleUpdate();
    return true;
}

void MenuBackground::setImage(CCTexture2D* texture) {
    if (texture == m_currentTexture) return;
    m_currentTexture = texture;

    // Whatever is showing fades out on top of the new image.
    for (auto& img : m_imageStack) {
        if (!img.leaving) {
            img.leaving = true;
            img.alpha.to(0.f, CROSSFADE_MS, Easing::OutQuint);
        }
    }
    if (!texture) return;

    auto win = this->getContentSize();
#ifndef GEODE_IS_MOBILE
    // Mipmaps, so shrinking the image into the quarter-size blur capture
    // averages pixels instead of skipping them (which shimmers and aliases).
    // GLES2 can't mipmap non-power-of-two textures, so desktop only.
    ccGLBindTexture2D(texture->getName());
    glGenerateMipmap(GL_TEXTURE_2D);
    ccTexParams params {GL_LINEAR_MIPMAP_LINEAR, GL_LINEAR, GL_CLAMP_TO_EDGE, GL_CLAMP_TO_EDGE};
    texture->setTexParameters(&params);
#endif

    auto sprite = CCSprite::createWithTexture(texture);
    auto size = sprite->getContentSize();
    if (size.width <= 0 || size.height <= 0) return;
    // FillMode.Fill: cover the screen, cropping the overflow.
    sprite->setScale(std::max(win.width / size.width, win.height / size.height));
    sprite->setPosition(win / 2);

    Image img {sprite, Tweened<float> {1.f}};
    if (m_imageStack.empty()) {
        // Nothing to fade out over GD's scene: fade the image in instead.
        img.alpha.set(0.f);
        img.alpha.to(1.f, CROSSFADE_MS, Easing::OutQuint);
    }
    // Newest goes underneath, so the old one visibly fades away over it.
    m_images->addChild(sprite, m_nextImageZ--);
    m_imageStack.push_back(std::move(img));
}

bool MenuBackground::imageCoversScreen() const {
    for (auto const& img : m_imageStack) {
        if (!img.leaving && img.alpha.get() >= 0.999f) return true;
    }
    return false;
}

void MenuBackground::update(float dt) {
    float ms = dt * 1000.f;

    // Image fades; drop images that have fully faded out.
    for (auto& img : m_imageStack) {
        img.alpha.update(dt);
        img.sprite->setOpacity(static_cast<GLubyte>(std::clamp(img.alpha.get(), 0.f, 1.f) * 255));
    }
    std::erase_if(m_imageStack, [](Image const& img) {
        bool gone = img.leaving && img.alpha.get() <= 0.001f;
        if (gone) img.sprite->removeFromParent();
        return gone;
    });

    // ParallaxContainer.Update: offset towards the mouse, with a soft falloff.
    auto win = this->getContentSize();
    auto half = win / 2;
    auto mouse = geode::cocos::getMousePos() - half;
    // Its falloff is in osu! pixels (768 tall); convert from GD units.
    float toOsu = 768.f / win.height;
    auto soft = [toOsu](float v) {
        float x = std::abs(v) * toOsu;
        return std::copysign(1.f - std::pow(0.999f, x), v);
    };
    CCPoint target {soft(mouse.x) * half.width * PARALLAX_AMOUNT, soft(mouse.y) * half.height * PARALLAX_AMOUNT};
    float t = static_cast<float>(ease(Easing::OutQuint, std::min(ms, PARALLAX_DURATION) / PARALLAX_DURATION));
    m_parallax = m_parallax + (target - m_parallax) * t;
    float ts = static_cast<float>(ease(Easing::OutQuint, std::min(ms, PARALLAX_SCALE_DURATION) / PARALLAX_SCALE_DURATION));
    m_parallaxScale += (1.f + PARALLAX_AMOUNT - m_parallaxScale) * ts;

    if (m_blurred) {
        m_blurred->setPosition(half + m_parallax);
        auto size = m_blurred->getContentSize();
        m_blurred->setScaleX(win.width / size.width * m_parallaxScale);
        m_blurred->setScaleY(win.height / size.height * m_parallaxScale);
    } else {
        // GD's own scene isn't ours to move; the level image is.
        m_images->setPosition(half + m_parallax);
        m_images->setScale(m_parallaxScale);
    }
}

void MenuBackground::setDim(float dim) {
    m_dim->setOpacity(static_cast<GLubyte>(std::clamp(dim, 0.f, 1.f) * 255));
}

void MenuBackground::visit() {
    if (!this->isVisible()) return;

    if (m_rt && m_source) {
        // Draw GD's live menu scene (unless a level image hides it) and the
        // level images into the small texture...
        bool covered = imageCoversScreen();
        m_rt->beginWithClear(0, 0, 0, 1);
        // The render texture captures a region of its own size (in points)
        // rather than shrinking the screen into it, so scale the scene down
        // to fit. Without this only the bottom-left corner shows, zoomed in.
        auto win = this->getContentSize();
        auto rtSize = m_rt->getSprite()->getContentSize();
        kmGLPushMatrix();
        kmGLScalef(rtSize.width / win.width, rtSize.height / win.height, 1.f);
        if (!covered) {
            m_source->setVisible(true);
            m_source->visit();
            m_source->setVisible(false);
        }
        m_images->setVisible(true);
        m_images->visit();
        m_images->setVisible(false);
        kmGLPopMatrix();
        m_rt->end();

        // ...blur it horizontally into the second texture...
        auto program = blurProgram();
        auto step = glGetUniformLocation(program->getProgram(), "u_step");
        auto tex = m_rt->getSprite()->getTexture();
        program->use();
        glUniform2f(step, 1.f / tex->getPixelsWide(), 0.f);
        m_rt2->beginWithClear(0, 0, 0, 1);
        m_horizontal->visit();
        m_rt2->end();

        // ...and vertically on the way to the screen (m_blurred, drawn below).
        program->use();
        glUniform2f(step, 0.f, 1.f / tex->getPixelsHigh());
    }

    CCNode::visit();
}

} // namespace lazer
