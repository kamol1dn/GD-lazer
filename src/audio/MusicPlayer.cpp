#include "MusicPlayer.hpp"

#include "../integrations/Ventilla.hpp"

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
    constexpr float RADIO_POLL_S = 0.5f;             // how often the stream's title is read

    std::mt19937& rng() {
        static std::mt19937 r {std::random_device {}()};
        return r;
    }

    FMODAudioEngine* engine() { return FMODAudioEngine::sharedEngine(); }

    bool enabled() {
        auto mod = Mod::get();
        return mod->getSettingValue<bool>("enabled") && mod->getSettingValue<bool>("music-player");
    }

    constexpr int MAIN_LEVEL_SONGS = 22;

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

    // RobTop's main level songs (Stereo Madness .. Dash), which ship with the game.
    // Negative IDs keep them apart from Newgrounds songs (and blockable).
    for (int audio = 0; audio < MAIN_LEVEL_SONGS; audio++) {
        int id = officialSongID(audio);
        if (m_blocked.contains(id)) continue;
        std::string file = LevelTools::getAudioFileName(audio);
        auto path = CCFileUtils::sharedFileUtils()->fullPathForFilename(file.c_str(), false);
        if (path.empty() || !CCFileUtils::sharedFileUtils()->isFileExist(path)) continue;
        // Played by bare file name, like GD does: on Android the full path is
        // "assets/...", which FMOD can't open.
        tracks.push_back({
            id, file, LevelTools::getAudioTitle(audio),
            LevelTools::nameForArtist(LevelTools::artistForAudio(audio)), {},
        });
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
    if (m_radio) return &m_radioTrack;
    return m_index < m_tracks.size() ? &m_tracks[m_index] : nullptr;
}

bool MusicPlayer::isPaused() const {
    return m_radio ? ventilla::paused() : m_paused;
}

bool MusicPlayer::radioWanted() const {
    return Mod::get()->getSettingValue<bool>("ventilla-radio") && ventilla::radioOn();
}

void MusicPlayer::enterRadio() {
    bool was = m_radio;
    if (!was) log::info("Menu music: Ventilla's radio");
    m_radio = true;
    m_active = false;
    m_paused = false;
    m_radioPoll = 0;
    ventilla::setPaused(false);
    bool changed = refreshRadioTrack();
    if (!was || changed) notify(Direction::None);
}

void MusicPlayer::leaveRadio() {
    log::info("Menu music: back to the songs");
    // Ventilla streams on a separate FMOD channel; replacing GD's music
    // alone cannot silence it. Leave its level/pause settings unchanged.
    ventilla::setPaused(true);
    m_radio = false;
    if (m_tracks.empty()) rebuildPlaylist();
    if (m_tracks.empty()) {
        // Nothing of ours: GD's loop, already playing, carries on.
        notify(Direction::None);
        return;
    }
    play(m_index, Direction::None, m_savedPosition, 1.f);
}

bool MusicPlayer::refreshRadioTrack() {
    std::string title = ventilla::title();
    if (title.empty()) title = ventilla::playing() ? "Live" : "Connecting...";
    if (title == m_radioTrack.title) return false;
    m_radioTrack.title = title;
    return true;
}

void MusicPlayer::setRadio(bool on) {
    Mod::get()->setSettingValue<bool>("ventilla-radio", on);
    if (on) {
        // Turning it on here means the radio: switch Ventilla's own "Enable
        // Radio" on with it (off stays theirs: the radio may still be wanted
        // in levels or the pause menu).
        if (auto v = ventilla::mod(); v && !v->getSettingValue<bool>("enabled")) v->setSettingValue<bool>("enabled", true);
        if (m_radio || !ventilla::radioOn()) return;
        if (m_active) m_savedPosition = engine()->getMusicTimeMS(CHANNEL);
        m_active = false;
        m_paused = false;
        // GD's loop is Ventilla's cue to start the radio; the menu-music hook
        // below enters radio mode on the way.
        GameManager::get()->playMenuMusic();
    } else if (m_radio) {
        leaveRadio();
    }
}

