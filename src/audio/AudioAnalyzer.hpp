#pragma once

#include <array>

namespace lazer {

// Listens to whatever GD is playing on its music channel group (so it also
// follows songs swapped in by mods like Menu Loop Randomizer) and exposes:
//  - peak amplitude (like osu's ChannelAmplitudes.Maximum)
//  - a 256-bin spectrum (like ChannelAmplitudes.FrequencyAmplitudes)
//  - detected beats (GD menu songs have no timing points, so we detect onsets)
class AudioAnalyzer {
public:
    static constexpr int BINS = 256;

    static AudioAnalyzer& get();

    // Call once per frame; cheap and safe to call from several nodes (runs once per frame).
    void update(float dt);

    float amplitude() const { return m_amplitude; }
    std::array<float, BINS> const& spectrum() const { return m_spectrum; }

    // Incremented on every detected beat. Compare with a stored value to react.
    int beatIndex() const { return m_beatIndex; }
    // Estimated time between beats, in ms.
    float beatLength() const { return m_beatLength; }
    bool isPlaying() const { return m_playing; }

private:
    AudioAnalyzer() = default;
    bool attach();

    void* m_group = nullptr; // FMOD::ChannelGroup*
    void* m_fft = nullptr;   // FMOD::DSP*
    unsigned int m_lastFrame = 0;

    float m_amplitude = 0;
    std::array<float, BINS> m_spectrum {};
    bool m_playing = false;

    float m_bassAverage = 0;
    float m_sinceBeatMs = 0;
    float m_beatLength = 500;
    int m_beatIndex = 0;
};

} // namespace lazer
