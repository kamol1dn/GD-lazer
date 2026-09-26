#include "QuestsOverlay.hpp"

#include "../core/Text.hpp"

#include <Geode/modify/ChallengesPage.hpp>

#include <cmath>

using namespace geode::prelude;

namespace lazer {

namespace {
    // osu!'s Lime overlay scheme: quests are about progress.
    constexpr theme::Scheme SCHEME {90};

    char const* typeFrame(GJChallengeType type) {
        switch (type) {
            case GJChallengeType::Orbs: return "currencyOrbIcon_001.png";
            case GJChallengeType::UserCoins: return "GJ_coinsIcon2_001.png";
            case GJChallengeType::Stars: return "GJ_starsIcon_001.png";
            case GJChallengeType::Moons: return "GJ_moonsIcon_001.png";
            default: return "GJ_starsIcon_001.png";
        }
    }

    char const* typeNoun(GJChallengeType type, int count) {
        bool one = count == 1;
        switch (type) {
            case GJChallengeType::Orbs: return one ? "orb" : "orbs";
            case GJChallengeType::UserCoins: return one ? "user coin" : "user coins";
            case GJChallengeType::Stars: return one ? "star" : "stars";
            case GJChallengeType::Moons: return one ? "moon" : "moons";
            default: return "things";
        }
    }

