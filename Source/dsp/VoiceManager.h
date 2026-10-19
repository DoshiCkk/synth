#pragma once

#include <array>
#include <cmath>

#include "Smoother.h"
#include "Voice.h"

namespace neutron::dsp
{

struct OscSettings
{
    Waveform wave = Waveform::Saw;
    int octave = 0;          // −2..+2
    int semitone = 0;        // −12..+12
    double fineCents = 0.0;  // ±50
    double pulseWidth = 0.5; // 0..1
    double level = 1.0;      // 0..1
};

// Все параметры синта в «физических» единицах. Заполняется раз в блок.
struct SynthParams
{
    OscSettings oscA;
    OscSettings oscB;

    double cutoffHz = 20000.0;
    double resonance = 0.0;       // 0..1
    double filterEnvAmount = 0.0; // −1..+1
    double keyTrack = 0.0;        // 0..1
    double velToFilterEnv = 0.0;  // 0..1
    EnvelopeSettings filterEnv;

    EnvelopeSettings ampEnv;
    double masterGain = 0.5; // линейный
};

// Пока один голос (шаг 4). Полифония и кража — на шаге 5.
class VoiceManager
{
public:
    void prepare(double sampleRate) noexcept
    {
        voice.prepare(sampleRate);
        voice.setInitialPhases(0.0, 0.37);
        for (auto* s : smoothers())
            s->prepare(sampleRate, kSmoothingSeconds);
        firstParams = true;
    }

    void reset() noexcept { voice.reset(); }

    void setParams(const SynthParams& p) noexcept
    {
        voice.setWaveforms(p.oscA.wave, p.oscB.wave);
        voice.setEnvelopes(p.filterEnv, p.ampEnv);

        // Октава и полутоны переключаются мгновенно (как галетный переключатель),
        // плавно идёт только fine — иначе смена октавы звучала бы как мини-глайд.
        oscACoarse = p.oscA.octave * 12 + p.oscA.semitone;
        oscBCoarse = p.oscB.octave * 12 + p.oscB.semitone;

        oscAFine.setTarget(p.oscA.fineCents / 100.0);
        oscBFine.setTarget(p.oscB.fineCents / 100.0);
        oscAPulseWidth.setTarget(p.oscA.pulseWidth);
        oscBPulseWidth.setTarget(p.oscB.pulseWidth);
        oscALevel.setTarget(p.oscA.level);
        oscBLevel.setTarget(p.oscB.level);

        // Срез сглаживаем в октавах (log2), а не в Гц: иначе рампа 100 Гц → 10 кГц
        // почти всё время провела бы в верхней октаве.
        cutoffLog2.setTarget(std::log2(p.cutoffHz));
        resonance.setTarget(p.resonance);
        filterEnvAmount.setTarget(p.filterEnvAmount);
        keyTrack.setTarget(p.keyTrack);
        velToFilterEnv.setTarget(p.velToFilterEnv);

        masterGain.setTarget(p.masterGain);

        if (firstParams) // после prepare не «въезжаем» в значения из нуля
        {
            for (auto* s : smoothers())
                s->reset(s->getTarget());
            firstParams = false;
        }
    }

    void noteOn(int note, float velocity) noexcept { voice.noteOn(note, velocity); }

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
            frame.oscAPitchOffset = oscACoarse + oscAFine.next();
            frame.oscBPitchOffset = oscBCoarse + oscBFine.next();
            frame.oscAPulseWidth = oscAPulseWidth.next();
            frame.oscBPulseWidth = oscBPulseWidth.next();
            frame.oscALevel = static_cast<float>(oscALevel.next());
            frame.oscBLevel = static_cast<float>(oscBLevel.next());
            frame.cutoffLog2 = cutoffLog2.next();
            frame.resonance = resonance.next();
            frame.filterEnvAmount = filterEnvAmount.next();
            frame.keyTrack = keyTrack.next();
            frame.velToFilterEnv = velToFilterEnv.next();
            const float gain = static_cast<float>(masterGain.next());

            const float sample = voice.isActive() ? voice.process(frame) : 0.0f;
            out[i] = sample * gain;
        }
    }

private:
    static constexpr double kSmoothingSeconds = 0.02;

    std::array<LinearSmoother*, 12> smoothers() noexcept
    {
        return { &oscAFine,   &oscBFine,  &oscAPulseWidth,  &oscBPulseWidth, &oscALevel,      &oscBLevel,
                 &cutoffLog2, &resonance, &filterEnvAmount, &keyTrack,       &velToFilterEnv, &masterGain };
    }

    Voice voice;
    int oscACoarse = 0, oscBCoarse = 0;
    LinearSmoother oscAFine, oscBFine, oscAPulseWidth, oscBPulseWidth, oscALevel, oscBLevel;
    LinearSmoother cutoffLog2, resonance, filterEnvAmount, keyTrack, velToFilterEnv;
    LinearSmoother masterGain;
    bool firstParams = true;
};

} // namespace neutron::dsp
