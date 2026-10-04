#include "RewardsPopup.hpp"

#include "../core/Easing.hpp"
#include "../core/Quips.hpp"
#include "../core/RoundedBox.hpp"
#include "../core/Text.hpp"
#include "../core/Theme.hpp"
#include "Dialog.hpp"

#include <Geode/Geode.hpp>
#include <Geode/modify/RewardUnlockLayer.hpp>
#include <Geode/modify/RewardsPage.hpp>

#include <algorithm>
#include <string>
#include <vector>

using namespace geode::prelude;

namespace lazer {

namespace {
    // One slot per chest: the chest, its name, timer and what it gave (osu! pixels).
    constexpr float SLOT_W = 200, SLOT_H = 252, SLOT_GAP = 24, SLOT_RADIUS = 10;
    constexpr float CHEST_AREA = 140;      // the chest's part of the slot, from its top
    constexpr float CHEST_SCALE = 1.15f;   // osu! pixels per point of GD's chest art
    constexpr float REWARD_ICON = 28;
    constexpr float DROP_FROM = 45;        // the chest drops in from this far above its place
    // RewardUnlockLayer's timeline: the chest drops for a second and lands on
    // its first bounce, the lid lifts at 1.2 s, and from 1.5 s the reward can
    // burst out (once the server has answered).
    constexpr float DROP_S = 1.f, LAND_S = 0.36f, LID_S = 1.2f, ARMED_S = 1.5f;
    constexpr float REWARD_STAGGER_S = 0.25f;
    constexpr float CLAIM_TIMEOUT_S = 10.f;  // no reward by then: give up on this chest
    constexpr float STATUS_TIMEOUT_S = 8.f;  // no chest status by then: say so
    constexpr ccColor4B SLOT_BG {255, 255, 255, 12};

    // Kept between popups (never freed): it holds the chest timers, and GD
    // made it the server's delegate.
    RewardsPage* s_page = nullptr;

    RewardsPage* hiddenPage(bool& fresh) {
        fresh = false;
        if (s_page) return s_page;
        auto page = RewardsPage::create();
        if (!page) return nullptr;
        page->retain();
        // Invisible and without input (it registers touches at a high
        // priority and would swallow everything).
        page->setUserObject("hidden"_spr, CCBool::create(true));
        page->setTouchEnabled(false);
        page->setKeypadEnabled(false);
        page->setOpacity(0);
        if (page->m_mainLayer) page->m_mainLayer->setVisible(false);
        s_page = page;
        fresh = true;
        return page;
    }

    // The server has told GD about the chests (their timers) at some point.
    bool hasChestStatus() {
        auto items = GameStatsManager::sharedState()->m_rewardItems;
        return items && items->objectForKey(static_cast<intptr_t>(1));
    }

    // The art RewardUnlockLayer shows for each kind of reward.
    char const* rewardFrame(SpecialRewardItem type) {
        switch (type) {
            case SpecialRewardItem::FireShard: return "fireShardBig_001.png";
            case SpecialRewardItem::IceShard: return "iceShardBig_001.png";
            case SpecialRewardItem::PoisonShard: return "poisonShardBig_001.png";
            case SpecialRewardItem::ShadowShard: return "shadowShardBig_001.png";
            case SpecialRewardItem::LavaShard: return "lavaShardBig_001.png";
            case SpecialRewardItem::BonusKey: return "GJ_bigKey_001.png";
            case SpecialRewardItem::Orbs: return "currencyOrbIcon_001.png";
            case SpecialRewardItem::Diamonds: return "GJ_bigDiamond_001.png";
            case SpecialRewardItem::EarthShard: return "shard0201ShardBig_001.png";
            case SpecialRewardItem::BloodShard: return "shard0202ShardBig_001.png";
            case SpecialRewardItem::MetalShard: return "shard0203ShardBig_001.png";
            case SpecialRewardItem::LightShard: return "shard0204ShardBig_001.png";
            case SpecialRewardItem::SoulShard: return "shard0205ShardBig_001.png";
            case SpecialRewardItem::GoldKey: return "GJ_bigGoldKey_001.png";
            default: return nullptr;
        }
    }

