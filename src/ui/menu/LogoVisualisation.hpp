#pragma once

#include <Geode/cocos/include/cocos2d.h>
#include <array>
#include <vector>

namespace lazer {

// Radial spectrum bars around the logo, after osu.Game/Screens/Menu/LogoVisualisation.cs.
// Content size = logo diameter; bars start at the circle's edge and grow outwards.
class LogoVisualisation : public cocos2d::CCNodeRGBA {
public:
    static LogoVisualisation* create(float diameter);

    void update(float dt) override;
    void draw() override;

protected:
    bool init(float diameter);
    void updateAmplitudes();

    static constexpr int BARS = 200;
    std::array<float, BARS> m_amplitudes {};
    float m_sinceUpdateMs = 0;
    int m_indexOffset = 0;

    struct Vertex {
        cocos2d::ccVertex2F pos;
        cocos2d::ccColor4B color;
    };
    std::vector<Vertex> m_vertices;
};

} // namespace lazer
