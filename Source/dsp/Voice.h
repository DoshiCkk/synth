#pragma once

#include "Envelope.h"
#include "Oscillator.h"

namespace neutron::dsp
{

// Значения параметров на текущий сэмпл — уже сглаженные VoiceManager'ом.
// Общие для всех голосов, поэтому считаются один раз на сэмпл, а не на голос.
struct VoiceFrame
{
    float oscALevel = 1.0f;
};

// Один голос: Osc A → VCA (Amp Env).
class Voice
{
public:
    void prepare(double sampleRate) noexcept
    {
        oscA.prepare(sampleRate);
        ampEnv.prepare(sampleRate);
    }

    void setWaveform(Waveform a) noexcept { oscA.setWaveform(a); }
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
        const float osc = oscA.process(note, pitchMod) * f.oscALevel;
        return osc * static_cast<float>(ampEnv.process());
    }

private:
    Oscillator oscA;
    Envelope ampEnv;
    int note = 60;
};

} // namespace neutron::dsp
