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
    // Re-register for the mouse wheel so we're the newest delegate again
    // (e.g. after a hidden GD list registered itself on top of us).
    void claimWheel();
    // GD's mouse dispatcher only feeds the newest delegate. When an owner routes
    // the wheel itself (calling scrollWheel), turn this off before adding the
    // area so it doesn't take the wheel from its owner.
    void setOwnsWheel(bool owns) { m_ownsWheel = owns; }

protected:
    bool init(cocos2d::CCSize size);
    float maxScroll() const;

    cocos2d::CCNode* m_content = nullptr;
    float m_contentHeight = 0;
    float m_current = 0;
    float m_target = 0;
    bool m_dragging = false;
    bool m_ownsWheel = true;
};

// Turns an owner's touches into drag scrolling for a ScrollArea: forward
// began / moved / ended; a touch only starts scrolling once it has moved a
// few units, so taps still reach the owner's buttons.
class ScrollDragger {
public:
    explicit ScrollDragger(float threshold = 5.f) : m_threshold(threshold) {}

    void began(ScrollArea* area, cocos2d::CCPoint loc);
    // Returns true while the touch is scrolling (the owner should ignore it).
    bool moved(cocos2d::CCPoint loc);
    // Returns true if the touch was a scroll (not a tap).
    bool ended();
    bool dragging() const { return m_dragging; }

private:
    ScrollArea* m_area = nullptr;
    cocos2d::CCPoint m_start, m_last;
    bool m_dragging = false;
    float m_velocity = 0;
    float m_threshold;
};

} // namespace lazer
