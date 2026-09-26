#include "Parallax.hpp"

#include "Easing.hpp"
#include "Tilt.hpp"

#include <Geode/loader/Mod.hpp>
#include <Geode/utils/cocos.hpp>
#include <algorithm>
#include <cmath>

using namespace cocos2d;

namespace lazer {

namespace {
    // ParallaxContainer's easing times.
    constexpr float DURATION = 100.f;
    constexpr float SCALE_DURATION = 1000.f;
    // Tilt (roughly sin of the angle) to input, before tanh: about 20 degrees
    // reaches the edge, and tanh keeps it from ever going past it.
    constexpr float TILT_GAIN = 3.f;
}

CCPoint Parallax::input(float dt, CCSize const& size) {
    // Every parallax layer reads the same input once per frame (tilt keeps state).
    static unsigned int s_frame = ~0u;
    static CCPoint s_input {0, 0};
    auto frame = CCDirector::sharedDirector()->getTotalFrames();
    if (frame == s_frame) return s_input;
    s_frame = frame;

#ifdef GEODE_IS_MOBILE
    // Without tilt, the "mouse" is just wherever the last tap was: hold still.
    if (!geode::Mod::get()->getSettingValue<bool>("tilt-parallax")) {
        s_input = CCPoint(0, 0);
        return s_input;
    }
#endif
    if (auto tilt = tilt::get(dt)) {
        // Phones: things slide towards whichever edge dips, like they're resting on the screen.
        s_input = CCPoint(std::tanh(tilt->x * TILT_GAIN), std::tanh(tilt->y * TILT_GAIN));
    } else {
        auto half = size / 2;
        auto mouse = geode::cocos::getMousePos() - half;
        // Its falloff is in osu! pixels (768 tall); convert from GD units.
        float toOsu = 768.f / size.height;
        auto soft = [toOsu](float v) {
            float x = std::abs(v) * toOsu;
            return std::copysign(1.f - std::pow(0.999f, x), v);
        };
        s_input = CCPoint(soft(mouse.x), soft(mouse.y));
    }
    return s_input;
}

void Parallax::update(float dt, CCSize const& size) {
    float ms = dt * 1000.f;
    float amount = static_cast<float>(geode::Mod::get()->getSettingValue<double>(m_setting)) / 100.f;
    auto in = input(dt, size);
    CCPoint target {in.x * size.width / 2 * amount, in.y * size.height / 2 * amount};
    float t = static_cast<float>(ease(Easing::OutQuint, std::min(ms, DURATION) / DURATION));
    m_offset = m_offset + (target - m_offset) * t;
    float ts = static_cast<float>(ease(Easing::OutQuint, std::min(ms, SCALE_DURATION) / SCALE_DURATION));
    m_scale += (1.f + amount - m_scale) * ts;
}

} // namespace lazer
