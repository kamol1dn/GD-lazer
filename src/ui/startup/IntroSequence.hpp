#pragma once

#include "../core/Easing.hpp"
#include "../core/PlayerPalette.hpp"

#include <Geode/Geode.hpp>
#include <Geode/fmod/fmod.hpp>
#include <functional>
#include <string>
#include <vector>

namespace lazer {

// The game-start intro, after osu!'s IntroTriangles (osu.Game/Screens/Menu/IntroTriangles.cs),
// cut to Dash (MDK, the last main level's song, GD's own copy) from the bar
// before its first drop: GD's cube, spike, orb and trigger punch in two a beat
// on its stabs (osu!'s ruleset icons) with triangles glitching behind them,
// the logo draws itself in and shrinks to its place on the menu over the last
// two beats as the voice says "Geometry Dash" (the cursor says it too; see
// quips::followSong), and the drop is the flash that reveals the menu, with
// the song playing on as its first track.
//
// The song is the clock: the timeline follows the music channel's position.
// A tap or Escape skips to the reveal.
//
// Without the music player (off, Ventilla's radio, Dash blocked) the intro
// plays its own copy of the song from GD's resources and fades it out as the
// menu's own music fades in at the reveal.
//
// The logo is drawn as line art in the player's colours (osu!'s LogoAnimation
// strokes): a thick coloured pass with a thin glow-coloured highlight racing
// along behind it, ring first, then the cube.
//
// Covers the whole menu while it runs; `onReveal` is called at the flash.
class IntroSequence : public cocos2d::CCLayer {
public:
    // `logoRadius`: the menu logo's radius, so the drawn logo lands exactly on it.
    static IntroSequence* create(float logoRadius, std::function<void()> onReveal);

    void update(float dt) override;
    void registerWithTouchDispatcher() override;
    bool ccTouchBegan(cocos2d::CCTouch*, cocos2d::CCEvent*) override;
    void keyBackClicked() override;
    // The menu is visible once the intro reveals it.
    bool revealed() const { return m_revealed; }

protected:
    struct Triangle {
        cocos2d::CCPoint pos; // top-left, in the triangles area
        float size;
        bool outline;
        float ageMs;
    };

    bool init(float logoRadius, std::function<void()> onReveal);
    // Starts the song (and the timeline with it).
    void start();
    void spawnTriangles(int count, float size);
    void updateTriangles(float ms, bool emitting, float intervalMs);
    void layoutIcons(float spacing);
    void drawLogo(float progress);
    // Draws `path` (a polyline) from its start up to `progress`, coloured
    // from `from` to `to` along its length.
    void drawStroke(std::vector<cocos2d::CCPoint> const& path, float progress, float width,
                    cocos2d::ccColor3B from, cocos2d::ccColor3B to);
    void reveal();
    void skip();
    void setMusicVolume(float volume);

    std::function<void()> m_onReveal;
    float m_k = 1;
    cocos2d::CCSize m_win;
    float m_timeMs = 0;
    float m_lastMs = -1;
    bool m_started = false;
    bool m_musicTrack = false;      // Dash plays on GD's music channel, the menu's first song
    FMOD::Channel* m_cue = nullptr; // or the intro's own copy, faded out at the reveal
    float m_cueVolume = 0;
    bool m_revealed = false;
    float m_revealMs = 0;
    PlayerPalette m_palette;

    cocos2d::CCNode* m_content = nullptr;

    cocos2d::CCDrawNode* m_triangleDraw = nullptr;
    std::vector<Triangle> m_triangles;
    float m_triangleClock = 0;

    cocos2d::CCNode* m_iconsScale = nullptr;
    cocos2d::CCNode* m_icons = nullptr;
    std::vector<cocos2d::CCNode*> m_iconHolders; // each scales its icon for the beat

    cocos2d::CCNode* m_logoContainer = nullptr;
    cocos2d::CCNode* m_logo = nullptr;
    cocos2d::CCDrawNode* m_logoDraw = nullptr;
    std::vector<cocos2d::CCPoint> m_ringPath, m_cubePath, m_innerPath;
    float m_logoBaseRadius = 0;
};

} // namespace lazer
