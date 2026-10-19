#pragma once

#include <cmath>

#include "Envelope.h"
#include "LadderFilter.h"
#include "Oscillator.h"

namespace neutron::dsp
{

// Значения параметров на текущий сэмпл — уже сглаженные VoiceManager'ом.
// Общие для всех голосов, поэтому считаются один раз на сэмпл, а не на голос.
struct VoiceFrame
{
    double oscAPitchOffset = 0.0; // полутона: октава·12 + полутоны + fine/100
    double oscBPitchOffset = 0.0;
    double oscAPulseWidth = 0.5;
    double oscBPulseWidth = 0.5;
    float oscALevel = 1.0f;
    float oscBLevel = 0.0f;

    double cutoffLog2 = 14.0;     // log2(Гц): модуляции складываются в октавах
    double resonance = 0.0;       // 0..1
    double filterEnvAmount = 0.0; // −1..+1
    double keyTrack = 0.0;        // 0..1
    double velToFilterEnv = 0.0;  // 0..1
};

// Один голос: Osc A + Osc B → микс → фильтр → VCA (Amp Env).
class Voice
{
public:
    // Env amount = ±1 сдвигает срез на ±8 октав: от 20 Гц огибающая открывает почти весь диапазон.
    static constexpr double kFilterEnvRangeOctaves = 8.0;
    static constexpr int kKeyTrackCenterNote = 60; // C4: на этой ноте key tracking не меняет срез

    void prepare(double sampleRate) noexcept
    {
        oscA.prepare(sampleRate);
        oscB.prepare(sampleRate);
        filter.prepare(sampleRate);
        filterEnv.prepare(sampleRate);
        ampEnv.prepare(sampleRate);
    }

    void setInitialPhases(double a, double b) noexcept
    {
        oscA.setPhase(a);
        oscB.setPhase(b);
    }

    void setWaveforms(Waveform a, Waveform b) noexcept
    {
        oscA.setWaveform(a);
        oscB.setWaveform(b);
    }

    void setEnvelopes(const EnvelopeSettings& filterSettings, const EnvelopeSettings& ampSettings) noexcept
    {
        filterEnv.setSettings(filterSettings);
        ampEnv.setSettings(ampSettings);
    }

    void noteOn(int midiNote, float noteVelocity) noexcept
    {
        note = midiNote;
        velocity = noteVelocity;
        filterEnv.noteOn(); // обе огибающие — с текущего уровня, retrigger без щелчка
        ampEnv.noteOn();
    }

    void noteOff() noexcept
    {
        filterEnv.noteOff();
        ampEnv.noteOff();
    }

    void reset() noexcept
    {
        filter.reset();
        filterEnv.reset();
        ampEnv.reset();
    }

    bool isActive() const noexcept { return ampEnv.isActive(); }
    int getNote() const noexcept { return note; }

    float process(const VoiceFrame& f) noexcept
    {
        const double pitchMod = 0.0; // сюда на этапе 2 придёт Poly-Mod
        const float a = oscA.process(note + f.oscAPitchOffset, pitchMod, f.oscAPulseWidth);
        const float b = oscB.process(note + f.oscBPitchOffset, pitchMod, f.oscBPulseWidth);

        // ×0.5: два осциллятора на полном уровне дают ±1, а не ±2 — запас перед фильтром.
        const float mix = 0.5f * (a * f.oscALevel + b * f.oscBLevel);

        // Срез считаем в октавах: огибающая и key tracking сдвигают его музыкально
        // (одинаково слышно на 200 Гц и на 5 кГц), потом один раз переводим в Гц.
        const double envAmount = f.filterEnvAmount * (1.0 - f.velToFilterEnv * (1.0 - velocity));
        const double cutoffOctaves = f.cutoffLog2
                                   + f.keyTrack * (note - kKeyTrackCenterNote) / 12.0
                                   + envAmount * filterEnv.process() * kFilterEnvRangeOctaves;
        const float filtered = filter.process(mix, std::exp2(cutoffOctaves), f.resonance);

        const float out = filtered * static_cast<float>(ampEnv.process());

        // Amp Env закончился — голос молчит. Сбрасываем фильтр и огибающую фильтра,
        // чтобы следующая нота на этом голосе начиналась с чистого состояния.
        if (!ampEnv.isActive())
            reset();

        return out;
    }

private:
    Oscillator oscA, oscB;
    LadderFilter filter;
    Envelope filterEnv, ampEnv;
    int note = 60;
    float velocity = 1.0f;
};

} // namespace neutron::dsp