    void setFrame(CCSprite* sprite, char const* frame, float size) {
        if (auto f = CCSpriteFrameCache::sharedSpriteFrameCache()->spriteFrameByName(frame)) sprite->setDisplayFrame(f);
        auto s = sprite->getContentSize();
        sprite->setScale(size / std::max(1.f, std::max(s.width, s.height)));
    }
}

QuestsOverlay* QuestsOverlay::create(float topInset) {
    auto ret = new QuestsOverlay();
    if (ret->init(topInset)) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

bool QuestsOverlay::init(float topInset) {
    if (!WaveOverlay::init(topInset, SCHEME, icon::LIST_CHECK, "quests", "collect things, earn diamonds")) return false;

    // GD's own page, invisible and without input (see RewardsOverlay).
    m_page = ChallengesPage::create();
    m_page->setUserObject("hidden"_spr, CCBool::create(true));
    m_page->setTouchEnabled(false);
    m_page->setKeypadEnabled(false);
    m_page->setOpacity(0);
    if (m_page->m_mainLayer) m_page->m_mainLayer->setVisible(false);
    this->addChild(m_page, 100);

    auto size = bodySize();
    float k = m_k;
    CCSize cardSize {std::min(size.width - 80 * k, 900 * k), 118 * k};
    float gap = 18 * k;
    float statusH = 44 * k;
    float margin = 20 * k;
    // Cards plus the timer line under them; shrink it all to fit short screens
    // (phones run a bigger UI scale).
    float total = cardSize.height * 3 + gap * 2 + statusH;
    float scale = std::min({1.f, (size.height - margin * 2) / total, (size.width - 40 * k) / cardSize.width});
    auto column = CCNode::create();
    column->setContentSize({cardSize.width, total});
    column->setAnchorPoint({0.5f, 0.5f});
    column->setPosition({size.width / 2, size.height / 2});
    column->setScale(scale);
    body()->addChild(column);
    for (int i = 0; i < 3; i++) {
        m_cards[i] = makeCard(column, i + 1, {0, total - (i + 1) * cardSize.height - i * gap}, cardSize);
    }

    m_status = makeText("", Weight::Regular, 18 * k);
    m_status->setColor(theme::rgb(m_scheme.content2()));
    m_status->setPosition({cardSize.width / 2, statusH / 2});
    column->addChild(m_status);
    return true;
}

QuestsOverlay::Card QuestsOverlay::makeCard(CCNode* parent, int slot, CCPoint origin, CCSize size) {
    Card card;
    float k = m_k;
    card.bg = RoundedBox::create(size, 14 * k, m_scheme.background4());
    card.bg->setShadow(14 * k, {0, 0, 0, 80});
    card.bg->setAnchorPoint({0, 0});
    card.bg->setPosition(origin);
    parent->addChild(card.bg);

    // What to collect, in a tile on the left.
    float tile = size.height - 28 * k;
    auto tileBg = RoundedBox::create({tile, tile}, 12 * k, m_scheme.background5());
    tileBg->setAnchorPoint({0, 0.5f});
    tileBg->setPosition({14 * k, size.height / 2});
    card.bg->addChild(tileBg, 1);
    card.typeIcon = CCSprite::create();
    card.typeIcon->setPosition({14 * k + tile / 2, size.height / 2});
    card.bg->addChild(card.typeIcon, 2);

    float x = 14 * k + tile + 20 * k;
    float right = size.width - 190 * k;
    card.name = makeText("", Weight::SemiBold, 24 * k);
    card.name->setAnchorPoint({0, 0.5f});
    card.name->setPosition({x, size.height - 28 * k});
    card.bg->addChild(card.name, 1);

    card.goal = makeText("", Weight::Regular, 17 * k);
    card.goal->setColor(theme::rgb(m_scheme.content2()));
    card.goal->setAnchorPoint({0, 0.5f});
    card.goal->setPosition({x, size.height - 56 * k});
    card.bg->addChild(card.goal, 1);

    // Progress bar.
    card.barWidth = right - x - 70 * k;
    auto track = RoundedBox::create({card.barWidth, 10 * k}, 5 * k, {255, 255, 255, 28});
    track->setAnchorPoint({0, 0.5f});
    track->setPosition({x, 26 * k});
    card.bg->addChild(track, 1);
    card.barFill = RoundedBox::create({card.barWidth, 10 * k}, 5 * k, m_scheme.highlight1());
    card.barFill->setAnchorPoint({0, 0.5f});
    card.barFill->setPosition({x, 26 * k});
    card.bg->addChild(card.barFill, 2);
    card.progress = makeText("", Weight::SemiBold, 16 * k);
    card.progress->setAnchorPoint({0, 0.5f});
    card.progress->setPosition({x + card.barWidth + 12 * k, 26 * k});
    card.bg->addChild(card.progress, 1);

    // Reward and claim button on the right.
    card.reward = CCNode::create();
    card.reward->setPosition({size.width - 100 * k, size.height - 36 * k});
    card.bg->addChild(card.reward, 1);
    auto diamond = CCSprite::create();
    setFrame(diamond, "GJ_diamondsIcon_001.png", 26 * k);
    diamond->setPosition({-16 * k, 0});
    card.reward->addChild(diamond);
    card.rewardLabel = makeText("", Weight::SemiBold, 22 * k);
    card.rewardLabel->setAnchorPoint({0, 0.5f});
    card.rewardLabel->setPosition({2 * k, 0});
    card.reward->addChild(card.rewardLabel);

    card.claim = ButtonRow::create("claim", 150 * k, k, [this, slot] { this->claim(slot); });
    card.claim->setColor(m_scheme.colour3());
    card.claim->setPosition({size.width - 170 * k, 12 * k});
    card.bg->addChild(card.claim, 1);
    addInteractive(card.claim);
    return card;
}

void QuestsOverlay::onOpened() {
    // Fresh quests from GD's server each time the overlay opens.
    if (m_page) m_page->tryGetChallenges();
}

void QuestsOverlay::updateCard(int slot, float dt) {
    auto& card = m_cards[slot - 1];
    auto stats = GameStatsManager::sharedState();
    auto item = stats->areChallengesLoaded() ? stats->getChallenge(slot) : nullptr;

    int count = 0, goal = 1;
    std::string state;
    if (item) {
        count = item->m_count.value();
        goal = std::max(1, item->m_goal.value());
        state = fmt::format("{}|{}|{}|{}|{}", std::string(item->m_name), count, goal, item->m_reward.value(), item->m_canClaim);
    } else {
        state = "empty";
    }

    if (state != card.shown) {
        card.shown = state;
        float k = m_k;
        if (item) {
            auto type = item->m_challengeType;
            setFrame(card.typeIcon, typeFrame(type), 54 * k);
            card.typeIcon->setOpacity(255);
            card.name->setString(std::string(item->m_name).c_str());
            card.goal->setString(fmt::format("collect {} {}", goal, typeNoun(type, goal)).c_str());
            card.progress->setString(fmt::format("{}/{}", std::min(count, goal), goal).c_str());
            card.rewardLabel->setString(std::to_string(item->m_reward.value()).c_str());
            card.reward->setVisible(true);
            card.claim->setVisible(true);
            card.fill.to(std::clamp(count / static_cast<float>(goal), 0.f, 1.f), 600, Easing::OutQuint);
            card.glow.to(item->m_canClaim ? 1.f : 0.f, 400, Easing::OutQuint);
        } else {
            // Claimed: GD brings a new quest into this slot after a while.
            setFrame(card.typeIcon, "GJ_starsIcon_001.png", 54 * k);
            card.typeIcon->setOpacity(60);
            card.name->setString("new quest on its way");
            card.progress->setString("");
            card.reward->setVisible(false);
            card.claim->setVisible(false);
            card.fill.to(0, 400, Easing::OutQuint);
            card.glow.to(0, 400, Easing::OutQuint);
        }
    }
    // The empty slot's countdown ticks every second.
    if (!item) {
        auto text = stats->areChallengesLoaded() ? "check back soon" : "loading...";
        if (card.goal->getString() != std::string_view(text)) card.goal->setString(text);
    }

    card.claim->setEnabled(item && item->m_canClaim);
    card.fill.update(dt);
    card.glow.update(dt);
    float fill = card.fill.get();
    card.barFill->setVisible(fill > 0.001f);
    // Resize rather than scale, so the rounded ends stay round.
    float barH = card.barFill->getContentSize().height;
    card.barFill->setContentSize({std::max(barH, card.barWidth * fill), barH});
    // Claimable quests get a pulsing highlight border.
    float glow = card.glow.get() * (0.75f + 0.25f * std::sin(m_time * 4.f));
    auto hl = m_scheme.highlight1();
    card.bg->setBorder(2.5f * m_k * card.glow.get(), {hl.r, hl.g, hl.b, static_cast<GLubyte>(255 * glow)});
}

void QuestsOverlay::onUpdate(float dt) {
    m_time += dt;
    if (!m_page) return;
    for (int slot = 1; slot <= 3; slot++) updateCard(slot, dt);

    auto stats = GameStatsManager::sharedState();
    std::string status;
    if (!stats->areChallengesLoaded()) {
        status = m_page->m_triedToLoad ? "couldn't load quests. Check your connection and reopen." : "loading quests...";
    } else if (m_page->m_countdownLabel) {
        // GD's own label reads "New quests in ..." (it also keeps the timer).
        status = m_page->m_countdownLabel->getString();
        for (auto& c : status) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    if (m_status->getString() != status) m_status->setString(status.c_str());
}

void QuestsOverlay::claim(int slot) {
    if (!m_page || !m_page->m_challengeNodes) return;
    auto item = GameStatsManager::sharedState()->getChallenge(slot);
    if (!item || !item->m_canClaim) return;
    // Same as pressing "claim" on GD's quest node: GD pays out and plays the diamond animation.
    auto nodes = m_page->m_challengeNodes->allKeys();
    auto values = CCArray::create();
    if (nodes) {
        for (auto key : CCArrayExt<CCObject*>(nodes)) {
            CCObject* value = nullptr;
            if (auto str = typeinfo_cast<CCString*>(key)) value = m_page->m_challengeNodes->objectForKey(str->getCString());
            else if (auto num = typeinfo_cast<CCInteger*>(key)) value = m_page->m_challengeNodes->objectForKey(num->getValue());
            if (value) values->addObject(value);
        }
    }
    for (auto object : CCArrayExt<CCObject*>(values)) {
        auto node = typeinfo_cast<ChallengeNode*>(object);
        if (!node || node->m_challengeItem != item) continue;
        log::info("Claiming quest {}", slot);
        // onClaimReward uses the button for the animation's start position.
        CCObject* sender = node;
        std::function<CCMenuItem*(CCNode*)> find = [&](CCNode* parent) -> CCMenuItem* {
            for (auto child : CCArrayExt<CCNode*>(parent->getChildren())) {
                if (auto button = typeinfo_cast<CCMenuItem*>(child)) return button;
                if (auto found = find(child)) return found;
            }
            return nullptr;
        };
        if (auto button = find(node)) sender = button;
        node->onClaimReward(sender);
        return;
    }
    log::warn("Quest {} has no node to claim through", slot);
}

} // namespace lazer

class $modify(LazerHiddenChallengesPage, ChallengesPage) {
    // Never grab touches (FLAlertLayer swallows everything at a high priority).
    void registerWithTouchDispatcher() {
        if (this->getUserObject("hidden"_spr)) return;
        ChallengesPage::registerWithTouchDispatcher();
    }

    // The diamond animation lands in the hidden main layer: move it onto the
    // (visible, transparent) page so it plays over the overlay.
    void claimItem(ChallengeNode* node, GJChallengeItem* item, CCPoint position) {
        ChallengesPage::claimItem(node, item, position);
        if (!this->getUserObject("hidden"_spr)) return;
        auto reward = m_currencyRewardLayer;
        if (reward && m_mainLayer && reward->getParent() == m_mainLayer) {
            Ref<CCNode> keep = reward;
            reward->removeFromParentAndCleanup(false);
            this->addChild(reward, 1000);
        }
    }
};
