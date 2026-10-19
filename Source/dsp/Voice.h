#pragma once

#include <cmath>
#include <cstdint>

#include "Envelope.h"
#include "LadderFilter.h"
#include "Oscillator.h"

namespace neutron::dsp
{

// Значения параметров на текущий сэмпл — уже сглаженные VoiceManager'ом.
// Общие для всех голосов, поэтому считаются один раз на сэмпл, а не на голос.
struct VoiceFrame
{
    double pitchBend = 0.0;       // полутона
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
    double velToAmp = 0.0;        // 0..1
};

// Один голос: Osc A + Osc B → микс → фильтр → VCA (Amp Env).
class Voice
{
public:
    // Env amount = ±1 сдвигает срез на ±8 октав: от 20 Гц огибающая открывает почти весь диапазон.
    static constexpr double kFilterEnvRangeOctaves = 8.0;
    static constexpr int kKeyTrackCenterNote = 60; // C4: на этой ноте key tracking не меняет срез
    static constexpr double kStealFadeSeconds = 0.004;

    void prepare(double sampleRate) noexcept
    {
        oscA.prepare(sampleRate);
        oscB.prepare(sampleRate);
        filter.prepare(sampleRate);
        filterEnv.prepare(sampleRate);
        ampEnv.prepare(sampleRate);
        fadeStep = 1.0f / static_cast<float>(sampleRate * kStealFadeSeconds);
        reset();
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

    // Note-on на свободный голос или retrigger звучащего: огибающие идут с текущего уровня.
    void start(int midiNote, float noteVelocity, std::uint64_t order) noexcept
    {
        noteOnOrder = order;
        if (fading) // голос ещё затухает (кража или All Sound Off) — нота стартует после затухания
        {
            pendingNote = midiNote;
            pendingVelocity = noteVelocity;
            pendingReleased = false;
            hasPending = true;
            return;
        }
        note = midiNote;
        velocity = noteVelocity;
        gate = true;
        filterEnv.noteOn();
        ampEnv.noteOn();
    }

    // Кража: старая нота затухает за kStealFadeSeconds, после чего стартует новая.
    // Мгновенно переключить ноту нельзя — оборванная на полуволне форма даст щелчок.
    void steal(int midiNote, float noteVelocity, std::uint64_t order) noexcept
    {
        noteOnOrder = order;
        pendingNote = midiNote;
        pendingVelocity = noteVelocity;
        pendingReleased = false;
        hasPending = true;
        fading = true;
    }

    void release(std::uint64_t order) noexcept
    {
        releaseOrder = order;
        if (fading && hasPending)
        {
            pendingReleased = true; // нота отпущена раньше, чем успела начаться
            return;
        }
        gate = false;
        filterEnv.noteOff();
        ampEnv.noteOff();
    }

    // Быстрое затухание без следующей ноты (All Sound Off).
    void fadeOut() noexcept
    {
        hasPending = false;
        fading = true;
        gate = false;
    }

    void reset() noexcept
    {
        filter.reset();
        filterEnv.reset();
        ampEnv.reset();
        gate = false;
        fading = false;
        hasPending = false;
        fadeGain = 1.0f;
    }

    bool isActive() const noexcept { return ampEnv.isActive() || fading; }
    bool isStealing() const noexcept { return fading; }
    bool isGateOn() const noexcept { return (fading && hasPending) ? !pendingReleased : gate; }
    int getNote() const noexcept { return (fading && hasPending) ? pendingNote : note; }
    std::uint64_t getNoteOnOrder() const noexcept { return noteOnOrder; }
    std::uint64_t getReleaseOrder() const noexcept { return releaseOrder; }

    float process(const VoiceFrame& f) noexcept
    {
        const double pitch = note + f.pitchBend;
        const double pitchMod = 0.0; // сюда на этапе 2 придёт Poly-Mod
        const float a = oscA.process(pitch + f.oscAPitchOffset, pitchMod, f.oscAPulseWidth);
        const float b = oscB.process(pitch + f.oscBPitchOffset, pitchMod, f.oscBPulseWidth);

        // ×0.5: два осциллятора на полном уровне дают ±1, а не ±2 — запас перед фильтром.
        const float mix = 0.5f * (a * f.oscALevel + b * f.oscBLevel);

        // Срез считаем в октавах: огибающая и key tracking сдвигают его музыкально
        // (одинаково слышно на 200 Гц и на 5 кГц), потом один раз переводим в Гц.
        const double envAmount = f.filterEnvAmount * (1.0 - f.velToFilterEnv * (1.0 - velocity));
        const double cutoffOctaves = f.cutoffLog2
                                   + f.keyTrack * (pitch - kKeyTrackCenterNote) / 12.0
                                   + envAmount * filterEnv.process() * kFilterEnvRangeOctaves;
        const float filtered = filter.process(mix, std::exp2(cutoffOctaves), f.resonance);

        // Velocity → громкость. Кривая v²: velocity 64 при полной чувствительности ≈ −12 дБ,
        // на слух ровнее линейной (у которой половина хода клавиши — это всего −6 дБ).
        const double velGain = 1.0 - f.velToAmp * (1.0 - static_cast<double>(velocity) * velocity);
        float out = filtered * static_cast<float>(ampEnv.process() * velGain);

        if (fading)
        {
            out *= fadeGain;
            fadeGain -= fadeStep;
            if (fadeGain <= 0.0f)
                finishFade();
        }
        else if (!ampEnv.isActive())
        {
            // Amp Env закончился — голос молчит. Чистим фильтр и огибающую фильтра,
            // чтобы следующая нота на этом голосе начиналась с нуля.
            reset();
        }
        return out;
    }

private:
    void finishFade() noexcept
    {
        const bool startNext = hasPending;
        reset(); // выход уже в нуле — можно обнулить всё без щелчка
        if (!startNext)
            return;

        note = pendingNote;
        velocity = pendingVelocity;
        gate = true;
        filterEnv.noteOn();
        ampEnv.noteOn();
        if (pendingReleased)
        {
            gate = false;
            filterEnv.noteOff();
            ampEnv.noteOff();
        }
    }

    Oscillator oscA, oscB;
    LadderFilter filter;
    Envelope filterEnv, ampEnv;

    int note = 60;
    float velocity = 1.0f;
    bool gate = false; // клавиша нажата

    bool fading = false;
    float fadeGain = 1.0f, fadeStep = 0.01f;
    bool hasPending = false, pendingReleased = false;
    int pendingNote = 60;
    float pendingVelocity = 1.0f;

    std::uint64_t noteOnOrder = 0, releaseOrder = 0;
};

} // namespace neutron::dsp
