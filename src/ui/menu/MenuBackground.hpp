#pragma once

#include "../core/Easing.hpp"
#include "../core/Parallax.hpp"

#include <Geode/cocos/include/cocos2d.h>
#include <vector>

namespace lazer {

class Triangles;

// osu!-style backdrop. Shows either GD's animated menu scene (kept running) or
// a level image (the current song's level), drawn through a blur, dimmed, with
// triangles drifting on top, and following the mouse (or the phone's tilt) a
// little (ParallaxContainer).
class MenuBackground : public cocos2d::CCNode {
public:
    // `source` is GD's MenuGameLayer. It is hidden and re-drawn by this node
    // (through the blur) when blur is on.
    static MenuBackground* create(cocos2d::CCNode* source, float dim, bool blur, bool triangles);

    void setDim(float dim);
    // Crossfades to `texture` (cover-fit), or back to GD's scene with nullptr.
    void setImage(cocos2d::CCTexture2D* texture);

    void update(float dt) override;
    void visit() override;
    // The tilt sensor (phones) runs while a background is on screen.
    void onEnter() override;
    void onExit() override;
    ~MenuBackground() override {
        CC_SAFE_RELEASE(m_rt);
        CC_SAFE_RELEASE(m_rt2);
        CC_SAFE_RELEASE(m_horizontal);
    }

protected:
    struct Image {
        cocos2d::CCSprite* sprite;
        Tweened<float> alpha;
        bool leaving = false;
    };

    bool init(cocos2d::CCNode* source, float dim, bool blur, bool triangles);
    static cocos2d::CCGLProgram* blurProgram();
    bool imageCoversScreen() const;

    cocos2d::CCNode* m_source = nullptr;
    cocos2d::CCRenderTexture* m_rt = nullptr;  // quarter-size capture of the scene
    cocos2d::CCRenderTexture* m_rt2 = nullptr; // after the horizontal blur pass
    cocos2d::CCSprite* m_horizontal = nullptr;
    cocos2d::CCSprite* m_blurred = nullptr;
    cocos2d::CCLayerColor* m_dim = nullptr;

    // Level images, oldest first. Drawn over GD's scene; in blur mode they're
    // only drawn into the render texture.
    cocos2d::CCNode* m_images = nullptr;
    std::vector<Image> m_imageStack;
    cocos2d::CCTexture2D* m_currentTexture = nullptr;
    int m_nextImageZ = 0; // decreasing: each new image goes under the previous ones

    Parallax m_parallax {"parallax-background"};
};

} // namespace lazer