bool MusicPlayer::startMenuMusic() {
    if (!enabled()) return false;

    // Ventilla's radio plays over GD's own loop (and mutes it): let GD start
    // the loop, and show the stream in the player.
    if (radioWanted()) {
        enterRadio();
        return false;
    }
    m_radio = false;

    // Our song is already playing (e.g. GD asked again on a menu change): leave it alone.
    if (m_active) return true;

    int previousSong = current() ? current()->songID : 0; // 0: no song (main level songs are negative)
    rebuildPlaylist();
    log::info("Menu music: {} songs", m_tracks.size());
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
    if (radioWanted()) {
        enterRadio();
        return false;
    }
    m_radio = false;
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

std::string MusicPlayer::handOff() {
    auto track = current();
    if (!m_active || m_paused || !track || engine()->getActiveMusic(CHANNEL) != track->path) return "";
    m_active = false;
    log::info("Handing \"{}\" to song select", track->title);
    return track->path;
}

void MusicPlayer::adopt(Track track) {
    if (!enabled() || m_introHold || m_radio) return;
    auto e = engine();
    if (!e->isMusicPlaying(CHANNEL) || e->getActiveMusic(CHANNEL) != track.path) return;
    if (m_tracks.empty()) rebuildPlaylist();

    auto it = std::find_if(m_tracks.begin(), m_tracks.end(), [&](Track const& t) { return t.path == track.path; });
    size_t index;
    if (it != m_tracks.end()) {
        index = it - m_tracks.begin();
    } else {
        // Not one of ours: slot it in after the current track, so "next" carries on from there.
        index = std::min(m_index + 1, m_tracks.size());
        m_tracks.insert(m_tracks.begin() + index, std::move(track));
        m_history.clear();
    }
    if (index != m_index && m_index < m_tracks.size()) m_history.push_back(m_index);

    m_index = index;
    m_active = true;
    m_paused = false;
    m_sinceStart = END_GRACE_S;
    m_savedPosition = e->getMusicTimeMS(CHANNEL);
    log::info("Carrying on with \"{}\" from song select", m_tracks[index].title);
    notify(Direction::None);
}

void MusicPlayer::togglePause() {
    if (m_radio) {
        ventilla::setPaused(!ventilla::paused());
        return;
    }
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
    if (m_tracks.empty() || m_radio) return;
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
    if (m_tracks.empty() || m_radio) return;
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
    if (!m_active || m_radio) return;
    unsigned len = lengthMs();
    if (len == 0) return;
    unsigned ms = static_cast<unsigned>(std::clamp(fraction, 0.f, 1.f) * (len - 1));
    engine()->setMusicTimeMS(ms, true, CHANNEL);
    m_savedPosition = ms;
}

void MusicPlayer::blockCurrent() {
    auto track = current();
    if (!track || m_radio) return;
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

void MusicPlayer::unblock(int songID) {
    m_blocked.erase(songID);
    Mod::get()->setSavedValue("music-blocked", std::vector<int>(m_blocked.begin(), m_blocked.end()));
    // Like unblockAll: back in the playlist the next time it's built.
}

std::vector<int> MusicPlayer::blocked() const {
    std::vector<int> ids(m_blocked.begin(), m_blocked.end());
    std::sort(ids.begin(), ids.end());
    return ids;
}

std::pair<std::string, std::string> MusicPlayer::describe(int songID) {
    if (songID < 0) {
        int audio = -songID - 1;
        return {LevelTools::getAudioTitle(audio), LevelTools::nameForArtist(LevelTools::artistForAudio(audio))};
    }
    if (auto info = MusicDownloadManager::sharedState()->getSongInfoObject(songID); info && !info->m_songName.empty()) {
        return {info->m_songName, info->m_artistName};
    }
    return {fmt::format("Song {}", songID), ""};
}

void MusicPlayer::toggleShuffle() {
    m_shuffle = !m_shuffle;
    Mod::get()->setSavedValue("music-shuffle", m_shuffle);
}

unsigned MusicPlayer::positionMs() const {
    if (m_radio) return 0;
    return m_active ? engine()->getMusicTimeMS(CHANNEL) : m_savedPosition;
}

unsigned MusicPlayer::lengthMs() const {
    if (m_radio) return 0; // a live stream has no end
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
    if (m_radio) {
        m_radioPoll += dt;
        if (m_radioPoll < RADIO_POLL_S) return;
        m_radioPoll = 0;
        // Ventilla switched off from its own side: back to the songs.
        if (!radioWanted()) {
            leaveRadio();
            return;
        }
        if (refreshRadioTrack()) notify(Direction::None);
        return;
    }
    // A stream connecting after deselection still needs silencing. Limit this
    // to our menu playback so Ventilla's own gameplay options remain its own.
    if (m_active && !radioWanted()) ventilla::setPaused(true);
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
    // Song select's previews loop: one it handed back wraps round instead of ending.
    bool wrapped = pos + 1000 < m_savedPosition;
    if (pos > 0) m_savedPosition = pos;

    if (m_sinceStart < END_GRACE_S) return;
    bool playing = engine()->isMusicPlaying(CHANNEL);
    bool ended = !playing || wrapped || (len > 0 && pos + 30 >= len);
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
