#include "Tilt.hpp"

#ifdef GEODE_IS_ANDROID

#include <Geode/loader/Log.hpp>
#include <android/looper.h>
#include <android/sensor.h>
#include <cmath>

using namespace cocos2d;

namespace lazer::tilt {

namespace {
    constexpr int LOOPER_ID = 3;
    constexpr int32_t SAMPLE_US = 1000000 / 60;
    // How quickly the resting angle follows the phone, in seconds.
    constexpr float REST_TIME = 2.f;
    // Smoothing on the raw reading (the gravity sensor is already smooth; the
    // accelerometer fallback shakes with every tap).
    constexpr float SMOOTH_TIME = 0.08f;
    // |x| (m/s^2) along the phone's short axis before we believe it's been
    // turned around to the other landscape.
    constexpr float FLIP_THRESHOLD = 4.f;

    int g_users = 0;
    ASensorManager* g_manager = nullptr;
    ASensor const* g_sensor = nullptr;
    ASensorEventQueue* g_queue = nullptr;
    bool g_failed = false;

    bool g_hasReading = false;
    float g_gravity[3] {0, 0, 0}; // smoothed, in the phone's own axes
    bool g_hasRest = false;
    CCPoint g_rest {0, 0};
    float g_side = 1.f; // +1: the phone's +x (its right edge, held upright) points up

    bool start() {
        if (g_queue) return true;
        if (g_failed) return false;
        // getInstanceForPackage needs API 26; the mod targets 23.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
        g_manager = ASensorManager_getInstance();
#pragma clang diagnostic pop
        if (g_manager) {
            g_sensor = ASensorManager_getDefaultSensor(g_manager, ASENSOR_TYPE_GRAVITY);
            if (!g_sensor) g_sensor = ASensorManager_getDefaultSensor(g_manager, ASENSOR_TYPE_ACCELEROMETER);
        }
        // Polled from the GL thread each frame: it needs a looper to attach the queue to.
        auto looper = ALooper_prepare(ALOOPER_PREPARE_ALLOW_NON_CALLBACKS);
        if (g_sensor && looper) g_queue = ASensorManager_createEventQueue(g_manager, looper, LOOPER_ID, nullptr, nullptr);
        if (!g_queue) {
            g_failed = true;
            geode::log::info("Tilt: no gravity sensor or accelerometer, background follows touches");
            return false;
        }
        ASensorEventQueue_enableSensor(g_queue, g_sensor);
        ASensorEventQueue_setEventRate(g_queue, g_sensor, SAMPLE_US);
        geode::log::info("Tilt: using {}", ASensor_getName(g_sensor));
        return true;
    }

    void stop() {
        if (!g_queue) return;
        ASensorEventQueue_disableSensor(g_queue, g_sensor);
        ASensorManager_destroyEventQueue(g_manager, g_queue);
        g_queue = nullptr;
        g_hasReading = false;
        g_hasRest = false;
    }
}

void acquire() {
    if (g_users++ == 0) start();
}

void release() {
    if (g_users > 0 && --g_users == 0) stop();
}

std::optional<CCPoint> get(float dt) {
    if (!g_queue) return std::nullopt;

    ASensorEvent events[16];
    ssize_t count;
    while ((count = ASensorEventQueue_getEvents(g_queue, events, 16)) > 0) {
        for (ssize_t i = 0; i < count; i++) {
            auto& v = events[i].vector.v;
            if (!g_hasReading) {
                std::copy(v, v + 3, g_gravity);
                g_hasReading = true;
                continue;
            }
            float a = 1.f - std::exp(-(SAMPLE_US / 1e6f) / SMOOTH_TIME);
            for (int j = 0; j < 3; j++) g_gravity[j] += (v[j] - g_gravity[j]) * a;
        }
    }
    if (!g_hasReading) return CCPoint {0, 0};

    float len = std::sqrt(g_gravity[0] * g_gravity[0] + g_gravity[1] * g_gravity[1] + g_gravity[2] * g_gravity[2]);
    if (len < 1.f) return CCPoint {0, 0};

    // GD runs in landscape, either way round. The sensor reports "up" (it
    // measures the push against gravity): whichever end of the phone's long
    // side points up says which landscape it is. Lying flat, keep the last one.
    if (std::abs(g_gravity[0]) > FLIP_THRESHOLD) {
        float side = g_gravity[0] > 0 ? 1.f : -1.f;
        if (side != g_side) g_hasRest = false; // turned around: start fresh
        g_side = side;
    }
    // "Up" in the screen's axes, as a fraction of g.
    CCPoint up {-g_side * g_gravity[1] / len, g_side * g_gravity[0] / len};

    if (!g_hasRest) {
        g_rest = up;
        g_hasRest = true;
    }
    float r = 1.f - std::exp(-dt / REST_TIME);
    g_rest = g_rest + (up - g_rest) * r;

    // Tilting the right edge down turns the screen's right axis downwards, so
    // "up" gains a negative x: flip it to get which way the phone is dipping.
    return CCPoint {-(up.x - g_rest.x), -(up.y - g_rest.y)};
}

} // namespace lazer::tilt

#else

namespace lazer::tilt {
void acquire() {}
void release() {}
std::optional<cocos2d::CCPoint> get(float) { return std::nullopt; }
} // namespace lazer::tilt

#endif
