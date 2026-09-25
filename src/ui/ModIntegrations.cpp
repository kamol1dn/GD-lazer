#include "ModIntegrations.hpp"

#include "Text.hpp"

#include <Geode/Geode.hpp>
#include <cmath>

using namespace geode::prelude;

namespace lazer::integrations {

namespace {
    constexpr auto DUAL_ICONS = "weebify.separate_dual_icons";
    constexpr auto PROGRESSION = "itzkiba.better_progression";

    Mod* loaded(char const* id) {
        return Loader::get()->getLoadedMod(id);
    }

    // Better Progression's LevelHelper.
    long long expForLevel(long long level) { return 50 * level * level + 50 * level; }
    int levelForExp(long long exp) { return int((std::sqrt(2.0 * exp + 25) - 5) / 10.0); }
}

CCNode* playerIcon(bool player2, float size) {
    auto gm = GameManager::get();
    int frame, color1, color2, glowColor;
    bool glow;
    if (player2) {
        auto mod = loaded(DUAL_ICONS);
        if (!mod) return nullptr;
        frame = int(mod->getSavedValue<int64_t>("cube", 1));
        color1 = int(mod->getSavedValue<int64_t>("color1", 0));
        color2 = int(mod->getSavedValue<int64_t>("color2", 0));
        glowColor = int(mod->getSavedValue<int64_t>("colorglow", 0));
        glow = mod->getSavedValue<bool>("glow", false);
    } else {
        frame = gm->getPlayerFrame();
        color1 = gm->getPlayerColor();
        color2 = gm->getPlayerColor2();
        glowColor = gm->getPlayerGlowColor();
        glow = gm->getPlayerGlow();
    }

    auto player = SimplePlayer::create(1);
    player->updatePlayerFrame(std::max(1, frame), IconType::Cube);
    player->setColors(gm->colorForIdx(color1), gm->colorForIdx(color2));
    if (glow) player->setGlowOutline(gm->colorForIdx(glowColor));
    else player->disableGlowOutline();
    // A cube is ~30 units tall at scale 1.
    player->setScale(size / 30.f);
    return player;
}

std::optional<PlayerLook> player2Look() {
    auto mod = loaded(DUAL_ICONS);
    if (!mod) return std::nullopt;
    auto get = [mod](char const* key, int fallback) { return int(mod->getSavedValue<int64_t>(key, fallback)); };
    // Its save keys use GD's old names: roll = ball, bird = ufo, dart = wave.
    return PlayerLook {
        get("cube", 1), get("ship", 1), get("roll", 1), get("bird", 1), get("dart", 1),
        get("robot", 1), get("spider", 1), get("swing", 1), get("jetpack", 1),
        get("color1", 0), get("color2", 0), get("colorglow", 0), mod->getSavedValue<bool>("glow", false),
    };
}

std::optional<Progression> betterProgression() {
    auto mod = loaded(PROGRESSION);
    if (!mod) return std::nullopt;
    long long exp = std::max<int64_t>(0, mod->getSavedValue<int64_t>("total-exp", 0));
    int level = levelForExp(exp);
    return Progression {level, exp, expForLevel(level), expForLevel(level + 1)};
}

std::optional<Progression> betterProgression(GJUserScore* score) {
    if (!score || !loaded(PROGRESSION)) return std::nullopt;
    // LevelHelper's EXP per stat.
    long long exp = 5LL * score->m_stars + 5LL * score->m_moons + 2LL * score->m_diamonds
        + 100LL * score->m_secretCoins + 20LL * score->m_userCoins + 75LL * score->m_demons
        + 5000LL * score->m_creatorPoints;
    int level = levelForExp(exp);
    return Progression {level, exp, expForLevel(level), expForLevel(level + 1)};
}

CCNode* progressionBadge(int level, float size) {
    if (!loaded(PROGRESSION)) return nullptr;
    // Its art: a new tier every 25 levels, a new shade every 5, one badge from 300.
    std::string frame = level >= 300
        ? fmt::format("{}/tier12.png", PROGRESSION)
        : fmt::format("{}/tier{}_{}.png", PROGRESSION, level / 25, (level % 25) / 5);
    if (!CCSpriteFrameCache::sharedSpriteFrameCache()->spriteFrameByName(frame.c_str())) return nullptr;

    auto badge = CCSprite::createWithSpriteFrameName(frame.c_str());
    auto s = badge->getContentSize();
    float side = std::max(s.width, s.height);
    if (side > 0) badge->setScale(size / side);

    auto number = makeText(fmt::format("{}", level), Weight::Bold, s.height * (level >= 100 ? 0.34f : 0.42f));
    number->setPosition({s.width / 2, s.height / 2 + s.height * 0.02f});
    badge->addChild(number);
    return badge;
}

} // namespace lazer::integrations
