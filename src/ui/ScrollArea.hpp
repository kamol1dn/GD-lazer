#pragma once

#include <Geode/cocos/include/cocos2d.h>
#include <Geode/cocos/robtop/mouse_dispatcher/CCMouseDelegate.h>

namespace lazer {

// Vertical, clipped scroll view with osu!-style smooth scrolling: wheel and
// drag set a target, the view eases towards it every frame (frame-rate
// independent), with elastic overscroll at the ends.
// Put children in content(); positions are measured from the top (y grows downwards).
class ScrollArea : public cocos2d::CCNode, public cocos2d::CCMouseDelegate {
public:
    static ScrollArea* create(cocos2d::CCSize size);

    cocos2d::CCNode* content() const { return m_content; }
    void setContentHeight(float h);
    float contentHeight() const { return m_contentHeight; }

    float scroll() const { return m_current; }
    void scrollTo(float y, bool animated = true);

    // Dragging is driven by the owner's touch handling.
    void beginDrag();
    void dragBy(float dy);
    void endDrag(float velocity);

    bool containsWorldPoint(cocos2d::CCPoint p);

    void update(float dt) override;
    void visit() override;
    void onEnter() override;
    void onExit() override;
    void scrollWheel(float y, float x) override;

protected:
    bool init(cocos2d::CCSize size);
    float maxScroll() const;

    cocos2d::CCNode* m_content = nullptr;
    float m_contentHeight = 0;
    float m_current = 0;
    float m_target = 0;
    bool m_dragging = false;
};

} // namespace lazer
