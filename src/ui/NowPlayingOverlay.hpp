#pragma once

#include "../audio/MusicPlayer.hpp"
#include "Easing.hpp"
#include "RoundedBox.hpp"

#include <Geode/cocos/include/cocos2d.h>
#include <functional>
#include <vector>

namespace lazer {

// osu!'s music player (osu.Game/Overlays/NowPlayingOverlay.cs): a small card
// under the toolbar with the current level's thumbnail, title / artist,
// previous / play / next, shuffle and a seek bar.
class NowPlayingOverlay : public cocos2d::CCNode, public cocos2d::CCTouchDelegate {
public:
    static NowPlayingOverlay* create(float toolbarHeight);

    void open();
    void close();
    void toggle() { m_open ? close() : open(); }
    bool isOpen() const { return m_open; }
    // Escape: closes if open.
    bool back();

    void update(float dt) override;
    void onEnter() override;
    void onExit() override;
    bool ccTouchBegan(cocos2d::CCTouch* touch, cocos2d::CCEvent*) override;
    void ccTouchMoved(cocos2d::CCTouch* touch, cocos2d::CCEvent*) override;
    void ccTouchEnded(cocos2d::CCTouch* touch, cocos2d::CCEvent*) override;
    void ccTouchCancelled(cocos2d::CCTouch* touch, cocos2d::CCEvent* e) override { ccTouchEnded(touch, e); }

protected:
    struct Button {
        cocos2d::CCNodeRGBA* node;
        RoundedBox* hoverBg;
        cocos2d::CCLabelBMFont* icon;
        float size;
        std::function<void()> action;
        Tweened<float> hover {0.f};
        Tweened<float> flash {0.f};
        Tweened<float> scale {1.f};
        bool hovered = false;
        bool active = false; // lit (shuffle on)
    };

    bool init(float toolbarHeight);
    Button& addButton(char const* glyph, float x, float size, float iconScale, std::function<void()> action);
    void setIcon(Button& b, char const* glyph);
    Button* buttonAt(cocos2d::CCPoint world);
    bool inProgressBar(cocos2d::CCPoint world);
    float progressFraction(cocos2d::CCPoint world);
    void onTrackChanged(MusicPlayer::Track const* track, MusicPlayer::Direction direction);
    void showBackground(cocos2d::CCTexture2D* texture, MusicPlayer::Direction direction);
    void setText(cocos2d::CCLabelBMFont* label, std::string const& text);

    float m_k = 1;
    float m_toolbarHeight = 0;
    bool m_open = false;

    cocos2d::CCNodeRGBA* m_panel = nullptr;
    RoundedBox* m_base = nullptr;
    RoundedBox* m_background = nullptr;       // current track's thumbnail
    RoundedBox* m_oldBackground = nullptr;    // sliding out
    cocos2d::CCLabelBMFont* m_title = nullptr;
    cocos2d::CCLabelBMFont* m_artist = nullptr;
    float m_titleScale = 1, m_artistScale = 1;
    RoundedBox* m_progressBg = nullptr;
    RoundedBox* m_progressFill = nullptr;
    std::vector<Button> m_buttons;
    size_t m_playButton = 0;
    size_t m_shuffleButton = 0;

    Tweened<float> m_alpha {0.f};
    Tweened<float> m_scale {0.9f};
    Tweened<float> m_bgShift {0.f};          // new background: slides from +-1 to 0
    Tweened<float> m_oldBgShift {0.f};       // old background: slides from 0 to -+1
    Tweened<float> m_progressHeight {0.f};
    bool m_progressHovered = false;

    Button* m_pressed = nullptr;
    bool m_seeking = false;
    float m_seekFraction = 0;
    float m_lastSeekMs = 0;
    int m_generation = 0; // drops thumbnail loads for tracks we've already left
    int m_listener = 0;
};

} // namespace lazer
