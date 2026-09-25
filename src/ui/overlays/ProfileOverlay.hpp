#pragma once

#include "../core/ScrollArea.hpp"
#include "WaveOverlay.hpp"

#include <Geode/Geode.hpp>
#include <functional>
#include <string>
#include <vector>

namespace lazer {

// Any player's profile as an osu!-style full-screen page (after osu!'s
// UserProfileOverlay): a cover in their colours, their icon and name, rank,
// Better Progression level, stats, their whole icon set, demon / star
// breakdowns and their profile posts.
//
// GD's own ProfilePage runs hidden underneath: it loads the user, and its
// handlers do friend requests, messages, following, blocking and so on, with
// GD's popups. Every ProfilePage GD opens is routed here (see ProfileOverlay.cpp).
class ProfileOverlay : public WaveOverlay, public cocos2d::CCKeypadDelegate {
public:
    // Shows `page` (a fresh, not yet shown ProfilePage) in the running scene.
    static void present(ProfilePage* page);

    bool ccTouchBegan(cocos2d::CCTouch* touch, cocos2d::CCEvent* e) override;
    void ccTouchMoved(cocos2d::CCTouch* touch, cocos2d::CCEvent* e) override;
    void ccTouchEnded(cocos2d::CCTouch* touch, cocos2d::CCEvent* e) override;
    void keyBackClicked() override;
    void onEnter() override;
    void onExit() override;

protected:
    struct Pill {
        cocos2d::CCNode* node;
        RoundedBox* bg;
        cocos2d::ccColor4B color;
        std::function<void()> action;
        bool hovered = false;
    };

    bool init(ProfilePage* page, theme::Scheme scheme);
    void onUpdate(float dt) override;
    void onClosed() override;

    void rebuild();
    float buildHeader(float y);
    float buildActions(float y);
    float buildStats(float y);
    float buildIcons(float y);
    float buildBreakdown(float y);
    float buildPosts(float y);
    float addSectionTitle(std::string const& title, float y);
    // A rounded pill button in the scroll content; returns its width.
    float addPill(char const* glyph, std::string const& label, float x, float y,
                  std::function<void()> action, cocos2d::ccColor4B color);
    std::string stateSignature() const;

    geode::Ref<ProfilePage> m_page;
    GJUserScore* m_shownScore = nullptr;
    int m_shownComments = -1;
    std::string m_signature;

    ScrollArea* m_scroll = nullptr;
    ScrollDragger m_drag;
    cocos2d::CCLabelBMFont* m_status = nullptr;
    std::vector<Pill> m_pills;
    Pill* m_pressed = nullptr;
    float m_pad = 0;
};

} // namespace lazer
