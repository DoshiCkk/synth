#pragma once

#include "Smoother.h"
#include "Voice.h"

namespace neutron::dsp
{

// Все параметры синта в «физических» единицах. Заполняется раз в блок.
struct SynthParams
{
    Waveform oscAWave = Waveform::Saw;
    double oscALevel = 1.0;
    EnvelopeSettings ampEnv;
    double masterGain = 0.5; // линейный
};

// Пока один голос (шаг 2). Полифония и кража — на шаге 5.
class VoiceManager
{
public:
    void prepare(double sampleRate) noexcept
    {
        voice.prepare(sampleRate);
        oscALevel.prepare(sampleRate, kSmoothingSeconds);
        masterGain.prepare(sampleRate, kSmoothingSeconds);
        firstParams = true;
    }

    void reset() noexcept { voice.reset(); }

    void setParams(const SynthParams& p) noexcept
    {
        voice.setWaveform(p.oscAWave);
        voice.setAmpEnvelope(p.ampEnv);

        oscALevel.setTarget(p.oscALevel);
        masterGain.setTarget(p.masterGain);

        if (firstParams) // после prepare не «въезжаем» в значения из нуля
        {
            oscALevel.reset(p.oscALevel);
            masterGain.reset(p.masterGain);
            firstParams = false;
        }
    }

    void noteOn(int note, float /*velocity*/) noexcept { voice.noteOn(note); }

    void noteOff(int note) noexcept
    {
        if (voice.getNote() == note)
            voice.noteOff();
    }

    // Рендер в моно-буфер (перезаписывает). Вызывается кусками между MIDI-событиями.
    void render(float* out, int numSamples) noexcept
    {
        for (int i = 0; i < numSamples; ++i)
        {
            VoiceFrame frame;
            frame.oscALevel = static_cast<float>(oscALevel.next());
            const float gain = static_cast<float>(masterGain.next());

            const float sample = voice.isActive() ? voice.process(frame) : 0.0f;
            out[i] = sample * gain;
        }
    }

private:
    static constexpr double kSmoothingSeconds = 0.02;

    Voice voice;
    LinearSmoother oscALevel, masterGain;
    bool firstParams = true;
};

} // namespace neutron::dsp
