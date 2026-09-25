#pragma once

#include <Geode/cocos/include/cocos2d.h>
#include <random>
#include <vector>

namespace lazer {

// Upward-drifting triangles, after osu.Game/Graphics/Backgrounds/Triangles.cs.
class Triangles : public cocos2d::CCNodeRGBA {
public:
    // `triangleSize` is the base edge length; `count` how many are alive at once.
    static Triangles* create(cocos2d::CCSize size, float triangleSize, int count);

    void setVelocity(float v) { m_velocity = v; }
    // Per-triangle alpha range; each triangle picks a shade in between.
    void setAlphaRange(float min, float max) { m_alphaMin = min; m_alphaMax = max; }

    void update(float dt) override;
    void draw() override;

protected:
    bool init(cocos2d::CCSize size, float triangleSize, int count);

    struct Particle {
        float x, y;   // centre of the base, in node units
        float scale;
        float shade;  // 0..1, picks the alpha
    };
    Particle spawn(bool anywhere);

    float m_triangleSize = 0;
    float m_velocity = 1;
    float m_alphaMin = 0.02f;
    float m_alphaMax = 0.08f;
    std::vector<Particle> m_particles;
    std::mt19937 m_rng {std::random_device{}()};

    struct Vertex {
        cocos2d::ccVertex2F pos;
        cocos2d::ccColor4B color;
    };
    std::vector<Vertex> m_vertices;
};

} // namespace lazer
