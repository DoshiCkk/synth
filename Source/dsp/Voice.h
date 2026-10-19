#pragma once

#include "Envelope.h"
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
};

// Один голос: Osc A + Osc B → микс → VCA (Amp Env).
class Voice
{
public:
    void prepare(double sampleRate) noexcept
    {
        oscA.prepare(sampleRate);
        oscB.prepare(sampleRate);
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

    void setAmpEnvelope(const EnvelopeSettings& s) noexcept { ampEnv.setSettings(s); }

    void noteOn(int midiNote) noexcept
    {
        note = midiNote;
        ampEnv.noteOn(); // с текущего уровня — retrigger без щелчка
    }

    void noteOff() noexcept { ampEnv.noteOff(); }
    void reset() noexcept { ampEnv.reset(); }

    bool isActive() const noexcept { return ampEnv.isActive(); }
    int getNote() const noexcept { return note; }

    float process(const VoiceFrame& f) noexcept
    {
        const double pitchMod = 0.0; // сюда на этапе 2 придёт Poly-Mod
        const float a = oscA.process(note + f.oscAPitchOffset, pitchMod, f.oscAPulseWidth);
        const float b = oscB.process(note + f.oscBPitchOffset, pitchMod, f.oscBPulseWidth);

        // ×0.5: два осциллятора на полном уровне дают ±1, а не ±2 — запас перед фильтром.
        const float mix = 0.5f * (a * f.oscALevel + b * f.oscBLevel);
        return mix * static_cast<float>(ampEnv.process());
    }

private:
    Oscillator oscA, oscB;
    Envelope ampEnv;
    int note = 60;
};

} // namespace neutron::dsp