    // And the sound it plays as each kind lands.
    char const* rewardSound(SpecialRewardItem type) {
        switch (type) {
            case SpecialRewardItem::Diamonds: return "crystal01.ogg";
            case SpecialRewardItem::BonusKey: return "secretKey.ogg";
            case SpecialRewardItem::GoldKey: return "goldKey.ogg";
            case SpecialRewardItem::Orbs: return "gold01.ogg";
            default: return "gold02.ogg";
        }
    }

    // GD's own chest sounds, at GD's SFX volume: the chest is GD's.
    void playChestSound(char const* file) {
        FMODAudioEngine::sharedEngine()->playEffect(std::string(file));
    }

    void fitInto(CCNode* node, float box) {
        auto size = node->getContentSize();
        if (size.width <= 0 || size.height <= 0) return;
        node->setScale(box / std::max(size.width, size.height));
    }

    class RewardsPanel : public CCNode {
    public:
        static RewardsPanel* create() {
            auto ret = new RewardsPanel();
            if (ret->init()) {
                ret->autorelease();
                return ret;
            }
            delete ret;
            return nullptr;
        }

        ~RewardsPanel() override {
            if (s_panel == this) s_panel = nullptr;
        }

        // The hidden unlock layer for chest `chestType` (1 small, 2 large)
        // was handed its reward: GD has already counted it in.
        static void collected(int chestType, GJRewardItem* item) {
            if (!s_panel || chestType < 1 || chestType > 2 || !item) return;
            auto& slot = s_panel->m_slots[chestType - 1];
            if (!slot.requested || slot.item) return;
            slot.item = item;
            log::info("Chest {} gave {} rewards", chestType, item->m_rewardObjects ? item->m_rewardObjects->count() : 0);
        }

    private:
        enum class Phase { Idle, Dropping, Opening, Opened, Failed };

        struct Reward {
            CCNode* node = nullptr;
            Tweened<float> scale {0.f};
            float at = 0;          // seconds after the burst
            bool started = false;
            char const* sound = nullptr;
        };

        struct Slot {
            int type = 0;                                // GD's chest type: 1 small, 2 large
            CCMenuItemSpriteExtra* button = nullptr;     // the hidden page's chest, what onReward takes
            CCLabelBMFont* timer = nullptr;              // the hidden page's countdown
            RoundedBox* bg = nullptr;
            GJChestSprite* chest = nullptr;
            CCLabelBMFont* status = nullptr;
            std::string statusText;
            CCPoint restPoint;
            float rewardsY = 0;
            float rewardsFit = 1;                        // the row shrinks to fit a long haul
            Phase phase = Phase::Idle;
            float t = 0;                                 // seconds since it began opening
            bool landed = false, lidOpen = false;
            bool requested = false;
            float requestedAt = 0;
            Ref<GJRewardItem> item;
            std::vector<Reward> rewards;
        };

        bool init() override {
            if (!CCNode::init()) return false;
            m_k = unitScale();
            float k = m_k;
            this->setContentSize({(SLOT_W * 2 + SLOT_GAP) * k, SLOT_H * k});

            bool fresh = false;
            auto page = hiddenPage(fresh);
            if (!page || !page->m_leftChest || !page->m_rightChest) return false;
            m_freshPage = fresh;
            // An earlier popup's cleanup took its schedules with it; this one
            // ticks the timers itself. No cleanup here: that would too.
            page->removeFromParentAndCleanup(false);
            this->addChild(page, -1);
            // GD wires a page up as the server's delegate only when it's made:
            // another rewards page since (GD's own, say) would have taken over.
            GameLevelManager::sharedState()->m_GJRewardDelegate = page;

            buildSlot(m_slots[0], 1, "small chest", 0, page->m_leftChest, page->m_leftLabel);
            buildSlot(m_slots[1], 2, "large chest", (SLOT_W + SLOT_GAP) * k, page->m_rightChest, page->m_rightLabel);
            s_panel = this;
            this->scheduleUpdate();
            return true;
        }

