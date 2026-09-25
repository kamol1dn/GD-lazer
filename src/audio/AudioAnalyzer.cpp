#include "AudioAnalyzer.hpp"

#include <Geode/Geode.hpp>
#include <Geode/fmod/fmod.hpp>
#include <algorithm>

using namespace geode::prelude;

namespace lazer {

namespace {
    constexpr int FFT_WINDOW = AudioAnalyzer::BINS * 2;
    // Bass bins used for onset detection (~0-150 Hz at 44.1kHz / 512).
    constexpr int BASS_BINS = 4;
    // An onset must exceed the running bass average by this factor...
    constexpr float BEAT_THRESHOLD = 1.35f;
    // ...and be at least this far from the last one.
    constexpr float MIN_BEAT_GAP_MS = 280.f;
    // Calibrated against GD's menu loop so the visualiser reads like osu!'s.
    constexpr float SPECTRUM_GAIN = 3.f;
}

AudioAnalyzer& AudioAnalyzer::get() {
    static AudioAnalyzer instance;
    return instance;
}

bool AudioAnalyzer::attach() {
    auto engine = FMODAudioEngine::sharedEngine();
    if (!engine || !engine->m_system || !engine->m_backgroundMusicChannel) return false;

    auto group = engine->m_backgroundMusicChannel;
    if (m_group == group && m_fft) return true;

    // Group changed (or first run): drop the old DSP and attach a fresh one.
    if (m_fft) {
        if (m_group) static_cast<FMOD::ChannelGroup*>(m_group)->removeDSP(static_cast<FMOD::DSP*>(m_fft));
        static_cast<FMOD::DSP*>(m_fft)->release();
        m_fft = nullptr;
    }

    FMOD::DSP* dsp = nullptr;
    if (engine->m_system->createDSPByType(FMOD_DSP_TYPE_FFT, &dsp) != FMOD_OK || !dsp) return false;
    dsp->setParameterInt(FMOD_DSP_FFT_WINDOWSIZE, FFT_WINDOW);
    dsp->setParameterInt(FMOD_DSP_FFT_WINDOWTYPE, FMOD_DSP_FFT_WINDOW_HANNING);
    // FMOD_CHANNELCONTROL_DSP_TAIL: see the signal before the group's volume is applied,
    // so the visuals don't vanish when the music volume is turned down.
    if (group->addDSP(FMOD_CHANNELCONTROL_DSP_TAIL, dsp) != FMOD_OK) {
        dsp->release();
        return false;
    }
    dsp->setMeteringEnabled(true, false);

    m_group = group;
    m_fft = dsp;
    return true;
}

void AudioAnalyzer::update(float dt) {
    // Several nodes call this each frame; only process once per frame.
    auto frame = CCDirector::sharedDirector()->getTotalFrames();
    if (frame == m_lastFrame) return;
    m_lastFrame = frame;

    if (!attach()) {
        m_playing = false;
        m_amplitude = 0;
        return;
    }

    auto dsp = static_cast<FMOD::DSP*>(m_fft);

    // Peak level of the music, 0..1.
    FMOD_DSP_METERING_INFO meter {};
    float peak = 0;
    if (dsp->getMeteringInfo(&meter, nullptr) == FMOD_OK) {
        for (int c = 0; c < meter.numchannels; c++) peak = std::max(peak, meter.peaklevel[c]);
    }
    m_amplitude = std::clamp(peak, 0.f, 1.f);

    FMOD_DSP_PARAMETER_FFT* fft = nullptr;
    unsigned int len = 0;
    m_spectrum.fill(0);
    if (dsp->getParameterData(FMOD_DSP_FFT_SPECTRUMDATA, reinterpret_cast<void**>(&fft), &len, nullptr, 0) == FMOD_OK
        && fft && fft->numchannels > 0) {
        int bins = std::min(fft->length / 2, BINS);
        for (int i = 0; i < bins; i++) {
            float v = 0;
            for (int c = 0; c < fft->numchannels; c++) v = std::max(v, fft->spectrum[c][i]);
            // FMOD's FFT is much quieter than BASS's (which osu! uses), and music
            // has far less energy up high: apply gain plus a tilt towards the treble.
            m_spectrum[i] = std::min(1.f, v * SPECTRUM_GAIN * (1.f + i / 48.f));
        }
    }

    m_playing = m_amplitude > 0.001f;

    // Onset detection on bass energy.
    float ms = dt * 1000.f;
    m_sinceBeatMs += ms;
    float bass = 0;
    for (int i = 0; i < BASS_BINS; i++) bass += m_spectrum[i];
    bass /= BASS_BINS;

    bool beat = m_playing
        && bass > m_bassAverage * BEAT_THRESHOLD
        && bass > 0.06f
        && m_sinceBeatMs > MIN_BEAT_GAP_MS;

    // Slow running average (~0.5s) of bass energy.
    m_bassAverage += (bass - m_bassAverage) * std::min(1.f, ms / 500.f);

    if (beat) {
        // Track the beat length, ignoring gaps that are clearly missed beats.
        if (m_sinceBeatMs < 1200.f) {
            m_beatLength += (m_sinceBeatMs - m_beatLength) * 0.3f;
        }
        m_sinceBeatMs = 0;
        m_beatIndex++;
    }
}

} // namespace lazer
