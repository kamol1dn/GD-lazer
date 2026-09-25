#include "StatsOverlay.hpp"

#include "../core/Text.hpp"

#include <algorithm>
#include <cmath>

using namespace geode::prelude;

namespace lazer {

namespace {
    // osu!'s Blue overlay scheme.
    constexpr theme::Scheme SCHEME {200};

    constexpr float HORIZONTAL_PADDING = 50.f; // WaveOverlayContainer
    constexpr float ROLL_UP_MS = 1400.f;

    // Colours for difficulty bars, easy -> hard.
    constexpr ccColor4B AUTO {255, 196, 90, 255};
    constexpr ccColor4B EASY {102, 204, 255, 255};
    constexpr ccColor4B NORMAL {136, 221, 85, 255};
    constexpr ccColor4B HARD {255, 204, 34, 255};
    constexpr ccColor4B HARDER {255, 136, 68, 255};
    constexpr ccColor4B INSANE {255, 102, 170, 255};
    constexpr ccColor4B DEMON {170, 102, 255, 255};
    constexpr ccColor4B EXTREME {255, 68, 102, 255};

    int stat(StatKey key) {
        return GameStatsManager::sharedState()->getStat(fmt::format("{}", static_cast<int>(key)).c_str());
    }

    // 1164220 -> "1,164,220"
    std::string withCommas(double value, int decimals) {
        if (decimals > 0) return fmt::format("{:.{}f}", value, decimals);
        auto s = fmt::format("{}", static_cast<long long>(std::llround(value)));
        int insertAt = int(s.size()) - 3;
        int stop = s[0] == '-' ? 1 : 0;
        for (; insertAt > stop; insertAt -= 3) s.insert(size_t(insertAt), ",");
        return s;
    }
}

StatsOverlay* StatsOverlay::create(float topInset) {
    auto ret = new StatsOverlay();
    if (ret->init(topInset)) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

bool StatsOverlay::init(float topInset) {
    if (!WaveOverlay::init(topInset, SCHEME, icon::CHART, "statistics", "everything you've done, in numbers")) return false;
    m_pad = HORIZONTAL_PADDING * m_k;
    m_gap = 10 * m_k;
    m_scroll = ScrollArea::create(bodySize());
    body()->addChild(m_scroll);
    build();
    return true;
}

void StatsOverlay::onOpened() {
    // Stats change as you play; rebuild and roll the numbers up again.
    build();
    m_rollUp.set(0.f);
    m_rollUp.to(1.f, ROLL_UP_MS, Easing::OutQuint);
}

void StatsOverlay::build() {
    m_scroll->content()->removeAllChildren();
    m_counters.clear();
    m_bars.clear();

    float y = 26 * m_k;
    y = addHeroRow({
        {"stars", double(stat(StatKey::Stars)), "GJ_starsIcon_001.png"},
        {"moons", double(stat(StatKey::Moons)), "GJ_moonsIcon_001.png"},
        {"diamonds", double(stat(StatKey::Diamonds)), "GJ_diamondsIcon_001.png"},
        {"secret coins", double(stat(StatKey::Coins)), "GJ_coinsIcon_001.png"},
        {"user coins", double(stat(StatKey::UserCoins)), "GJ_coinsIcon2_001.png"},
        {"demons", double(stat(StatKey::Demons)), "GJ_demonIcon_001.png"},
    }, y);

    y = addSection("levels", y);
    y = addTileGrid({
        {"completed levels", double(stat(StatKey::CompletedOfficialLevels)), nullptr, icon::PLAY},
        {"online levels", double(stat(StatKey::CustomLevels)), nullptr, icon::GLOBE},
        {"daily levels", double(stat(StatKey::DailyLevels)), nullptr, icon::STAR},
        {"insane levels", double(stat(StatKey::Insanes)), nullptr, icon::SHIELD},
        {"map packs", double(stat(StatKey::MapPacks)), nullptr, icon::BOX_OPEN},
        {"gauntlets", double(stat(StatKey::Gauntlets)), nullptr, icon::GEM},
        {"list rewards", double(stat(StatKey::ListsRewards)), nullptr, icon::MEDAL},
        {"orbs collected", double(stat(StatKey::Orbs)), nullptr, icon::COINS},
    }, 4, y);

    int jumps = stat(StatKey::Jumps), attempts = stat(StatKey::Attempts);
    y = addSection("activity", y);
    y = addTileGrid({
        {"jumps", double(jumps), nullptr, icon::GAMEPAD},
        {"attempts", double(attempts), nullptr, icon::GEARS},
        {"jumps per attempt", attempts > 0 ? double(jumps) / attempts : 0.0, nullptr, icon::CHART, 1},
        {"players destroyed", double(stat(StatKey::DestroyedPlayers)), nullptr, icon::XMARK},
        {"liked / disliked", double(stat(StatKey::LikedLevels)), nullptr, icon::USERS},
        {"rated levels", double(stat(StatKey::RatedLevels)), nullptr, icon::SLIDERS},
    }, 3, y);

    // How completed (saved) levels split by difficulty.
    std::array<int, 7> byStars {};  // auto, easy, normal, hard, harder, insane, demon
    std::array<int, 5> byDemon {};  // easy, medium, hard, insane, extreme
    if (auto levels = GameLevelManager::sharedState()->m_onlineLevels) {
        for (auto [key, object] : CCDictionaryExt<gd::string, CCObject*>(levels)) {
            auto level = typeinfo_cast<GJGameLevel*>(object);
            if (!level || level->m_normalPercent.value() < 100) continue;
            int stars = level->m_stars.value();
            if (stars <= 0) continue;
            int bucket = level->m_autoLevel || stars == 1 ? 0
                : stars == 2 ? 1 : stars == 3 ? 2 : stars <= 5 ? 3 : stars <= 7 ? 4 : stars <= 9 ? 5 : 6;
            byStars[bucket]++;
            if (bucket == 6) {
                switch (level->m_demonDifficulty) {
                    case 3: byDemon[0]++; break;
                    case 4: byDemon[1]++; break;
                    case 5: byDemon[3]++; break;
                    case 6: byDemon[4]++; break;
                    default: byDemon[2]++; break;
                }
            }
        }
    }
    y = addSection("difficulty", y);
    auto note = makeText("rated levels you've completed, from the levels saved on this device", Weight::Regular, 14 * m_k);
    note->setColor(theme::rgb(m_scheme.content2()));
    note->setAnchorPoint({0, 1});
    note->setPosition({m_pad, -y});
    m_scroll->content()->addChild(note);
    y += note->getScaledContentSize().height + 14 * m_k;

    float columnW = (bodySize().width - m_pad * 2 - 40 * m_k) / 2;
    float y1 = addBarChart("rated levels", {
        {"auto", byStars[0], AUTO}, {"easy", byStars[1], EASY}, {"normal", byStars[2], NORMAL},
        {"hard", byStars[3], HARD}, {"harder", byStars[4], HARDER}, {"insane", byStars[5], INSANE},
        {"demon", byStars[6], DEMON},
    }, m_pad, columnW, y);
    float y2 = addBarChart("demons", {
        {"easy", byDemon[0], EASY}, {"medium", byDemon[1], NORMAL}, {"hard", byDemon[2], HARD},
        {"insane", byDemon[3], INSANE}, {"extreme", byDemon[4], EXTREME},
    }, m_pad + columnW + 40 * m_k, columnW, y);
    y = std::max(y1, y2);

    y = addSection("shards & keys", y);
    y = addTileGrid({
        {"fire", double(stat(StatKey::FireShards)), "fireShardBig_001.png"},
        {"ice", double(stat(StatKey::IceShards)), "iceShardBig_001.png"},
        {"poison", double(stat(StatKey::PoisonShards)), "poisonShardBig_001.png"},
        {"shadow", double(stat(StatKey::ShadowShards)), "shadowShardBig_001.png"},
        {"lava", double(stat(StatKey::LavaShards)), "lavaShardBig_001.png"},
        {"demon keys", double(stat(StatKey::Keys)), "GJ_bigKey_001.png"},
        {"earth", double(stat(StatKey::EarthShards)), "shard0201ShardBig_001.png"},
        {"blood", double(stat(StatKey::BloodShards)), "shard0202ShardBig_001.png"},
        {"metal", double(stat(StatKey::MetalShards)), "shard0203ShardBig_001.png"},
        {"light", double(stat(StatKey::LightShards)), "shard0204ShardBig_001.png"},
        {"soul", double(stat(StatKey::SoulShards)), "shard0205ShardBig_001.png"},
        {"diamond shards", double(stat(StatKey::DiamondShards)), "GJ_bigDiamond_001.png"},
    }, 6, y);

    m_scroll->setContentHeight(y + 30 * m_k);
    m_scroll->scrollTo(0, false);
    m_rollUp.set(1.f);
}

// --- pieces ---

float StatsOverlay::addSection(std::string const& title, float y) {
    y += 18 * m_k;
    auto label = makeText(title, Weight::SemiBold, 22 * m_k);
    label->setAnchorPoint({0, 1});
    label->setPosition({m_pad, -y});
    m_scroll->content()->addChild(label);
    return y + label->getScaledContentSize().height + 12 * m_k;
}

CCNode* StatsOverlay::makeTileIcon(Tile const& tile, float size) {
    CCNode* node = nullptr;
    if (tile.sprite && CCSpriteFrameCache::sharedSpriteFrameCache()->spriteFrameByName(tile.sprite)) {
        node = CCSprite::createWithSpriteFrameName(tile.sprite);
    }
    if (!node && tile.glyph) {
        auto glyph = makeIcon(tile.glyph, size * 0.8f);
        glyph->setColor(theme::rgb(m_scheme.highlight1()));
        return glyph;
    }
    if (!node) return nullptr;
    auto s = node->getContentSize();
    float side = std::max(s.width, s.height);
    if (side > 0) node->setScale(size / side);
    return node;
}

float StatsOverlay::addHeroRow(std::vector<Tile> const& tiles, float y) {
    int n = int(tiles.size());
    float width = (bodySize().width - m_pad * 2 - m_gap * (n - 1)) / n;
    float height = 124 * m_k;
    for (int i = 0; i < n; i++) {
        auto const& t = tiles[i];
        auto box = RoundedBox::create({width, height}, 10 * m_k, m_scheme.background4());
        box->setShadow(10 * m_k, {0, 0, 0, 60});
        box->setAnchorPoint({0, 1});
        box->setPosition({m_pad + i * (width + m_gap), -y});
        m_scroll->content()->addChild(box);

        if (auto icon = makeTileIcon(t, 34 * m_k)) {
            icon->setPosition({width / 2, height - 30 * m_k});
            box->addChild(icon);
        }
        auto value = makeText("0", Weight::Bold, 30 * m_k);
        value->setPosition({width / 2, height / 2 - 12 * m_k});
        box->addChild(value);
        m_counters.push_back({value, t.value, t.decimals});

        auto label = makeText(t.label, Weight::Regular, 15 * m_k);
        label->setColor(theme::rgb(m_scheme.content2()));
        label->setPosition({width / 2, 18 * m_k});
        box->addChild(label);
    }
    return y + height + 10 * m_k;
}

float StatsOverlay::addTileGrid(std::vector<Tile> const& tiles, int columns, float y) {
    float width = (bodySize().width - m_pad * 2 - m_gap * (columns - 1)) / columns;
    float height = 70 * m_k;
    for (size_t i = 0; i < tiles.size(); i++) {
        auto const& t = tiles[i];
        int row = int(i) / columns, col = int(i) % columns;
        auto box = RoundedBox::create({width, height}, 8 * m_k, m_scheme.background4());
        box->setAnchorPoint({0, 1});
        box->setPosition({m_pad + col * (width + m_gap), -(y + row * (height + m_gap))});
        m_scroll->content()->addChild(box);

        float textX = 16 * m_k;
        if (auto icon = makeTileIcon(t, 32 * m_k)) {
            icon->setPosition({16 * m_k + 16 * m_k, height / 2});
            box->addChild(icon);
            textX = 16 * m_k + 32 * m_k + 14 * m_k;
        }
        auto label = makeText(t.label, Weight::Regular, 14 * m_k);
        label->setColor(theme::rgb(m_scheme.content2()));
        label->setAnchorPoint({0, 0});
        label->setPosition({textX, height / 2 + 2 * m_k});
        box->addChild(label);

        auto value = makeText("0", Weight::Bold, 22 * m_k);
        value->setAnchorPoint({0, 1});
        value->setPosition({textX, height / 2 + 2 * m_k});
        box->addChild(value);
        m_counters.push_back({value, t.value, t.decimals});
    }
    int rows = (int(tiles.size()) + columns - 1) / columns;
    return y + rows * (height + m_gap);
}

float StatsOverlay::addBarChart(std::string const& title, std::vector<BarDef> const& bars, float x, float width,
                                float y) {
    auto heading = makeText(title, Weight::SemiBold, 16 * m_k);
    heading->setAnchorPoint({0, 1});
    heading->setPosition({x, -y});
    m_scroll->content()->addChild(heading);
    y += heading->getScaledContentSize().height + 10 * m_k;

    int maxValue = 1;
    for (auto const& b : bars) maxValue = std::max(maxValue, b.value);

    float labelW = 70 * m_k, countW = 50 * m_k;
    float barW = width - labelW - countW - 16 * m_k;
    float rowH = 26 * m_k, barH = 12 * m_k;
    for (auto const& b : bars) {
        float cy = -(y + rowH / 2);
        auto label = makeText(b.label, Weight::Regular, 15 * m_k);
        label->setColor(theme::rgb(m_scheme.content2()));
        label->setAnchorPoint({0, 0.5f});
        label->setPosition({x, cy});
        m_scroll->content()->addChild(label);

        auto track = RoundedBox::create({barW, barH}, barH / 2, m_scheme.background6());
        track->setAnchorPoint({0, 0.5f});
        track->setPosition({x + labelW, cy});
        m_scroll->content()->addChild(track);

        auto fill = RoundedBox::create({0, barH}, barH / 2, b.color);
        fill->setAnchorPoint({0, 0.5f});
        fill->setPosition({x + labelW, cy});
        m_scroll->content()->addChild(fill);

        auto count = makeText("0", Weight::SemiBold, 15 * m_k);
        count->setAnchorPoint({1, 0.5f});
        count->setPosition({x + width, cy});
        m_scroll->content()->addChild(count);

        m_bars.push_back({fill, count, barW, float(b.value) / maxValue, b.value});
        y += rowH;
    }
    return y + 6 * m_k;
}

// --- per frame ---

void StatsOverlay::onUpdate(float dt) {
    m_rollUp.update(dt);
    float t = m_rollUp.get();
    for (auto const& c : m_counters) {
        c.label->setString(withCommas(c.target * t, c.decimals).c_str());
    }
    for (auto const& b : m_bars) {
        float w = b.fullWidth * b.fraction * t;
        b.fill->setVisible(w > 0.5f);
        b.fill->setContentSize({std::max(w, b.fill->getContentSize().height), b.fill->getContentSize().height});
        b.count->setString(withCommas(b.value * t, 0).c_str());
    }
}

bool StatsOverlay::ccTouchBegan(CCTouch* touch, CCEvent* e) {
    if (!WaveOverlay::ccTouchBegan(touch, e)) return false;
    m_drag.began(m_scroll, touch->getLocation());
    return true;
}

void StatsOverlay::ccTouchMoved(CCTouch* touch, CCEvent*) {
    m_drag.moved(touch->getLocation());
}

void StatsOverlay::ccTouchEnded(CCTouch* touch, CCEvent* e) {
    WaveOverlay::ccTouchEnded(touch, e);
    m_drag.ended();
}

} // namespace lazer
