#pragma once

#include "../../audio/MusicPlayer.hpp"

#include <Geode/cocos/include/cocos2d.h>

namespace lazer {

// Title / artist that fades in at the top right when the song changes, then
// fades away (osu.Game/Screens/Menu/SongTicker.cs). Adds the level it's from.
class SongTicker : public cocos2d::CCNodeRGBA {
public:
    static SongTicker* create(float k);
    void show(MusicPlayer::Track const* track);
    void hide();
    void update(float dt) override;

protected:
    bool init(float k);

    float m_k = 1;
    cocos2d::CCLabelBMFont* m_title = nullptr;
    cocos2d::CCLabelBMFont* m_artist = nullptr;
    cocos2d::CCLabelBMFont* m_level = nullptr;
    float m_timeMs = -1; // time since show(); -1 = hidden
};

} // namespace lazer