        void buildSlot(Slot& slot, int type, char const* name, float x, CCMenuItemSpriteExtra* button, CCLabelBMFont* timer) {
            float k = m_k;
            slot.type = type;
            slot.button = button;
            slot.timer = timer;
            CCSize size {SLOT_W * k, SLOT_H * k};
            slot.bg = RoundedBox::create(size, SLOT_RADIUS * k, SLOT_BG);
            slot.bg->setAnchorPoint({0, 0});
            slot.bg->setPosition({x, 0});
            this->addChild(slot.bg);

            slot.restPoint = CCPoint {size.width / 2, size.height - CHEST_AREA / 2 * k};
            slot.chest = GJChestSprite::create(type);
            if (slot.chest) {
                slot.chest->setScale(CHEST_SCALE * k);
                slot.chest->setPosition(slot.restPoint);
                slot.bg->addChild(slot.chest, 2);
            }

            auto title = makeText(name, Weight::SemiBold, 20 * k);
            title->setAnchorPoint({0.5f, 1});
            title->setPosition({size.width / 2, size.height - (CHEST_AREA + 8) * k});
            slot.bg->addChild(title, 1);

            slot.status = makeText(" ", Weight::Regular, 17 * k);
            slot.status->setAnchorPoint({0.5f, 1});
            slot.status->setPosition({size.width / 2, size.height - (CHEST_AREA + 34) * k});
            slot.status->setColor(theme::CONTENT2);
            slot.bg->addChild(slot.status, 1);

            slot.rewardsY = size.height - (CHEST_AREA + 60 + REWARD_ICON / 2 + 6) * k;
        }

        bool ready(Slot const& slot) const {
            return slot.type == 1 ? s_page->m_leftOpen : s_page->m_rightOpen;
        }

        void setStatus(Slot& slot, std::string const& text, bool highlight = false) {
            if (text != slot.statusText) {
                slot.statusText = text;
                slot.status->setString(text.empty() ? " " : text.c_str());
            }
            slot.status->setColor(highlight ? theme::rgb(theme::HIGHLIGHT1) : theme::CONTENT2);
        }

        void update(float dt) override {
            auto page = s_page;
            if (!page) return;
            m_time += dt;
            // The dialog shows the panel once it has faded in: the chests wait for it.
            if (!m_started) {
                if (!this->isVisible()) return;
                m_started = true;
                // A claim an earlier popup left hanging (closed mid-request) would block new ones.
                if (page->m_openLayer) page->m_openLayer->onClose(nullptr);
                // No chest status yet: ask (a page just made asked already,
                // and asks again by itself while that fails).
                if (!hasChestStatus() && !m_freshPage) {
                    page->stopAllActions();
                    page->tryGetRewards();
                }
            }
            // GD's own once-a-second tick: it keeps the timers and "ready" flags.
            m_tick += dt;
            if (m_tick >= 1.f) {
                m_tick = 0;
                page->updateTimers(0);
            }
            bool status = hasChestStatus();
            for (auto& slot : m_slots) updateSlot(slot, dt, status);
        }

        void updateSlot(Slot& slot, float dt, bool status) {
            auto page = s_page;
            // Mac's rewardsStatusFinished inlines showCollectReward, bypassing
            // our hook. Read the accepted result GD stored on the unlock layer.
            if (slot.requested && !slot.item && page->m_openLayer) {
                auto unlock = page->m_openLayer;
                if (unlock->m_chestType == slot.type && unlock->m_rewardCollected && unlock->m_rewardItem) {
                    collected(slot.type, unlock->m_rewardItem);
                    Loader::get()->queueInMainThread([self = Ref<RewardUnlockLayer>(unlock)] {
                        self->onClose(nullptr);
                    });
                }
            }
            switch (slot.phase) {
                case Phase::Idle:
                    if (!status) {
                        setStatus(slot, m_time > STATUS_TIMEOUT_S ? "no connection" : "checking...");
                    } else if (ready(slot)) {
                        begin(slot);
                    } else {
                        std::string left = slot.timer ? slot.timer->getString() : "";
                        setStatus(slot, left.empty() ? "not yet" : "next in " + left);
                    }
                    return;
                case Phase::Failed:
                    return;
                case Phase::Opened:
                    for (auto& reward : slot.rewards) {
                        if (!reward.started && slot.t >= reward.at) {
                            reward.started = true;
                            reward.node->setVisible(true);
                            reward.scale.to(slot.rewardsFit, 600, Easing::OutElasticHalf);
                            if (reward.sound) playChestSound(reward.sound);
                        }
                        reward.scale.update(dt);
                        reward.node->setScale(reward.scale.get());
                    }
                    slot.t += dt;
                    return;
                default:
                    break;
            }

            // Dropping / Opening: GD's timeline, and the server's answer.
            slot.t += dt;
            if (!slot.landed && slot.t >= LAND_S) {
                slot.landed = true;
                playChestSound("chestLand.ogg");
            }
            if (!slot.lidOpen && slot.t >= LID_S) {
                slot.lidOpen = true;
                slot.phase = Phase::Opening;
                playChestSound("chestOpen01.ogg");
                if (slot.chest) slot.chest->switchToState(ChestSpriteState::Opening, false);
            }
            // One unlock layer at a time: the other chest's request waits for its turn.
            if (!slot.requested && !page->m_openLayer) request(slot);
            if (slot.requested && !slot.item && m_time - slot.requestedAt > CLAIM_TIMEOUT_S) {
                if (page->m_openLayer && page->m_openLayer->m_chestType == slot.type) page->m_openLayer->onClose(nullptr);
                fail(slot);
                return;
            }
            if (slot.item && slot.t >= ARMED_S) burst(slot);
        }

