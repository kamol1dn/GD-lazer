#pragma once

#include "../core/Easing.hpp"
#include "../core/RoundedBox.hpp"

#include <Geode/Geode.hpp>
#include <functional>
#include <string>
#include <vector>

namespace lazer {

// The game-start intro, after osu!'s IntroTriangles (osu.Game/Screens/Menu/IntroTriangles.cs):
// "welcome to geometry dash" typed out over glitching triangles, GD's cube,
// spike, orb and trigger punching in (osu!'s ruleset icons), then the logo
// drawing itself in and a flash as the menu appears.
//
// osu!'s own theme song is cut to this timeline. Ours is a random level song,
// so it does what osu! does for a user beatmap: the song starts 600 ms in at
// silence and swells in (InCubic over 2.6 s), arriving at full volume as the
// menu is revealed. The reveal itself waits for the song's next beat (within
// a short window), so the flash lands on the music.
//
// Covers the whole menu while it runs; `onReveal` is called at the flash.
class IntroSequence : public cocos2d::CCLayer {
public:
    // `logoRadius`: the menu logo's radius, so the drawn logo lands exactly on it.
    static IntroSequence* create(float logoRadius, std::function<void()> onReveal);

    void update(float dt) override;
    void registerWithTouchDispatcher() override;
    bool ccTouchBegan(cocos2d::CCTouch*, cocos2d::CCEvent*) override { return true; }
    void keyBackClicked() override {}

protected:
    struct Triangle {
        cocos2d::CCPoint pos; // top-left, in the triangles area
        float size;
        bool outline;
        float ageMs;
    };

    bool init(float logoRadius, std::function<void()> onReveal);
    void setText(std::string const& text);
    void layoutText();
    void updateTriangles(float ms);
    void layoutRulesets();
    void drawLogo(float progress);
    void reveal();
    void setMusicVolume(float volume);

    std::function<void()> m_onReveal;
    float m_k = 1;
    cocos2d::CCSize m_win;
    float m_timeMs = 0;
    float m_lastMs = -1;
    bool m_started = false;
    bool m_trackStarted = false;
    bool m_revealed = false;
    int m_beatAtStart = 0;

    cocos2d::CCNode* m_content = nullptr;

    // "welcome to geometry dash", one label per character so letter spacing can animate.
    cocos2d::CCNode* m_text = nullptr;
    std::vector<cocos2d::CCLabelBMFont*> m_chars;
    float m_spacing = 5;

    cocos2d::CCDrawNode* m_triangleDraw = nullptr;
    std::vector<Triangle> m_triangles;
    float m_triangleClock = 0;
    bool m_trianglesOn = false;

    cocos2d::CCNode* m_rulesetsScale = nullptr;
    cocos2d::CCNode* m_rulesets = nullptr;
    std::vector<cocos2d::CCNode*> m_icons;
    float m_rulesetSpacing = 200;
    Tweened<float> m_rulesetsScaleTween {1.f};

    cocos2d::CCNode* m_logoContainer = nullptr;
    cocos2d::CCNode* m_logo = nullptr;
    RoundedBox* m_logoDisc = nullptr;
    cocos2d::CCLabelBMFont* m_logoText = nullptr;
    cocos2d::CCDrawNode* m_logoRing = nullptr;
    float m_logoBaseRadius = 0;
    Tweened<float> m_logoScale {1.2f};
    Tweened<float> m_logoContainerScale {1.2f};
};

} // namespace lazer
