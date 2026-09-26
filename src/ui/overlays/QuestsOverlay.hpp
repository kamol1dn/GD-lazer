#pragma once

#include "WaveOverlay.hpp"

#include <Geode/Geode.hpp>

namespace lazer {

// GD's quests as an osu!-style overlay: three quest cards with progress bars
// and diamond rewards. A hidden ChallengesPage does the real work (server,
// timers, claiming and the diamond animation); this only presents it, like
// RewardsOverlay does for chests.
class QuestsOverlay : public WaveOverlay {
public:
    static QuestsOverlay* create(float topInset);

protected:
    bool init(float topInset);
    void onOpened() override;
    void onUpdate(float dt) override;

    struct Card {
        RoundedBox* bg = nullptr;
        cocos2d::CCSprite* typeIcon = nullptr;
        cocos2d::CCLabelBMFont* name = nullptr;
        cocos2d::CCLabelBMFont* goal = nullptr;
        cocos2d::CCLabelBMFont* progress = nullptr;
        RoundedBox* barFill = nullptr;
        cocos2d::CCNode* reward = nullptr;
        cocos2d::CCLabelBMFont* rewardLabel = nullptr;
        ButtonRow* claim = nullptr;
        float barWidth = 0;
        Tweened<float> fill {0.f};
        Tweened<float> glow {0.f};
        std::string shown; // what the card currently shows, to skip redundant updates
    };
    Card makeCard(cocos2d::CCNode* parent, int slot, cocos2d::CCPoint origin, cocos2d::CCSize size);
    void updateCard(int slot, float dt);
    void claim(int slot);

    geode::Ref<ChallengesPage> m_page;
    Card m_cards[3];
    cocos2d::CCLabelBMFont* m_status = nullptr;
    float m_time = 0;
};

} // namespace lazer