        void begin(Slot& slot) {
            slot.phase = Phase::Dropping;
            slot.t = 0;
            slot.landed = slot.lidOpen = false;
            setStatus(slot, "opening...", true);
            if (!slot.chest) return;
            // RewardUnlockLayer: the chest falls into place and bounces.
            slot.chest->stopAllActions();
            slot.chest->setPosition({slot.restPoint.x, slot.restPoint.y + DROP_FROM * m_k});
            slot.chest->setOpacity(0);
            slot.chest->runAction(CCEaseBounceOut::create(CCMoveTo::create(DROP_S, slot.restPoint)));
            slot.chest->runAction(CCFadeTo::create(0.2f, 255));
        }

        // What clicking the chest on GD's page does: a hidden unlock layer
        // and a request; GD claims the reward when the answer comes.
        void request(Slot& slot) {
            auto page = s_page;
            if (!ready(slot)) return fail(slot);
            page->onReward(slot.button);
            if (!page->m_openLayer) return fail(slot);
            slot.requested = true;
            slot.requestedAt = m_time;
            log::info("Opening chest {}", slot.type);
        }

        void fail(Slot& slot) {
            if (slot.chest) {
                slot.chest->stopAllActions();
                slot.chest->setPosition(slot.restPoint);
                slot.chest->setOpacity(255);
                slot.chest->switchToState(ChestSpriteState::Closed, false);
            }
            slot.requested = false;
            slot.item = nullptr;
            // The server said no (claimed elsewhere?): back to its timer.
            // Otherwise it stays put until the popup is opened again.
            slot.phase = ready(slot) ? Phase::Failed : Phase::Idle;
            if (slot.phase == Phase::Failed) setStatus(slot, "something went wrong");
        }

