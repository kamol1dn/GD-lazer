#include "MusicPlayer.hpp"

#include <Geode/Geode.hpp>
#include <Geode/modify/GameManager.hpp>

#include <algorithm>
#include <random>
#include <unordered_map>

using namespace geode::prelude;

namespace lazer {

namespace {
    constexpr int CHANNEL = 0;                 // GD's background music channel
    constexpr unsigned RESTART_CUTOFF_MS = 5000; // MusicController.restart_cutoff_point
    constexpr float END_GRACE_S = 1.f;
    constexpr unsigned MIN_TRACK_MS = 30000;         // ignore "not playing" right after starting a track

    std::mt19937& rng() {
        static std::mt19937 r {std::random_device {}()};
        return r;
    }

    FMODAudioEngine* engine() { return FMODAudioEngine::sharedEngine(); }

    bool enabled() {
        auto mod = Mod::get();
        return mod->getSettingValue<bool>("enabled") && mod->getSettingValue<bool>("music-player");
    }

    std::vector<int> parseSongIDs(std::string const& list) {
        std::vector<int> ids;
        for (auto part : utils::string::split(list, ",")) {
            if (auto id = utils::numFromString<int>(part); id && *id > 0) ids.push_back(*id);
        }
        return ids;
    }
}

MusicPlayer& MusicPlayer::get() {
    // Never freed: the scheduler holds on to it for the whole session.
    static auto instance = new MusicPlayer();
    return *instance;
}

MusicPlayer::MusicPlayer() {
    m_shuffle = Mod::get()->getSavedValue<bool>("music-shuffle", false);
    for (int id : Mod::get()->getSavedValue<std::vector<int>>("music-blocked", {})) m_blocked.insert(id);
    CCScheduler::get()->scheduleUpdateForTarget(this, 0, false);
}

void MusicPlayer::rebuildPlaylist() {
    auto songs = MusicDownloadManager::sharedState();
    auto levels = GameLevelManager::sharedState()->m_onlineLevels;

    // Song ID -> track, gathered from every saved online level.
    std::unordered_map<int, size_t> byId;
    std::vector<Track> tracks;
    if (levels) {
        for (auto [key, object] : CCDictionaryExt<gd::string, CCObject*>(levels)) {
            auto level = typeinfo_cast<GJGameLevel*>(object);
            if (!level) continue;

            std::vector<int> ids;
            if (level->m_songID > 0) ids.push_back(level->m_songID);
            for (int id : parseSongIDs(level->m_songIDs)) {
                if (std::find(ids.begin(), ids.end(), id) == ids.end()) ids.push_back(id);
            }

            for (int id : ids) {
                auto it = byId.find(id);
                if (it == byId.end()) {
                    if (m_blocked.contains(id) || !songs->isSongDownloaded(id)) continue;
                    Track track {id, songs->pathForSong(id), fmt::format("Song {}", id), "", {}};
                    if (auto info = songs->getSongInfoObject(id)) {
                        if (!info->m_songName.empty()) track.title = info->m_songName;
                        track.artist = info->m_artistName;
                    }
                    it = byId.emplace(id, tracks.size()).first;
                    tracks.push_back(std::move(track));
                }
                tracks[it->second].levels.push_back({
                    level->m_levelID.value(), level->m_levelName, level->m_creatorName,
                });
            }
        }
    }

    // Shuffled once, so the menu doesn't always start on the same song.
    std::shuffle(tracks.begin(), tracks.end(), rng());

    // Keep the current track selected across rebuilds.
    size_t index = 0;
    if (auto cur = current()) {
        for (size_t i = 0; i < tracks.size(); i++) {
            if (tracks[i].songID == cur->songID) { index = i; break; }
        }
    }
    bool keep = current() != nullptr;
    m_tracks = std::move(tracks);
    m_index = index;
    m_history.clear();
    if (!keep) m_savedPosition = 0;
}

MusicPlayer::Track const* MusicPlayer::current() const {
    return m_index < m_tracks.size() ? &m_tracks[m_index] : nullptr;
}

bool MusicPlayer::startMenuMusic() {
    if (!enabled()) return false;

    // Our song is already playing (e.g. GD asked again on a menu change): leave it alone.
    if (m_active) return true;

    int previousSong = current() ? current()->songID : -1;
    rebuildPlaylist();
    log::info("Menu music: {} downloaded level songs", m_tracks.size());
    if (m_tracks.empty()) return false;

    // The intro starts it (see releaseIntro).
    if (m_introHold) return true;

    bool resume = current() && current()->songID == previousSong;
    play(m_index, Direction::None, resume ? m_savedPosition : 0, 1.f);
    return true;
}

bool MusicPlayer::releaseIntro() {
    bool held = m_introHold;
    m_introHold = false;
    log::info("Intro starts the music (held: {})", held);
    if (!enabled()) return false;
    if (m_tracks.empty()) {
        if (!held) return false;
        rebuildPlaylist();
        if (m_tracks.empty()) return false;
    }
    play(m_index, Direction::None, 0, 0.f);
    return true;
}

void MusicPlayer::play(size_t index, Direction direction, unsigned startMs, float fadeIn) {
    if (index >= m_tracks.size()) return;
    m_index = index;
    auto& track = m_tracks[index];

    log::info("Playing \"{}\" by {} ({} level(s), {})", track.title, track.artist, track.levels.size(), track.path);
    engine()->playMusic(track.path, false, fadeIn, CHANNEL);

    // Levels can use library sound effects as their song: skip anything that
    // short, it's not menu music.
    unsigned length = engine()->getMusicLengthMS(CHANNEL);
    if (length > 0 && length < MIN_TRACK_MS && m_tracks.size() > 1) {
        log::info("Skipping \"{}\" ({} ms, too short)", track.title, length);
        m_tracks.erase(m_tracks.begin() + index);
        m_history.clear();
        play(index % m_tracks.size(), direction, 0, fadeIn);
        return;
    }
    if (startMs > 0) engine()->setMusicTimeMS(startMs, true, CHANNEL);

    m_active = true;
    m_paused = false;
    m_sinceStart = 0;
    m_savedPosition = startMs;
    notify(direction);
}

void MusicPlayer::togglePause() {
    if (!m_active) {
        // Something else took the channel (a song preview...): take it back.
        if (!m_tracks.empty()) play(m_index, Direction::None, m_savedPosition);
        return;
    }
    m_paused = !m_paused;
    if (m_paused) engine()->pauseMusic(CHANNEL);
    else engine()->resumeMusic(CHANNEL);
}

void MusicPlayer::next() {
    if (m_tracks.empty()) return;
    size_t index = (m_index + 1) % m_tracks.size();
    if (m_shuffle && m_tracks.size() > 1) {
        std::uniform_int_distribution<size_t> pick(0, m_tracks.size() - 2);
        index = pick(rng());
        if (index >= m_index) index++; // never the same track twice in a row
    }
    m_history.push_back(m_index);
    play(index, Direction::Next);
}

void MusicPlayer::previous() {
    if (m_tracks.empty()) return;
    if (m_active && positionMs() >= RESTART_CUTOFF_MS) {
        seek(0);
        return;
    }
    size_t index;
    if (!m_history.empty()) {
        index = m_history.back();
        m_history.pop_back();
    } else {
        index = (m_index + m_tracks.size() - 1) % m_tracks.size();
    }
    if (index >= m_tracks.size()) index = 0;
    play(index, Direction::Prev);
}

void MusicPlayer::seek(float fraction) {
    if (!m_active) return;
    unsigned len = lengthMs();
    if (len == 0) return;
    unsigned ms = static_cast<unsigned>(std::clamp(fraction, 0.f, 1.f) * (len - 1));
    engine()->setMusicTimeMS(ms, true, CHANNEL);
    m_savedPosition = ms;
}

void MusicPlayer::blockCurrent() {
    auto track = current();
    if (!track) return;
    log::info("Blocking \"{}\" ({})", track->title, track->songID);
    m_blocked.insert(track->songID);
    Mod::get()->setSavedValue("music-blocked", std::vector<int>(m_blocked.begin(), m_blocked.end()));

    size_t index = m_index;
    m_tracks.erase(m_tracks.begin() + index);
    m_history.clear();
    if (m_tracks.empty()) {
        // Nothing of ours left: GD's own menu loop takes over.
        m_active = false;
        m_index = 0;
        notify(Direction::None);
        GameManager::get()->playMenuMusic();
        return;
    }
    play(index % m_tracks.size(), Direction::Next);
}

void MusicPlayer::unblockAll() {
    m_blocked.clear();
    Mod::get()->setSavedValue("music-blocked", std::vector<int>());
    // Picked up the next time the playlist is built (menu music restart).
}

void MusicPlayer::toggleShuffle() {
    m_shuffle = !m_shuffle;
    Mod::get()->setSavedValue("music-shuffle", m_shuffle);
}

unsigned MusicPlayer::positionMs() const {
    return m_active ? engine()->getMusicTimeMS(CHANNEL) : m_savedPosition;
}

unsigned MusicPlayer::lengthMs() const {
    return m_active ? engine()->getMusicLengthMS(CHANNEL) : 0;
}

int MusicPlayer::addListener(Listener listener) {
    int id = m_nextListenerId++;
    m_listeners.emplace_back(id, std::move(listener));
    return id;
}

void MusicPlayer::removeListener(int id) {
    std::erase_if(m_listeners, [id](auto const& l) { return l.first == id; });
}

void MusicPlayer::notify(Direction direction) {
    auto listeners = m_listeners; // listeners may unsubscribe while being called
    for (auto& [id, fn] : listeners) fn(current(), direction);
}

void MusicPlayer::update(float dt) {
    if (!m_active) return;
    auto track = current();
    if (!track) {
        m_active = false;
        return;
    }

    // A level, a song preview or GD itself replaced our song: step aside, but
    // remember where we were so returning to the menu resumes it.
    if (std::string active = engine()->getActiveMusic(CHANNEL); active != track->path) {
        log::debug("Music channel taken over by {}", active);
        m_active = false;
        m_paused = false;
        return;
    }
    if (m_paused) return;

    m_sinceStart += dt;
    unsigned pos = engine()->getMusicTimeMS(CHANNEL);
    unsigned len = engine()->getMusicLengthMS(CHANNEL);
    if (pos > 0) m_savedPosition = pos;

    if (m_sinceStart < END_GRACE_S) return;
    bool playing = engine()->isMusicPlaying(CHANNEL);
    bool ended = !playing || (len > 0 && pos + 30 >= len);
    if (ended) {
        log::info("Track ended (playing={}, pos={}ms, len={}ms)", playing, pos, len);
        next();
    }
}

} // namespace lazer

// GD starts its menu loop through these two; both hand over to the player.
class $modify(LazerMenuMusic, GameManager) {
    void fadeInMenuMusic() {
        if (lazer::MusicPlayer::get().startMenuMusic()) return;
        GameManager::fadeInMenuMusic();
    }

    void playMenuMusic() {
        if (lazer::MusicPlayer::get().startMenuMusic()) return;
        GameManager::playMenuMusic();
    }
};
