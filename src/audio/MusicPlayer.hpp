#pragma once

#include <Geode/cocos/include/cocos2d.h>
#include <functional>
#include <string>
#include <unordered_set>
#include <vector>

namespace lazer {

// The menu's music, after osu!'s MusicController: instead of GD's menu loop it
// plays the downloaded songs of your saved levels, one after another. Each
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

    enum class Direction { None, Next, Prev };
    using Listener = std::function<void(Track const*, Direction)>;

    static MusicPlayer& get();

    // Called from GD's menu-music hooks. Starts (or resumes) our playlist and
    // returns true, or returns false to let GD play its own loop.
    bool startMenuMusic();

    // Game start: hold the first song back until the intro starts it (osu!'s
    // IntroScreen.StartTrack), so it fades in under the animation.
    void holdForIntro() { m_introHold = true; }
    // Starts the current song from the top. Returns false if there is no song
    // of ours to play (GD's own menu loop is on the channel then).
    bool releaseIntro();

    Track const* current() const;
    bool hasTracks() const { return !m_tracks.empty(); }
    // Our track is on the music channel (it may be paused).
    bool isActive() const { return m_active; }
    bool isPaused() const { return m_paused; }

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
