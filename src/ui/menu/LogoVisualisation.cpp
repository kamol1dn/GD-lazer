#include "LogoVisualisation.hpp"

#include "../../audio/AudioAnalyzer.hpp"

#include <cmath>

using namespace cocos2d;

namespace lazer {

namespace {
    // Constants from LogoVisualisation.cs. osu!'s visualiser is 480px wide for a
    // 600px bar_length, so bar length is expressed relative to the diameter.
    constexpr int INDEX_CHANGE = 5;
    constexpr float BAR_LENGTH_RATIO = 600.f / 480.f;
    constexpr int ROUNDS = 5;
    constexpr float DECAY_PER_MS = 0.0024f;
    constexpr float TIME_BETWEEN_UPDATES = 50.f;
    constexpr float DEAD_ZONE = 1.f / 600.f;
    constexpr float ALPHA = 0.2f;
    // No kiai sections in GD menu music: osu! uses 0.5 outside kiai.
    constexpr float KIAI_MULTIPLIER = 0.5f;
    constexpr float PI = 3.14159265f;
}

LogoVisualisation* LogoVisualisation::create(float diameter) {
    auto ret = new LogoVisualisation();
    if (ret->init(diameter)) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

bool LogoVisualisation::init(float diameter) {
    if (!CCNodeRGBA::init()) return false;
    this->setContentSize({diameter, diameter});
    this->setAnchorPoint({0.5f, 0.5f});
    this->setShaderProgram(CCShaderCache::sharedShaderCache()->programForKey(kCCShader_PositionColor));
    m_vertices.reserve(BARS * ROUNDS * 6);
    this->scheduleUpdate();
    return true;
}

void LogoVisualisation::updateAmplitudes() {
    auto& audio = AudioAnalyzer::get();
    auto const& spectrum = audio.spectrum();
    for (int i = 0; i < BARS; i++) {
        float target = spectrum[(i + m_indexOffset) % BARS] * KIAI_MULTIPLIER;
        if (target > m_amplitudes[i]) m_amplitudes[i] = target;
    }
    m_indexOffset = (m_indexOffset + INDEX_CHANGE) % BARS;
}

void LogoVisualisation::update(float dt) {
    float ms = dt * 1000.f;
    AudioAnalyzer::get().update(dt);

    m_sinceUpdateMs += ms;
    if (m_sinceUpdateMs >= TIME_BETWEEN_UPDATES) {
        // No catch-up executions, same as osu!.
        m_sinceUpdateMs = std::fmod(m_sinceUpdateMs, TIME_BETWEEN_UPDATES);
        updateAmplitudes();
    }

    float decay = ms * DECAY_PER_MS;
    for (auto& a : m_amplitudes) {
        // 3% extra so bars finish falling a little faster near zero.
        a -= decay * (a + 0.03f);
        if (a < 0) a = 0;
    }
}

void LogoVisualisation::draw() {
    float size = this->getContentSize().width;
    float barLength = size * BAR_LENGTH_RATIO;
    float barWidth = size * std::sqrt(2 * (1 - std::cos(2 * PI / BARS))) / 2.f;

    auto color = this->getDisplayedColor();
    GLubyte alpha = static_cast<GLubyte>(ALPHA * this->getDisplayedOpacity());
    // Additive blending, so premultiply.
    ccColor4B c {
        static_cast<GLubyte>(color.r * alpha / 255),
        static_cast<GLubyte>(color.g * alpha / 255),
        static_cast<GLubyte>(color.b * alpha / 255),
        alpha,
    };

    m_vertices.clear();
    for (int j = 0; j < ROUNDS; j++) {
        for (int i = 0; i < BARS; i++) {
            if (m_amplitudes[i] < DEAD_ZONE) continue;

            float rotation = (i / float(BARS) * 360.f + j * 360.f / ROUNDS) * PI / 180.f;
            float cs = std::cos(rotation);
            float sn = std::sin(rotation);

            ccVertex2F base {cs / 2 * size + size / 2, sn / 2 * size + size / 2};
            float h = barLength * m_amplitudes[i];
            ccVertex2F side {-sn * barWidth / 2, cs * barWidth / 2};
            ccVertex2F up {cs * h, sn * h};

            Vertex a {{base.x - side.x, base.y - side.y}, c};
            Vertex b {{base.x + side.x, base.y + side.y}, c};
            Vertex a2 {{a.pos.x + up.x, a.pos.y + up.y}, c};
            Vertex b2 {{b.pos.x + up.x, b.pos.y + up.y}, c};
            m_vertices.insert(m_vertices.end(), {a, b, a2, b, b2, a2});
        }
    }
    if (m_vertices.empty()) return;

    CC_NODE_DRAW_SETUP();
    ccGLBlendFunc(GL_ONE, GL_ONE);
    ccGLEnableVertexAttribs(kCCVertexAttribFlag_Position | kCCVertexAttribFlag_Color);
    glVertexAttribPointer(kCCVertexAttrib_Position, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), &m_vertices[0].pos);
    glVertexAttribPointer(kCCVertexAttrib_Color, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(Vertex), &m_vertices[0].color);
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(m_vertices.size()));
    CC_INCREMENT_GL_DRAWS(1);
}

} // namespace lazer
