#include "RewardsOverlay.hpp"

#include "Text.hpp"

#include <Geode/modify/RewardUnlockLayer.hpp>
#include <Geode/modify/RewardsPage.hpp>

#include <cmath>

using namespace geode::prelude;

namespace lazer {

namespace {
    // osu!'s Orange overlay scheme: warm, fits treasure.
    constexpr theme::Scheme SCHEME {45};

}

RewardsOverlay* RewardsOverlay::create(float topInset) {
    auto ret = new RewardsOverlay();
    if (ret->init(topInset)) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

bool RewardsOverlay::init(float topInset) {
    if (!WaveOverlay::init(topInset, SCHEME, icon::GIFT, "rewards", "free chests, every few hours")) return false;

    // GD's own page: it talks to the server and runs the timers. It lives in
    // the scene (so anything GD attaches to it while revealing a reward
    // draws) but is invisible and takes no input.
    m_page = RewardsPage::create();
    m_page->setUserObject("hidden"_spr, CCBool::create(true));
    m_page->setTouchEnabled(false);
    m_page->setKeypadEnabled(false);
    m_page->setOpacity(0);
    if (m_page->m_mainLayer) m_page->m_mainLayer->setVisible(false);
    this->addChild(m_page, 100);

    auto size = bodySize();
    CCSize cardSize {280 * m_k, 360 * m_k};
    float gap = 40 * m_k;
    float cy = size.height / 2;
    m_cards[0] = makeCard(0, "small chest", {size.width / 2 - cardSize.width / 2 - gap / 2, cy}, cardSize);
    m_cards[1] = makeCard(1, "large chest", {size.width / 2 + cardSize.width / 2 + gap / 2, cy}, cardSize);
    return true;
}

RewardsOverlay::Card RewardsOverlay::makeCard(int index, std::string const& name, CCPoint center, CCSize size) {
    Card card;
    auto bg = RoundedBox::create(size, 14 * m_k, m_scheme.background4());
    bg->setShadow(16 * m_k, {0, 0, 0, 90});
    bg->setPosition(center);
    body()->addChild(bg);

    card.chestHolder = CCNode::create();
    card.chestHolder->setPosition({size.width / 2, size.height - 130 * m_k});
    bg->addChild(card.chestHolder, 1);

    auto title = makeText(name, Weight::SemiBold, 28 * m_k);
    title->setPosition({size.width / 2, 138 * m_k});
    bg->addChild(title, 1);

    card.status = makeText("...", Weight::Regular, 20 * m_k);
    card.status->setColor(theme::rgb(m_scheme.content2()));
    card.status->setPosition({size.width / 2, 106 * m_k});
    bg->addChild(card.status, 1);

    float buttonW = size.width - 60 * m_k;
    card.button = ButtonRow::create("open", buttonW, m_k, [this, index] { this->openChest(index); });
    card.button->setColor(m_scheme.colour3());
    card.button->setPosition({30 * m_k, 26 * m_k});
    bg->addChild(card.button, 1);
    addInteractive(card.button);
    return card;
}

void RewardsOverlay::onOpened() {
    // Ask GD for fresh chest status each time the overlay opens.
    if (m_page) m_page->tryGetRewards();
}

void RewardsOverlay::updateCard(Card& card, CCMenuItemSpriteExtra* chestButton, CCLabelBMFont* label, bool open) {
    // Mirror GD's chest sprite (closed / ready / opened art) as it changes.
    if (chestButton) {
        if (auto normal = typeinfo_cast<CCSprite*>(chestButton->getNormalImage())) {
            auto frame = normal->displayFrame();
            if (frame && frame != card.lastFrame) {
                card.lastFrame = frame;
                if (!card.chest) {
                    card.chest = CCSprite::createWithSpriteFrame(frame);
                    card.chestHolder->addChild(card.chest);
                } else {
                    card.chest->setDisplayFrame(frame);
                }
                auto s = card.chest->getContentSize();
                card.chest->setScale(170 * m_k / std::max(s.width, s.height));
            }
        }
    }

    if (open != card.ready) {
        card.ready = open;
        if (open) card.bounce.set(0.85f), card.bounce.to(1.f, 600, Easing::OutElastic);
    }
    card.bounce.update(CCDirector::sharedDirector()->getDeltaTime());

    if (open) {
        card.status->setString("ready to open!");
        card.status->setColor(theme::rgb(m_scheme.highlight1()));
        // Ready chests breathe gently.
        card.chestHolder->setScale(card.bounce.get() * (1.f + 0.03f * std::sin(m_idleTime * 3.f)));
    } else {
        std::string text = label ? label->getString() : "...";
        card.status->setString(text.empty() ? "..." : text.c_str());
        card.status->setColor(theme::rgb(m_scheme.content2()));
        card.chestHolder->setScale(card.bounce.get());
    }
    card.button->setEnabled(open);
}

void RewardsOverlay::onUpdate(float dt) {
    m_idleTime += dt;
    if (!m_page) return;
    // The page is in the scene, so it runs its own timer schedule.
    updateCard(m_cards[0], m_page->m_leftChest, m_page->m_leftLabel, m_page->m_leftOpen);
    updateCard(m_cards[1], m_page->m_rightChest, m_page->m_rightLabel, m_page->m_rightOpen);
}

void RewardsOverlay::openChest(int index) {
    if (!m_page) return;
    log::info("Opening chest {}", index);
    // Same as clicking the chest on GD's page: GD's unlock animation plays over the overlay.
    if (index == 0 && m_page->m_leftOpen) m_page->onReward(m_page->m_leftChest);
    if (index == 1 && m_page->m_rightOpen) m_page->onReward(m_page->m_rightChest);
}

} // namespace lazer

// Our hidden RewardsPage must never grab touches (FLAlertLayer registers at a
// very high priority and swallows everything).
class $modify(LazerHiddenRewardsPage, RewardsPage) {
    void registerWithTouchDispatcher() {
        if (this->getUserObject("hidden"_spr)) return;
        RewardsPage::registerWithTouchDispatcher();
    }
};

class $modify(LazerRewardUnlockLayer, RewardUnlockLayer) {
    bool showCollectReward(GJRewardItem* item) {
        log::info("Chest reward arrived: {}", item ? "yes" : "none");
        return RewardUnlockLayer::showCollectReward(item);
    }
};
