#include "ScrollArea.hpp"

#include "Easing.hpp"

#include <Geode/Geode.hpp>

using namespace geode::prelude;

namespace lazer {

namespace {
    // osu-framework ScrollContainer decays the remaining distance by ~1% per ms.
    constexpr double DECAY_PER_MS = 0.989;
    constexpr float WHEEL_STEP = 40.f;       // GD units per wheel notch (about three rows)
    constexpr float UNITS_PER_NOTCH = 12.f;  // GD reports ~12 per physical notch
}

ScrollArea* ScrollArea::create(CCSize size) {
    auto ret = new ScrollArea();
    if (ret->init(size)) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

bool ScrollArea::init(CCSize size) {
    if (!CCNode::init()) return false;
    this->setContentSize(size);
    m_content = CCNode::create();
    this->addChild(m_content);
    this->scheduleUpdate();
    this->update(0);
    return true;
}

void ScrollArea::onEnter() {
    CCNode::onEnter();
    CCDirector::sharedDirector()->getMouseDispatcher()->addDelegate(this);
}

void ScrollArea::onExit() {
    CCDirector::sharedDirector()->getMouseDispatcher()->removeDelegate(this);
    CCNode::onExit();
}

float ScrollArea::maxScroll() const {
    return std::max(0.f, m_contentHeight - this->getContentSize().height);
}

void ScrollArea::setContentHeight(float h) {
    m_contentHeight = h;
    m_target = std::clamp(m_target, 0.f, maxScroll());
}

void ScrollArea::scrollTo(float y, bool animated) {
    m_target = std::clamp(y, 0.f, maxScroll());
    if (!animated) m_current = m_target;
}

bool ScrollArea::containsWorldPoint(CCPoint p) {
    auto local = this->convertToNodeSpace(p);
    auto size = this->getContentSize();
    return local.x >= 0 && local.y >= 0 && local.x <= size.width && local.y <= size.height;
}

void ScrollArea::scrollWheel(float y, float) {
    if (!this->isVisible() || !containsWorldPoint(geode::cocos::getMousePos())) return;
    for (auto n = this->getParent(); n; n = n->getParent()) {
        if (!n->isVisible()) return;
    }
    // Positive = scroll down. Cap each event at a few notches so a fast flick
    // (or a free-spinning wheel) can't fling the list to the end.
    float notches = std::clamp(y / UNITS_PER_NOTCH, -3.f, 3.f);
    m_target = std::clamp(m_target + notches * WHEEL_STEP, 0.f, maxScroll());
}

void ScrollArea::beginDrag() {
    m_dragging = true;
    m_target = m_current;
}

void ScrollArea::dragBy(float dy) {
    // Resist past the ends, like osu!'s elastic overscroll.
    float next = m_current + dy;
    if (next < 0 || next > maxScroll()) dy *= 0.35f;
    m_current += dy;
    m_target = m_current;
}

void ScrollArea::endDrag(float velocity) {
    m_dragging = false;
    // Carry some momentum, then settle within bounds.
    m_target = std::clamp(m_current + velocity * 0.25f, 0.f, maxScroll());
}

void ScrollArea::update(float dt) {
    if (!m_dragging) {
        m_current = damp(m_current, m_target, DECAY_PER_MS, dt * 1000.0);
        if (std::abs(m_current - m_target) < 0.01f) m_current = m_target;
    }
    // Content hangs from the top edge; scrolling moves it up.
    m_content->setPosition({0, this->getContentSize().height + m_current});
}

void ScrollArea::visit() {
    if (!this->isVisible()) return;

    // Clip to our rectangle in screen space.
    auto size = this->getContentSize();
    auto bl = this->convertToWorldSpace({0, 0});
    auto tr = this->convertToWorldSpace({size.width, size.height});

    bool wasEnabled = glIsEnabled(GL_SCISSOR_TEST);
    GLint prev[4];
    glGetIntegerv(GL_SCISSOR_BOX, prev);

    glEnable(GL_SCISSOR_TEST);
    CCEGLView::sharedOpenGLView()->setScissorInPoints(bl.x, bl.y, tr.x - bl.x, tr.y - bl.y);
    CCNode::visit();

    if (wasEnabled) glScissor(prev[0], prev[1], prev[2], prev[3]);
    else glDisable(GL_SCISSOR_TEST);
}

} // namespace lazer
