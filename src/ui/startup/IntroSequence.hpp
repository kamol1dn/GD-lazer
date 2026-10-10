#pragma once

#include "../core/Easing.hpp"
#include "../core/PlayerPalette.hpp"

#include <Geode/Geode.hpp>
#include <Geode/fmod/fmod.hpp>
#include <functional>
#include <string>
#include <vector>

namespace lazer {

// The game-start intro, cut to the bar before the first drop of Dash (MDK,
// the last main level's song, GD's own copy), which plays on as the menu's
// first track:
//  - streaks rush past as if the camera were dashing along a level, thicker
//    on every stab and kick of the bar and denser into the drop;
//  - your cube flies in from the left with a trail in your colours, hops on
//    the second beat, then dashes off to the right as the voice says
//    "Geometry Dash", writing GEOMETRY in its wake a letter at a time (the
//    cursor says the line too; see quips::followSong);
//  - DASH slams in under it on the word, shaking the camera;
//  - on the drop a flash reveals the menu and the words zoom through the
//    camera over it.
//
// The song is the clock: the timeline follows the music channel's position.
// A tap or Escape skips to the reveal.
//
// Without the music player (off, Ventilla's radio, Dash blocked) the intro
// plays its own copy of the song from GD's resources and fades it out as the
// menu's own music fades in at the reveal.
//
// Covers the whole menu while it runs; `onReveal` is called at the flash.
class IntroSequence : public cocos2d::CCLayer {
public:
    static IntroSequence* create(std::function<void()> onReveal);

    void update(float dt) override;
    void registerWithTouchDispatcher() override;
    bool ccTouchBegan(cocos2d::CCTouch*, cocos2d::CCEvent*) override;
    void keyBackClicked() override;
    // The menu is visible once the intro reveals it.
    bool revealed() const { return m_revealed; }

protected:
    struct Streak {
        float x, y;     // right end, in points
        float length;
        float width;
        float speed;    // points per second, leftwards
        float alpha;
    };
    struct TrailPoint {
        cocos2d::CCPoint pos;
        float ageMs;
    };

    bool init(std::function<void()> onReveal);
    // Starts the song (and the timeline with it).
    void start();
    void spawnStreaks(int count, float widthScale, float alphaScale);
    void updateStreaks(float dt, float rate, float brightness);
    void updateCube(float dt);
    void updateText();
    void layoutLetters(float spacing);
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

    cocos2d::CCNode* m_content = nullptr; // everything under the flash; shaken as the camera
    cocos2d::CCDrawNode* m_streakDraw = nullptr;
    std::vector<Streak> m_streaks;
    float m_streakClock = 0;

    cocos2d::CCNode* m_cube = nullptr;
    cocos2d::CCDrawNode* m_trailDraw = nullptr;
    std::vector<TrailPoint> m_trail;
    float m_cubeSize = 0;

    cocos2d::CCNode* m_text = nullptr; // GEOMETRY over DASH, at the centre
    std::vector<cocos2d::CCLabelBMFont*> m_letters;
    std::vector<float> m_letterX;      // each letter's centre, at the default spacing
    cocos2d::CCLabelBMFont* m_dash = nullptr;
};

} // namespace lazer