        void burst(Slot& slot) {
            slot.phase = Phase::Opened;
            slot.t = 0;
            quips::say("chest-open", 0.7f);
            setStatus(slot, "opened!", true);
            playChestSound("reward01.ogg");
            if (slot.chest) {
                slot.chest->switchToState(ChestSpriteState::Opened, false);
                // GD lays a wide coloured band and a blue glow behind an
                // opened chest, sized for its own popup: here they'd cover
                // the slot. The chest, its rays and sparks stay.
                auto cache = CCSpriteFrameCache::get();
                CCSpriteFrame* backdrops[] = {
                    cache->spriteFrameByName("whiteSquare60_001.png"),
                    cache->spriteFrameByName("chest_glow_bg_001.png"),
                };
                for (auto child : CCArrayExt<CCNode*>(slot.chest->getChildren())) {
                    auto sprite = typeinfo_cast<CCSprite*>(child);
                    if (!sprite) continue;
                    for (auto frame : backdrops) {
                        if (frame && sprite->isFrameDisplayed(frame)) sprite->setVisible(false);
                    }
                }
            }

            // What it held, in a row under it, popping out one after another.
            float k = m_k;
            float gap = 6 * k, between = 14 * k, h = REWARD_ICON * k;
            std::vector<std::pair<CCNode*, float>> entries;
            if (!slot.item->m_rewardObjects) return;
            for (auto object : CCArrayExt<GJRewardObject*>(slot.item->m_rewardObjects)) {
                auto type = object->m_specialRewardItem;
                CCNode* icon = nullptr;
                if (type == SpecialRewardItem::CustomItem) icon = GJItemIcon::createBrowserItem(object->m_unlockType, object->m_itemID);
                else if (auto frame = rewardFrame(type)) icon = CCSprite::createWithSpriteFrameName(frame);
                if (!icon) continue;
                fitInto(icon, h);
                auto entry = CCNode::create();
                float w = h;
                icon->setPosition({w / 2, h / 2});
                entry->addChild(icon);
                if (type != SpecialRewardItem::CustomItem || object->m_total > 1) {
                    auto count = makeText(std::to_string(object->m_total), Weight::Bold, 18 * k);
                    count->setAnchorPoint({0, 0.5f});
                    count->setPosition({w + gap, h / 2});
                    entry->addChild(count);
                    w += gap + count->getContentSize().width * count->getScale();
                }
                entry->setContentSize({w, h});
                entry->setAnchorPoint({0.5f, 0.5f});
                entry->setVisible(false);
                slot.bg->addChild(entry, 3);
                entries.push_back({entry, w});
                Reward reward;
                reward.node = entry;
                reward.at = REWARD_STAGGER_S * static_cast<float>(slot.rewards.size());
                reward.sound = rewardSound(type);
                slot.rewards.push_back(reward);
            }
            float total = 0;
            for (auto& [node, w] : entries) total += w;
            total += between * std::max<float>(0, entries.size() - 1);
            // A long haul shrinks to fit the slot.
            float fit = std::min(1.f, (SLOT_W - 16) * k / std::max(total, 1.f));
            float x = (SLOT_W * k - total * fit) / 2;
            for (auto& [node, w] : entries) {
                node->setPosition({x + w * fit / 2, slot.rewardsY});
                node->setScale(0);
                x += (w + between) * fit;
            }
            slot.rewardsFit = fit;
        }

        float m_k = 1;
        float m_time = 0;
        float m_tick = 0;
        bool m_started = false;
        bool m_freshPage = false;
        Slot m_slots[2];

        static inline RewardsPanel* s_panel = nullptr;
    };
}

void showRewards() {
    if (Dialog::isOpen()) return;
    auto panel = RewardsPanel::create();
    if (!panel) return;
    Dialog::Content content;
    content.icon = icon::GIFT;
    content.header = "daily chests";
    content.body = "a small one every four hours, a large one every day";
    content.panel = panel;
    content.buttons = {{"close", Dialog::Kind::Cancel, nullptr}};
    Dialog::show(std::move(content));
}

} // namespace lazer

// Our hidden RewardsPage must never grab touches (FLAlertLayer registers at a
// very high priority and swallows everything).
class $modify(LazerHiddenRewardsPage, RewardsPage) {
    void show() {
        if (!this->getUserObject("hidden"_spr) && Mod::get()->getSettingValue<bool>("enabled")
            && Mod::get()->getSettingValue<bool>("restyle-popups")) {
            lazer::showRewards();
            return;
        }
        RewardsPage::show();
    }

    void registerWithTouchDispatcher() {
        if (this->getUserObject("hidden"_spr)) return;
        RewardsPage::registerWithTouchDispatcher();
    }
};

// The unlock layer GD makes for a chest opened from our hidden page: GD needs
// it to hand the reward over, but nobody sees it. Its own show (the drop, the
// lid, the reward flying out, the close button) is stopped before it starts;
// the popup plays that itself.
class $modify(LazerRewardUnlockLayer, RewardUnlockLayer) {
    bool init(int type, RewardsPage* page) {
        if (!RewardUnlockLayer::init(type, page)) return false;
        if (page && page->getUserObject("hidden"_spr)) {
            this->stopAllActions();
            this->setVisible(false);
            this->setTouchEnabled(false);
            this->setKeypadEnabled(false);
        }
        return true;
    }

    bool showCollectReward(GJRewardItem* item) {
        bool ok = RewardUnlockLayer::showCollectReward(item);
        if (!m_rewardsPage || !m_rewardsPage->getUserObject("hidden"_spr)) return ok;
        if (ok) lazer::RewardsPanel::collected(m_chestType, item);
        // Its job is done (GD counted the reward in before calling this):
        // close it so the other chest can have its turn. Not from inside
        // its own method: next frame.
        Loader::get()->queueInMainThread([self = Ref(static_cast<RewardUnlockLayer*>(this))] {
            self->onClose(nullptr);
        });
        return ok;
    }
};
