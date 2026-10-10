#pragma once

#include <Geode/cocos/include/cocos2d.h>
#include <functional>
#include <string>
#include <unordered_set>
#include <vector>

namespace lazer {

// The menu's music, after osu!'s MusicController: instead of GD's menu loop it
// plays the downloaded songs of your saved levels and the main levels' songs,
// one after another. Each
// track remembers which levels use it, so the menu can show their thumbnail.
//
// Playback goes through GD's own music channel (FMODAudioEngine channel 0), so
// the music volume, the visualiser and other audio mods all keep working.
class MusicPlayer : public cocos2d::CCObject {
public:
    struct Level {
        int id;
        std::string name;
        std::string creator;
    };

    struct Track {
        int songID;
        std::string path;
        std::string title;
        std::string artist;
        std::vector<Level> levels; // saved levels that use this song

        // Levels to look for a thumbnail in, in order.
        std::vector<int> levelIDs(size_t max = 4) const {
            std::vector<int> ids;
            for (size_t i = 0; i < levels.size() && i < max; i++) ids.push_back(levels[i].id);
            return ids;
        }
    };

    // Track::songID of a main level's song (GD audio track 0 = Stereo Madness).
    static int officialSongID(int audioTrack) { return -(audioTrack + 1); }

    enum class Direction { None, Next, Prev };
    using Listener = std::function<void(Track const*, Direction)>;

    static MusicPlayer& get();

    // Called from GD's menu-music hooks. Starts (or resumes) our playlist and
    // returns true, or returns false to let GD play its own loop.
    bool startMenuMusic();

    // Game start: hold the first song back until the intro starts it (osu!'s
    // IntroScreen.StartTrack).
    void holdForIntro() { m_introHold = true; }
    // The intro's song: Dash (MDK), the last main level's, GD's audio track 21.
    static constexpr int INTRO_AUDIO = 21;
    static std::string introFile();
    // Whether startIntroTrack() can: the player on, no radio, Dash not blocked.
    bool introTrackPossible() const;
    // Plays Dash from `startMs` on the music channel as the intro begins, so
    // it carries on as the menu's first song. False when it can't (see above,
    // or the file missing): the hold stays for releaseIntro() then.
    bool startIntroTrack(unsigned startMs);
    // Starts the current song from the top. Returns false if there is no song
    // of ours to play (GD's own menu loop is on the channel then).
    bool releaseIntro();

    Track const* current() const;
    bool hasTracks() const { return !m_tracks.empty(); }
    // Our track is on the music channel (it may be paused), or the radio is on.
    bool isActive() const { return m_active || m_radio; }
    bool isPaused() const;

    // JoseII's Ventilla radio in place of the songs (with Ventilla installed
    // and its radio on): GD's own loop plays, Ventilla streams over it, and
    // current() describes the stream. Saved as the "ventilla-radio" setting.
    bool radio() const { return m_radio; }
    void setRadio(bool on);

    void togglePause();
    void next();
    // Restarts the track when more than 5 s in, otherwise goes back one (MusicController.prev).
    void previous();
    void seek(float fraction);

    bool shuffle() const { return m_shuffle; }
    void toggleShuffle();

    // Never play the current song again (saved), and skip to the next one.
    void blockCurrent();
    size_t blockedCount() const { return m_blocked.size(); }
    void unblockAll();
    void unblock(int songID);
    // Blocked song IDs, sorted.
    std::vector<int> blocked() const;
    // A song's title and artist from GD's song info (or RobTop's list).
    static std::pair<std::string, std::string> describe(int songID);

    // Song select takes over the channel with our song still playing (osu! keeps
    // the track going into song select): returns its path, or "" if nothing of
    // ours is playing. We stop following the channel until adopt().
    std::string handOff();
    // Back from song select: carry on with whatever it left playing as the
    // current track, without restarting it. `track` describes the song in case
    // it isn't in the playlist (RobTop's songs, a level saved since).
    void adopt(Track track);

    unsigned positionMs() const;
    unsigned lengthMs() const;

    // Notified on the main thread whenever the current track changes.
    int addListener(Listener listener);
    void removeListener(int id);

    void update(float dt) override;

private:
    MusicPlayer();
    void rebuildPlaylist();
    bool radioWanted() const;
    void enterRadio();
    void leaveRadio();
    // The stream's title into the radio track; true when it changed.
    bool refreshRadioTrack();
    void play(size_t index, Direction direction, unsigned startMs = 0, float fadeIn = 0.f);
    void notify(Direction direction);

    std::vector<Track> m_tracks;
    size_t m_index = 0;
    bool m_active = false;
    bool m_paused = false;
    bool m_shuffle = false;
    bool m_introHold = false;
    std::unordered_set<int> m_blocked; // song IDs, saved as "music-blocked"
    unsigned m_savedPosition = 0; // where to resume after a level took over the channel
    bool m_radio = false;
    Track m_radioTrack {0, "", "Connecting...", "Ventilla radio", {}};
    float m_radioPoll = 0;
    float m_sinceStart = 0;
    std::vector<size_t> m_history; // for "previous" in shuffle mode

    int m_nextListenerId = 1;
    std::vector<std::pair<int, Listener>> m_listeners;
};

// Calls `callback` on every track change while this node is in the scene.
class MusicListener : public cocos2d::CCNode {
public:
    static MusicListener* create(MusicPlayer::Listener callback) {
        auto ret = new MusicListener();
        ret->m_callback = std::move(callback);
        ret->init();
        ret->autorelease();
        return ret;
    }

    void onEnter() override {
        CCNode::onEnter();
        m_id = MusicPlayer::get().addListener([this](auto track, auto dir) { m_callback(track, dir); });
    }

    void onExit() override {
        MusicPlayer::get().removeListener(m_id);
        CCNode::onExit();
    }

private:
    MusicPlayer::Listener m_callback;
    int m_id = 0;
};

} // namespace lazer
