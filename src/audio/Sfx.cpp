#include "Sfx.hpp"

#include <Geode/Geode.hpp>
#include <Geode/binding/FMODAudioEngine.hpp>
#include <Geode/fmod/fmod.hpp>

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

    // The menu's sounds get their own channel group, not GD's SFX one: their
    // volume is the "UI sound volume" setting alone, so they still play with
    // GD's SFX turned off.
    FMOD::ChannelGroup* group() {
        static FMOD::ChannelGroup* group = nullptr;
        if (!group) {
            auto engine = FMODAudioEngine::sharedEngine();
            if (!engine || !engine->m_system) return nullptr;
            if (engine->m_system->createChannelGroup("lazer-ui", &group) != FMOD_OK) group = nullptr;
        }
        return group;
    }

    FMOD::Sound* soundFor(char const* name) {
        static std::unordered_map<std::string, FMOD::Sound*> cache;
        auto it = cache.find(name);
        if (it != cache.end()) return it->second;
        auto engine = FMODAudioEngine::sharedEngine();
        if (!engine || !engine->m_system) return nullptr;
        FMOD::Sound* sound = nullptr;
        if (engine->m_system->createSound(pathFor(name).c_str(), FMOD_DEFAULT | FMOD_CREATESAMPLE, nullptr, &sound) != FMOD_OK) sound = nullptr;
        cache.emplace(name, sound);
        return sound;
    }

    void playOnGroup(char const* name, float frequency, float volume) {
        auto sound = soundFor(name);
        auto target = group();
        if (!sound || !target) return;
        FMOD::Channel* channel = nullptr;
        if (FMODAudioEngine::sharedEngine()->m_system->playSound(sound, target, true, &channel) != FMOD_OK || !channel) return;
        channel->setVolume(volume);
        channel->setPitch(frequency);
        channel->setPaused(false);
    }

    float uiVolume() {
        return Mod::get()->getSettingValue<int64_t>("ui-sound-volume") / 100.f;
    }

    float randomBetween(float lo, float hi) {
        static std::mt19937 rng {std::random_device {}()};
        return std::uniform_real_distribution<float>(lo, hi)(rng);
    }
}

void play(char const* name, float pitchVariation, float frequency) {
    float volume = uiVolume();
    if (volume <= 0.f || !debounce(name)) return;

    if (pitchVariation > 0.f) frequency *= randomBetween(1.f - pitchVariation, 1.f + pitchVariation);
    playOnGroup(name, frequency, volume);
}

void playCue(char const* name) {
    float volume = uiVolume();
    if (volume > 0.f) playOnGroup(name, 1.f, volume);
}

void preloadCue(char const* name) {
    if (uiVolume() <= 0.f) return;
    soundFor(name);
    group();
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
