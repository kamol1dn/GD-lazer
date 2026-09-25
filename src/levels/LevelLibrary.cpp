#include "LevelLibrary.hpp"

#include <algorithm>
#include <cctype>

using namespace geode::prelude;

namespace lazer::levels {

namespace {
    // RobTop's main levels (Stereo Madness .. Dash).
    constexpr int FIRST_MAIN = 1;
    constexpr int LAST_MAIN = 22;

    std::string lower(std::string s) {
        for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return s;
    }

    // GJDifficultySprite frame for a level, the way GD's level cells pick it.
    int difficultyFrame(GJGameLevel* level, bool official) {
        if (official) {
            int d = static_cast<int>(level->m_difficulty);
            return std::clamp(d, 0, 6);
        }
        if (level->m_autoLevel) return -1;
        if (level->m_demon.value() > 0) {
            switch (level->m_demonDifficulty) {
                case 3: return 7;  // easy demon
                case 4: return 8;  // medium
                case 5: return 9;  // insane
                case 6: return 10; // extreme
                default: return 6; // hard
            }
        }
        return std::clamp(level->getAverageDifficulty(), 0, 5);
    }

    void fillSong(Entry& e, GJGameLevel* level) {
        if (level->m_songID > 0) {
            auto songs = MusicDownloadManager::sharedState();
            e.songTitle = fmt::format("Song {}", level->m_songID);
            if (auto info = songs->getSongInfoObject(level->m_songID)) {
                if (!info->m_songName.empty()) e.songTitle = info->m_songName;
                e.songArtist = info->m_artistName;
            }
            if (songs->isSongDownloaded(level->m_songID)) e.songPath = songs->pathForSong(level->m_songID);
        } else {
            int track = level->m_audioTrack;
            e.songTitle = LevelTools::getAudioTitle(track);
            e.songArtist = LevelTools::nameForArtist(LevelTools::artistForAudio(track));
            std::string file = LevelTools::getAudioFileName(track);
            e.songPath = CCFileUtils::sharedFileUtils()->fullPathForFilename(file.c_str(), false);
        }
    }

    void fillCoins(Entry& e, GJGameLevel* level) {
        e.coins = level->m_coins;
        e.coinsVerified = e.official || level->m_coinsVerified.value() > 0;
        auto stats = GameStatsManager::sharedState();
        for (int i = 1; i <= e.coins; i++) {
            auto key = level->getCoinKey(i);
            if (!key) continue;
            bool has = e.official ? stats->hasSecretCoin(key) : stats->hasUserCoin(key);
            if (has) e.coinsCollected++;
        }
    }

    Entry make(GJGameLevel* level, bool official) {
        Entry e;
        e.level = level;
        e.official = official;
        e.id = level->m_levelID.value();
        e.name = level->m_levelName;
        e.creator = official ? "RobTop" : std::string(level->m_creatorName);
        e.difficulty = difficultyFrame(level, official);
        e.stars = level->m_stars.value();
        e.normalPercent = level->m_normalPercent.value();
        e.practicePercent = level->m_practicePercent;
        e.length = level->m_levelLength;
        fillSong(e, level);
        fillCoins(e, level);
        e.search = lower(e.name + " " + e.creator + " " + e.songTitle + " " + e.songArtist);
        return e;
    }
}

std::vector<Entry> all() {
    std::vector<Entry> entries;
    auto glm = GameLevelManager::sharedState();

    for (int id = FIRST_MAIN; id <= LAST_MAIN; id++) {
        auto level = glm->getMainLevel(id, false);
        if (!level || std::string(level->m_levelName).empty()) continue;
        entries.push_back(make(level, true));
    }

    if (auto saved = glm->getSavedLevels(false, 0)) {
        for (auto level : CCArrayExt<GJGameLevel*>(saved)) {
            if (!level || level->m_levelID.value() <= 0) continue;
            entries.push_back(make(level, false));
        }
    }
    return entries;
}

ccColor3B difficultyColor(int difficulty) {
    switch (difficulty) {
        case -1: return {255, 214, 76};  // auto
        case 1: return {70, 180, 255};   // easy
        case 2: return {90, 210, 90};    // normal
        case 3: return {255, 200, 50};   // hard
        case 4: return {255, 110, 50};   // harder
        case 5: return {255, 80, 170};   // insane
        case 7: return {160, 110, 255};  // easy demon
        case 8: return {200, 80, 255};   // medium demon
        case 6: return {230, 50, 60};    // hard demon
        case 9: return {200, 30, 90};    // insane demon
        case 10: return {150, 20, 40};   // extreme demon
        default: return {150, 150, 160}; // N/A
    }
}

char const* lengthName(int length) {
    switch (length) {
        case 0: return "tiny";
        case 1: return "short";
        case 2: return "medium";
        case 3: return "long";
        case 4: return "XL";
        case 5: return "plat.";
        default: return "?";
    }
}

bool readyToPlay(Entry const& entry) {
    if (entry.official) return true;
    return !std::string(entry.level->m_levelString).empty() && !entry.songPath.empty();
}

} // namespace lazer::levels
