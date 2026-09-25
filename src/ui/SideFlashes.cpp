#include "SideFlashes.hpp"

#include "../audio/AudioAnalyzer.hpp"

#include <algorithm>

using namespace cocos2d;

namespace lazer {

namespace {
    // Values from MenuSideFlashes.cs.
    constexpr float AMPLITUDE_DEAD_ZONE = 0.25f;
    constexpr float KIAI_MULTIPLIER = (1 - AMPLITUDE_DEAD_ZONE * 0.95f) / 0.8f;
    constexpr float FADE_IN_MS = 65.f;
    constexpr float BOX_WIDTH = 200.f; // osu! pixels; the box is 2x this, half off-screen
    constexpr ccColor3B BLUE {0x66, 0xcc, 0xff}; // OsuColour.Blue
    constexpr float GLOW_ALPHA = 0.6f;
}

SideFlashes* SideFlashes::create() {
    auto ret = new SideFlashes();
    if (ret->init()) {
        ret->autorelease();
        return ret;
    }
    delete ret;
    return nullptr;
}

bool SideFlashes::init() {
    if (!CCNode::init()) return false;
    auto win = CCDirector::sharedDirector()->getWinSize();
    this->setContentSize(win);

    float k = win.height / 768.f;
    float width = BOX_WIDTH * 2 * k;
    GLubyte glow = static_cast<GLubyte>(GLOW_ALPHA * 255);
    for (int side = 0; side < 2; side++) {
        // Bright at the outer edge, fading to nothing towards the centre.
        ccColor4B outer {BLUE.r, BLUE.g, BLUE.b, glow};
        ccColor4B inner {BLUE.r, BLUE.g, BLUE.b, 0};
        auto box = CCLayerGradient::create(side == 0 ? outer : inner, side == 0 ? inner : outer, {1, 0});
        box->setContentSize({width, win.height * 1.5f});
        // Half off-screen, so the edges never show while the background moves.
        float x = side == 0 ? -BOX_WIDTH * k : win.width - BOX_WIDTH * k;
        box->setPosition({x, -win.height * 0.25f});
        box->setBlendFunc({GL_SRC_ALPHA, GL_ONE}); // additive
        box->setOpacity(0);
        this->addChild(box);
        m_boxes[side] = box;
    }

    m_lastBeat = AudioAnalyzer::get().beatIndex();
    this->scheduleUpdate();
    return true;
}

void SideFlashes::flash(int side, float amplitude, float beatLength) {
    float target = std::clamp(0.1f + (amplitude - AMPLITUDE_DEAD_ZONE) / KIAI_MULTIPLIER, 0.1f, 1.f);
    m_alpha[side].to(target, FADE_IN_MS, Easing::None);
    m_fadeOutMs[side] = FADE_IN_MS;
    m_beatLength[side] = beatLength;
}

void SideFlashes::update(float dt) {
    float ms = dt * 1000.f;
    auto& audio = AudioAnalyzer::get();
    audio.update(dt);

    if (audio.beatIndex() != m_lastBeat) {
        m_lastBeat = audio.beatIndex();
        if (audio.isPlaying()) flash(m_lastBeat % 2, audio.amplitude(), audio.beatLength());
    }

    for (int side = 0; side < 2; side++) {
        if (m_fadeOutMs[side] >= 0) {
            m_fadeOutMs[side] -= ms;
            if (m_fadeOutMs[side] < 0) m_alpha[side].to(0.f, m_beatLength[side], Easing::In);
        }
        m_alpha[side].update(dt);
        m_boxes[side]->setOpacity(static_cast<GLubyte>(std::clamp(m_alpha[side].get(), 0.f, 1.f) * 255));
    }
}

} // namespace lazer
