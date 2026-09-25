#include "Sfx.hpp"

#include <Geode/Geode.hpp>
#include <Geode/binding/FMODAudioEngine.hpp>

#include <chrono>
#include <random>
#include <string>
#include <unordered_map>

using namespace geode::prelude;

namespace lazer::sfx {

namespace {
    constexpr double DEBOUNCE_MS = 20; // OsuGameBase.SAMPLE_DEBOUNCE_TIME

    double nowMs() {
        using namespace std::chrono;
        return duration<double, std::milli>(steady_clock::now().time_since_epoch()).count();
    }

    std::string const& pathFor(char const* name) {
        static std::unordered_map<std::string, std::string> cache;
        auto it = cache.find(name);
        if (it == cache.end()) {
            auto path = Mod::get()->getResourcesDir() / (std::string(name) + ".ogg");
            it = cache.emplace(name, utils::string::pathToString(path)).first;
        }
        return it->second;
    }

    // Plays unless the same key played within the debounce window.
    bool debounce(char const* key) {
        static std::unordered_map<std::string, double> last;
        double now = nowMs();
        auto [it, inserted] = last.try_emplace(key, now);
        if (!inserted) {
            if (now - it->second < DEBOUNCE_MS) return false;
            it->second = now;
        }
        return true;
    }

    float randomBetween(float lo, float hi) {
        static std::mt19937 rng {std::random_device {}()};
        return std::uniform_real_distribution<float>(lo, hi)(rng);
    }
}

void play(char const* name, float pitchVariation, float frequency) {
    float volume = Mod::get()->getSettingValue<int64_t>("ui-sound-volume") / 100.f;
    if (volume <= 0.f || !debounce(name)) return;

    if (pitchVariation > 0.f) frequency *= randomBetween(1.f - pitchVariation, 1.f + pitchVariation);
    FMODAudioEngine::sharedEngine()->playEffect(pathFor(name), frequency, 0.f, volume);
}

void hover(char const* name) {
    // HoverSampleDebounceComponent shares one timestamp across every hover sound.
    static char const* const HOVER_KEY = "hover";
    if (!debounce(HOVER_KEY)) return;
    play(name, 0.02f);
}

void click(char const* name) {
    play(name, 0.01f);
}

} // namespace lazer::sfx
