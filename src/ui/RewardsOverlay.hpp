#pragma once

#include "WaveOverlay.hpp"

#include <Geode/Geode.hpp>

namespace lazer {

// Daily chests as an osu!-style overlay. A hidden RewardsPage does the real
// work (server status, timers, GD's unlock animation); this only presents it.
class RewardsOverlay : public WaveOverlay {
public:
    static RewardsOverlay* create(float topInset);

protected:
    bool init(float topInset);
    void onOpened() override;
    void onUpdate(float dt) override;

    struct Card {
        cocos2d::CCNode* chestHolder = nullptr;
        cocos2d::CCSprite* chest = nullptr;
        cocos2d::CCLabelBMFont* status = nullptr;
        ButtonRow* button = nullptr;
        cocos2d::CCSpriteFrame* lastFrame = nullptr;
        Tweened<float> bounce {1.f};
        bool ready = false;
    };
    Card makeCard(int index, std::string const& name, cocos2d::CCPoint center, cocos2d::CCSize size);
    void updateCard(Card& card, CCMenuItemSpriteExtra* chestButton, cocos2d::CCLabelBMFont* label, bool open);
    void openChest(int index);

    geode::Ref<RewardsPage> m_page;
    Card m_cards[2];
    float m_idleTime = 0;
};

} // namespace lazer
