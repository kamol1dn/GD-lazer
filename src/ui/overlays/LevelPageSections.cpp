#include "LevelPage.hpp"

#include "../../levels/LevelLibrary.hpp"
#include "../core/Text.hpp"
#include "LevelFacts.hpp"
#include "LevelPageInternal.hpp"

#include <Geode/Geode.hpp>
#include <algorithm>
#include <cctype>

using namespace geode::prelude;

namespace lazer {

using namespace levelpage;

// --- under the band: the description, details, your progress, the scores ---

namespace {
    constexpr float BOTTOM_PADDING = 30.f;
    constexpr int SCORES_SHOWN = 25;
    constexpr ccColor3B COIN_ICON {255, 200, 60};

    std::string trim(std::string s) {
        auto space = [](char c) { return std::isspace(static_cast<unsigned char>(c)) != 0; };
        while (!s.empty() && space(s.back())) s.pop_back();
        size_t start = 0;
        while (start < s.size() && space(s[start])) start++;
        return s.substr(start);
    }

    // The song's title and who made it: GD's own track, or the custom song
    // (its name once GD knows it).
    std::pair<std::string, std::string> songOf(GJGameLevel* level) {
        if (level->m_songID > 0) {
            auto info = MusicDownloadManager::sharedState()->getSongInfoObject(level->m_songID);
            if (info && !info->m_songName.empty()) return {info->m_songName, info->m_artistName};
            return {fmt::format("Song {}", level->m_songID), ""};
        }
        int track = level->m_audioTrack;
        return {LevelTools::getAudioTitle(track), LevelTools::nameForArtist(LevelTools::artistForAudio(track))};
    }
}

void LevelPage::rebuildSections() {
    float k = m_k, W = bodySize().width;
    m_sections->removeAllChildren();
    m_sectionButtons.clear();
    m_spinner = nullptr;
    m_pressed = nullptr;
    m_sections->setPosition({0, -m_heroHeight});

    // Two columns: the description and your progress at the left, the
    // details at the right; then the scores across.
    float x = m_pad, avail = W - 2 * m_pad;
    float leftW = std::floor((avail - COLUMN_GAP * k) * 0.58f), rightX = x + leftW + COLUMN_GAP * k, rightW = avail - leftW - COLUMN_GAP * k;
    float y = SECTION_GAP * k;
    float leftY = buildDescription(y, x, leftW);
    leftY = buildProgress(leftY + SECTION_GAP * k, x, leftW);
    float rightY = buildDetails(y, rightX, rightW);
    y = std::max(leftY, rightY) + SECTION_GAP * k;
    y = buildScores(y);
    m_sectionsHeight = y + BOTTOM_PADDING * k;
    m_scroll->setContentHeight(m_heroHeight + m_sectionsHeight);
    m_scroll->claimWheel();
}

namespace {
    // A section's name, like osu!'s overlay headers.
    CCLabelBMFont* sectionTitle(CCNode* parent, std::string const& text, float x, float y, float k, theme::Scheme const& scheme) {
        auto label = makeText(text, Weight::SemiBold, 13 * k);
        label->setColor(theme::rgb(scheme.light1()));
        label->setAnchorPoint({0, 0.5f});
        label->setPosition({x, -(y + 8 * k)});
        parent->addChild(label);
        return label;
    }
}

float LevelPage::buildDescription(float y, float x, float w) {
    float k = m_k;
    sectionTitle(m_sections, "description", x, y, k, m_scheme);
    y += 26 * k;
    // As GD's page shows it (with its words for none).
    std::string desc = trim(std::string(m_level->getUnpackedLevelDescription()));
    bool none = desc.empty();
    if (none) desc = "(No description provided)";
    auto text = makeWrappedText(desc, 14 * k, w, theme::rgb(none ? m_scheme.foreground1() : m_scheme.content2()));
    text->setPosition({x, -y});
    m_sections->addChild(text);
    return y + text->getContentSize().height;
}

float LevelPage::buildDetails(float y, float x, float w) {
    float k = m_k;
    auto level = m_level.data();
    sectionTitle(m_sections, "details", x, y, k, m_scheme);
    y += 26 * k;
    float labelW = 88 * k, rowH = 24 * k;
    auto muted = theme::rgb(m_scheme.foreground1());
    auto row = [&](std::string const& name, std::string const& value) -> CCLabelBMFont* {
        auto nameLabel = makeText(name, Weight::SemiBold, 12 * k);
        nameLabel->setColor(muted);
        nameLabel->setAnchorPoint({0, 0.5f});
        nameLabel->setPosition({x, -(y + rowH / 2)});
        m_sections->addChild(nameLabel);
        auto valueLabel = makeText(value, Weight::Regular, 13 * k);
        valueLabel->setColor(theme::rgb(m_scheme.content2()));
        valueLabel->setAnchorPoint({0, 0.5f});
        valueLabel->setPosition({x + labelW, -(y + rowH / 2)});
        facts::fit(valueLabel, w - labelW);
        m_sections->addChild(valueLabel);
        y += rowH;
        return valueLabel;
    };

    // The creator, as a button to their profile (GD's own page).
    {
        std::string creator = level->m_creatorName;
        if (creator.empty()) creator = "unknown";
        auto nameLabel = makeText("creator", Weight::SemiBold, 12 * k);
        nameLabel->setColor(muted);
        nameLabel->setAnchorPoint({0, 0.5f});
        nameLabel->setPosition({x, -(y + rowH / 2)});
        m_sections->addChild(nameLabel);
        auto& button = addButton(m_sectionButtons, m_sections, Button::Kind::Tab, icon::USER, creator, TAB_HEIGHT * k,
                                 {x + labelW - 12 * k, -(y + rowH / 2)}, {0, 0.5f}, [this] {
            if (m_leaving || pressGD("right-side-menu", "creator-name")) return;
            // Profiles are addressed by account ID, not the level's player ID.
            int account = m_level->m_accountID.value();
            if (account <= 0) {
                account = GameLevelManager::sharedState()->accountIDForUserID(m_level->m_userID.value());
            }
            if (account > 0) {
                if (auto page = ProfilePage::create(account, false)) page->show();
            } else {
                FLAlertLayer::create("Player profile", "This creator has no linked account profile.", "OK")->show();
            }
        });
        button.textColor = theme::rgb(m_scheme.content2());
        y += rowH;
    }
    auto [songTitle, songArtist] = songOf(level);
    row("song", songArtist.empty() ? songTitle : songTitle + "  -  " + songArtist);
    if (level->m_songID > 0) row("song ID", std::to_string(level->m_songID));
    row("length", facts::lengthName(level));
    int objects = level->m_objectCount.value();
    if (objects > 0) row("objects", facts::withCommas(objects));
    row("version", std::to_string(std::max(1, level->m_levelVersion)));
    if (level->m_gameVersion > 0) {
        // GD stores "22" for 2.2, "21" for 2.1 and so on (older ones differ, GD shows them the same way).
        int v = level->m_gameVersion;
        row("made in", v >= 10 ? fmt::format("{}.{}", v / 10, v % 10) : fmt::format("1.{}", v));
    }
    std::string uploaded = level->m_uploadDate, updated = level->m_updateDate;
    if (!uploaded.empty()) row("uploaded", uploaded + " ago");
    if (!updated.empty() && updated != uploaded) row("updated", updated + " ago");
    if (level->m_password.value() != 0) row("copyable", level->m_password.value() == 1 ? "free copy" : "with a password");
    if (level->m_dailyID.value() > 0) row("was", level->m_dailyID.value() > 200000 ? "a weekly level" : "a daily level");
    return y;
}

float LevelPage::buildProgress(float y, float x, float w) {
    float k = m_k;
    auto level = m_level.data();
    sectionTitle(m_sections, "your progress", x, y, k, m_scheme);
    y += 26 * k;
    float barW = std::min(w, 360 * k);

    // Normal and practice as bars; a platformer is beaten or not, with your best time.
    auto bar = [&](std::string const& name, int percent, ccColor4B colour) {
        auto label = makeText(name, Weight::SemiBold, 12 * k);
        label->setColor(theme::rgb(m_scheme.foreground1()));
        label->setAnchorPoint({0, 0.5f});
        label->setPosition({x, -(y + 7 * k)});
        m_sections->addChild(label);
        auto value = makeText(fmt::format("{}%", percent), Weight::SemiBold, 13 * k);
        value->setAnchorPoint({1, 0.5f});
        value->setPosition({x + barW, -(y + 7 * k)});
        m_sections->addChild(value);
        y += 18 * k;
        auto track = RoundedBox::create({barW, 8 * k}, 4 * k, m_scheme.background3());
        track->setAnchorPoint({0, 0.5f});
        track->setPosition({x, -(y + 4 * k)});
        m_sections->addChild(track);
        if (percent > 0) {
            auto fill = RoundedBox::create({barW * std::clamp(percent, 0, 100) / 100.f, 8 * k}, 4 * k, colour);
            fill->setAnchorPoint({0, 0.5f});
            fill->setPosition({x, -(y + 4 * k)});
            m_sections->addChild(fill, 1);
        }
        y += 18 * k;
    };
    if (level->isPlatformer()) {
        bool beaten = level->m_normalPercent.value() >= 100;
        std::vector<facts::Stat> best;
        best.push_back({beaten ? icon::CHECK : icon::XMARK, beaten ? "completed" : "not completed yet"});
        if (level->m_bestTime > 0) best.push_back({icon::CLOCK, "best " + facts::formatTime(level->m_bestTime)});
        auto row = facts::statsRow(best, 13 * k, theme::rgb(beaten ? m_scheme.highlight1() : m_scheme.foreground1()),
                                   theme::rgb(m_scheme.content2()));
        row->setPosition({x, -(y + 8 * k)});
        m_sections->addChild(row);
        y += 22 * k;
    } else {
        bar("normal", level->m_normalPercent.value(), m_scheme.colour3());
        bar("practice", level->m_practicePercent, m_scheme.light4());
    }
    // Attempts, jumps and orbs, as GD's page counts them.
    std::vector<facts::Stat> counts;
    if (level->m_attempts.value() > 0) counts.push_back({icon::ROTATE, facts::withCommas(level->m_attempts.value()) + " attempts"});
    if (level->m_jumps.value() > 0) counts.push_back({icon::ARROW_UP, facts::withCommas(level->m_jumps.value()) + " jumps"});
    if (level->m_orbCompletion.value() > 0) counts.push_back({icon::GEM, fmt::format("{}% of the orbs", level->m_orbCompletion.value())});
    if (counts.empty()) counts.push_back({nullptr, "not played yet"});
    auto row = facts::statsRow(counts, 12 * k, theme::rgb(m_scheme.light2()), theme::rgb(m_scheme.content2()));
    row->setPosition({x, -(y + 8 * k)});
    m_sections->addChild(row);
    return y + 18 * k;
}

float LevelPage::buildScores(float y) {
    float k = m_k, W = bodySize().width, x = m_pad, w = W - 2 * m_pad;
    sectionTitle(m_sections, "scores", x, y, k, m_scheme);
    // Which board, at the right of the title.
    if (m_board != Board::Hidden) {
        struct Tab { char const* label; LevelLeaderboardType type; };
        Tab const tabs[] = {{"top", LevelLeaderboardType::Global}, {"this week", LevelLeaderboardType::Weekly}, {"friends", LevelLeaderboardType::Friends}};
        float tx = x + w;
        auto& refresh = addButton(m_sectionButtons, m_sections, Button::Kind::Tab, icon::ROTATE, "refresh", TAB_HEIGHT * k,
                                  {tx, -(y + 8 * k)}, {1, 0.5f}, [this] { this->loadScores(m_boardType); });
        tx -= refresh.node->getContentSize().width + 14 * k;
        for (int i = 2; i >= 0; i--) {
            auto type = tabs[i].type;
            auto& tab = addButton(m_sectionButtons, m_sections, Button::Kind::Tab, nullptr, tabs[i].label, TAB_HEIGHT * k,
                                  {tx, -(y + 8 * k)}, {1, 0.5f}, [this, type] {
                if (m_boardType != type || m_board == Board::Failed) this->loadScores(type);
            });
            tab.active = [this, type] { return m_boardType == type; };
            tx -= tab.node->getContentSize().width + 4 * k;
        }
    }
    y += 30 * k;

    switch (m_board) {
        case Board::Hidden: {
            // Asking GD for them uploads your best, so only on request (GD's
            // own page does the same with its leaderboard button).
            auto& show = addButton(m_sectionButtons, m_sections, Button::Kind::Tab, icon::RANKING_STAR,
                                   "show the scores (this also uploads your progress)", TAB_HEIGHT * k,
                                   {x - 12 * k, -(y + TAB_HEIGHT * k / 2)}, {0, 0.5f}, [this] { this->loadScores(m_boardType); });
            show.bg->setFillColor(m_scheme.background4());
            show.color = m_scheme.background4();
            return y + TAB_HEIGHT * k;
        }
        case Board::Loading: {
            m_spinner = facts::makeSpinner(22 * k, theme::rgb(m_scheme.content2()));
            m_spinner->setPosition({x + 12 * k, -(y + 14 * k)});
            m_sections->addChild(m_spinner);
            auto label = makeText("loading the scores...", Weight::Regular, 13 * k);
            label->setColor(theme::rgb(m_scheme.content2()));
            label->setAnchorPoint({0, 0.5f});
            label->setPosition({x + 34 * k, -(y + 14 * k)});
            m_sections->addChild(label);
            return y + 28 * k;
        }
        case Board::Failed: {
            auto label = makeText("Couldn't load the scores. Try again in a moment.", Weight::Regular, 13 * k);
            label->setColor(theme::rgb(m_scheme.content2()));
            label->setAnchorPoint({0, 0.5f});
            label->setPosition({x, -(y + 12 * k)});
            m_sections->addChild(label);
            return y + 24 * k;
        }
        case Board::Loaded:
            break;
    }

    if (!m_scores || m_scores->count() == 0) {
        auto label = makeText(m_boardType == LevelLeaderboardType::Friends ? "None of your friends have played it yet." : "No scores yet.",
                              Weight::Regular, 13 * k);
        label->setColor(theme::rgb(m_scheme.content2()));
        label->setAnchorPoint({0, 0.5f});
        label->setPosition({x, -(y + 12 * k)});
        m_sections->addChild(label);
        return y + 24 * k;
    }

    // The table: rank, icon and name, the percent (a platformer's time), coins.
    // Yours is lit (osu!'s own-score row).
    float rowH = 32 * k;
    int me = GJAccountManager::get()->m_accountID;
    bool platformer = m_level->isPlatformer();
    int shown = 0;
    for (auto score : CCArrayExt<GJUserScore*>(m_scores)) {
        if (shown >= SCORES_SHOWN) break;
        bool mine = me > 0 && score->m_accountID == me;
        float cy = -(y + rowH / 2);
        if (mine || shown % 2 == 0) {
            auto stripe = RoundedBox::create({w + 16 * k, rowH - 2 * k}, 6 * k, mine ? theme::lerp(m_scheme.colour3(), m_scheme.background5(), 0.6f) : m_scheme.background4());
            stripe->setAnchorPoint({0, 0.5f});
            stripe->setPosition({x - 8 * k, cy});
            m_sections->addChild(stripe, 0);
        }
        auto textColor = mine ? theme::rgb(m_scheme.highlight1()) : theme::rgb(m_scheme.content1());
        auto rank = makeText(fmt::format("#{}", score->m_playerRank > 0 ? score->m_playerRank : shown + 1), Weight::Bold, 13 * k);
        rank->setColor(theme::rgb(m_scheme.light2()));
        rank->setAnchorPoint({0, 0.5f});
        rank->setPosition({x, cy});
        m_sections->addChild(rank, 1);
        auto player = facts::playerIcon(score, 22 * k);
        player->setPosition({x + 56 * k, cy});
        m_sections->addChild(player, 1);
        auto name = makeText(score->m_userName, Weight::SemiBold, 14 * k);
        name->setColor(textColor);
        name->setAnchorPoint({0, 0.5f});
        name->setPosition({x + 76 * k, cy});
        facts::fit(name, w - 76 * k - 160 * k);
        m_sections->addChild(name, 1);
        // Level scores reuse the profile fields: percent (or time) in stars, coins in coins.
        std::string value = platformer ? facts::formatTime(score->m_stars) : fmt::format("{}%", score->m_stars);
        auto valueLabel = makeText(value, Weight::Bold, 14 * k);
        valueLabel->setColor(textColor);
        valueLabel->setAnchorPoint({1, 0.5f});
        valueLabel->setPosition({x + w - 60 * k, cy});
        m_sections->addChild(valueLabel, 1);
        if (score->m_secretCoins > 0) {
            auto coins = facts::statsRow({{icon::COINS, std::to_string(score->m_secretCoins)}}, 12 * k, COIN_ICON, theme::rgb(m_scheme.content2()));
            coins->setPosition({x + w - 44 * k, cy});
            m_sections->addChild(coins, 1);
        }
        y += rowH;
        shown++;
    }
    return y;
}

// --- the scores, from GD ---

void LevelPage::loadScores(LevelLeaderboardType type) {
    if (m_leaving) return;
    m_board = Board::Loading;
    m_boardType = type;
    m_scores = nullptr;
    rebuildSections();
    auto glm = GameLevelManager::sharedState();
    glm->m_leaderboardManagerDelegate = this;
    glm->getLevelLeaderboard(m_level, type, m_level->isPlatformer() ? LevelLeaderboardMode::Time : LevelLeaderboardMode::Points);
}

void LevelPage::loadLeaderboardFinished(CCArray* scores, char const*) {
    auto glm = GameLevelManager::sharedState();
    if (glm->m_leaderboardManagerDelegate == this) glm->m_leaderboardManagerDelegate = nullptr;
    if (m_board != Board::Loading || m_leaving) return;
    m_board = Board::Loaded;
    m_scores = scores;
    rebuildSections();
}

void LevelPage::loadLeaderboardFailed(char const*) {
    auto glm = GameLevelManager::sharedState();
    if (glm->m_leaderboardManagerDelegate == this) glm->m_leaderboardManagerDelegate = nullptr;
    if (m_board != Board::Loading || m_leaving) return;
    m_board = Board::Failed;
    rebuildSections();
}

} // namespace lazer
