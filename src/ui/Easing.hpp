#pragma once

#include <algorithm>
#include <cmath>

// Easing curves and frame-rate independent damping, modelled on osu-framework
// (osu.Framework.Graphics.Transforms.DefaultEasingFunction / Utils.Interpolation, MIT).
namespace lazer {

enum class Easing {
    None,
    Out,
    OutQuad,
    OutCubic,
    OutQuint,
    OutExpo,
    OutElastic,
    OutElasticHalf,
    In,
    InSine,
    OutSine,
    InOutSine,
    InQuint,
    InOutQuint,
};

namespace detail {
    constexpr double PI = 3.14159265358979323846;
    constexpr double ELASTIC_CONST = 2 * PI / .3;
    constexpr double ELASTIC_CONST2 = .3 / 4;

    // Offsets that pin expo / elastic curves to exactly 0 and 1 at the ends.
    inline const double EXPO_OFFSET = std::pow(2, -10);
    inline const double ELASTIC_OFFSET_FULL = std::pow(2, -11);
    inline const double ELASTIC_OFFSET_HALF = std::pow(2, -10) * std::sin((.5 - ELASTIC_CONST2) * ELASTIC_CONST);
}

// t in [0, 1] -> eased progress (may overshoot for elastic curves).
inline double ease(Easing e, double t) {
    using namespace detail;
    t = std::clamp(t, 0.0, 1.0);
    switch (e) {
        case Easing::None: return t;
        case Easing::Out:
        case Easing::OutQuad: return t * (2 - t);
        case Easing::OutCubic: { t -= 1; return t * t * t + 1; }
        case Easing::OutQuint: { t -= 1; return t * t * t * t * t + 1; }
        case Easing::OutExpo: return -std::pow(2, -10 * t) + 1 + EXPO_OFFSET * t;
        case Easing::OutElastic:
            return std::pow(2, -10 * t) * std::sin((t - ELASTIC_CONST2) * ELASTIC_CONST) + 1
                - ELASTIC_OFFSET_FULL * t;
        case Easing::OutElasticHalf:
            return std::pow(2, -10 * t) * std::sin((.5 * t - ELASTIC_CONST2) * ELASTIC_CONST) + 1
                - ELASTIC_OFFSET_HALF * t;
        case Easing::In: return t * t;
        case Easing::InSine: return 1 - std::cos(t * PI * .5);
        case Easing::OutSine: return std::sin(t * PI * .5);
        case Easing::InOutSine: return .5 - .5 * std::cos(PI * t);
        case Easing::InQuint: return t * t * t * t * t;
        case Easing::InOutQuint:
            if (t < .5) return 16 * t * t * t * t * t;
            t = t - 1;
            return 16 * t * t * t * t * t + 1;
    }
    return t;
}

// Exponential approach towards `target`, independent of frame rate.
// Same semantics as osu's Interpolation.Damp(current, target, base, elapsedMs):
// each millisecond, the remaining distance is multiplied by `base` (e.g. 0.9 = fast, 0.995 = slow).
template <class T>
T damp(T current, T target, double base, double elapsedMs) {
    double k = 1.0 - std::pow(base, elapsedMs);
    return current + (target - current) * static_cast<float>(k);
}

// A value that can be tweened with an easing curve (like osu's TransformTo),
// advanced manually each frame via update(dtSeconds).
template <class T = float>
class Tweened {
public:
    Tweened() = default;
    Tweened(T v) : m_from(v), m_to(v), m_value(v) {}

    void to(T target, double durationMs, Easing easing = Easing::OutQuint) {
        m_from = m_value;
        m_to = target;
        m_elapsed = 0;
        m_duration = durationMs;
        m_easing = easing;
        if (durationMs <= 0) m_value = target;
    }

    void set(T v) { m_from = m_to = m_value = v; m_duration = 0; }

    // Returns true while still animating.
    bool update(float dtSeconds) {
        if (m_duration <= 0 || m_elapsed >= m_duration) { m_value = m_to; return false; }
        m_elapsed += dtSeconds * 1000.0;
        double p = ease(m_easing, m_elapsed / m_duration);
        m_value = m_from + (m_to - m_from) * static_cast<float>(p);
        return m_elapsed < m_duration;
    }

    T get() const { return m_value; }
    T target() const { return m_to; }
    operator T() const { return m_value; }

private:
    T m_from {};
    T m_to {};
    T m_value {};
    double m_elapsed = 0;
    double m_duration = 0;
    Easing m_easing = Easing::OutQuint;
};

} // namespace lazer
