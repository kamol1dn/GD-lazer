#include "Triangles.hpp"

#include <algorithm>
#include <cmath>

using namespace cocos2d;

namespace lazer {

namespace {
    // Triangles.cs
    constexpr float BASE_VELOCITY_RATIO = 50.f / 100.f; // base_velocity / triangle_size
    constexpr float EQUILATERAL = 0.866f;
}

Triangles* Triangles::create(CCSize size, float triangleSize, int count) {
    auto ret = new Triangles();
    if (ret->init(size, triangleSize, count)) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

bool Triangles::init(CCSize size, float triangleSize, int count) {
    if (!CCNodeRGBA::init()) return false;
    this->setContentSize(size);
    this->setShaderProgram(CCShaderCache::sharedShaderCache()->programForKey(kCCShader_PositionColor));
    m_triangleSize = triangleSize;
    for (int i = 0; i < count; i++) m_particles.push_back(spawn(true));
    this->scheduleUpdate();
    return true;
}

Triangles::Particle Triangles::spawn(bool anywhere) {
    std::uniform_real_distribution<float> u(0.f, 1.f);
    // Triangles.cs: scale follows a clamped normal distribution (mean 0.5, sd 0.16).
    std::normal_distribution<float> n(0.5f, 0.16f);
    float scale = std::clamp(n(m_rng), 0.05f, 1.f) * 2.f;

    auto size = this->getContentSize();
    Particle p;
    p.scale = scale;
    p.shade = u(m_rng);
    p.x = u(m_rng) * size.width;
    // New triangles enter from just below the bottom edge.
    p.y = anywhere ? u(m_rng) * size.height : -m_triangleSize * scale * EQUILATERAL;
    return p;
}

void Triangles::update(float dt) {
    auto size = this->getContentSize();
    float moved = dt * m_velocity * BASE_VELOCITY_RATIO * m_triangleSize;
    for (auto& p : m_particles) {
        // Bigger triangles move faster (Triangles.cs: Math.Max(0.5f, 2 * scale)).
        p.y += std::max(0.5f, p.scale) * moved;
        if (p.y - m_triangleSize * p.scale * EQUILATERAL > size.height) p = spawn(false);
    }
}

void Triangles::draw() {
    auto tint = this->getDisplayedColor();
    float opacity = this->getDisplayedOpacity() / 255.f;

    m_vertices.clear();
    for (auto const& p : m_particles) {
        float w = m_triangleSize * p.scale;
        float h = w * EQUILATERAL;
        float a = (m_alphaMin + (m_alphaMax - m_alphaMin) * p.shade) * opacity;
        // Premultiplied.
        ccColor4B c {
            static_cast<GLubyte>(tint.r * a),
            static_cast<GLubyte>(tint.g * a),
            static_cast<GLubyte>(tint.b * a),
            static_cast<GLubyte>(255 * a),
        };
        m_vertices.push_back({{p.x - w / 2, p.y}, c});
        m_vertices.push_back({{p.x + w / 2, p.y}, c});
        m_vertices.push_back({{p.x, p.y + h}, c});
    }
    if (m_vertices.empty()) return;

    CC_NODE_DRAW_SETUP();
    ccGLBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    ccGLEnableVertexAttribs(kCCVertexAttribFlag_Position | kCCVertexAttribFlag_Color);
    glVertexAttribPointer(kCCVertexAttrib_Position, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), &m_vertices[0].pos);
    glVertexAttribPointer(kCCVertexAttrib_Color, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(Vertex), &m_vertices[0].color);
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(m_vertices.size()));
    CC_INCREMENT_GL_DRAWS(1);
}

} // namespace lazer
