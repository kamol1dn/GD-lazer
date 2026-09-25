#pragma once

#include <Geode/cocos/include/cocos2d.h>

namespace lazer {

class Triangles;

// osu!-style backdrop for GD's animated menu scene: the scene keeps running,
// but is drawn through a blur, dimmed, with triangles drifting on top.
class MenuBackground : public cocos2d::CCNode {
public:
    // `source` is GD's MenuGameLayer. It is hidden and re-drawn by this node
    // (through the blur) when blur is on.
    static MenuBackground* create(cocos2d::CCNode* source, float dim, bool blur, bool triangles);

    void visit() override;
    ~MenuBackground() override { CC_SAFE_RELEASE(m_rt); }

protected:
    bool init(cocos2d::CCNode* source, float dim, bool blur, bool triangles);
    static cocos2d::CCGLProgram* blurProgram();

    cocos2d::CCNode* m_source = nullptr;
    cocos2d::CCRenderTexture* m_rt = nullptr;
    cocos2d::CCSprite* m_blurred = nullptr;
};

} // namespace lazer
