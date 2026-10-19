#pragma once

#include <array>
#include <cmath>
#include <cstdint>

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
    double velToAmp = 0.0;       // 0..1

    double pitchBendRange = 2.0; // полутона
    double masterGain = 0.5;     // линейный
};

// Распределение нот по 8 голосам. Своя логика вместо juce::Synthesiser — на этапе 2
// сюда добавятся mono/legato/глайд, которым нужен полный контроль над голосами.
//
// Порядок выбора голоса для новой ноты:
//   1. Та же нота уже звучит (держится или в release) → retrigger этого же голоса.
//   2. Свободный голос — тот, который дольше всех не использовался (ротация, как у
//      аналоговых полифоников: хвосты релиза не обрываются без нужды).
//   3. Кража: сначала голос в release (отпущенный раньше всех), иначе самый старый.
//      Украденный голос затухает за 4 мс и только потом берёт новую ноту.
class VoiceManager
{
public:
    static constexpr int kNumVoices = 8;

    void prepare(double sampleRate) noexcept
    {
        for (int i = 0; i < kNumVoices; ++i)
        {
            auto& v = voices[static_cast<size_t>(i)];
            v.prepare(sampleRate);
            // Разные стартовые фазы у голосов: аккорд не начинается с одинакового «щелчка»
            // всех пил в одной фазе. Дробные части золотого сечения — равномерный разброс.
            v.setInitialPhases(std::fmod(i * 0.618034, 1.0), std::fmod(i * 0.618034 + 0.37, 1.0));
        }
        for (auto* s : smoothers())
            s->prepare(sampleRate, kSmoothingSeconds);
        firstParams = true;
    }

    void reset() noexcept
    {
        for (auto& v : voices)
            v.reset();
        bendNormalized = 0.0;
        pitchBend.reset(0.0);
    }

    void setParams(const SynthParams& p) noexcept
    {
        for (auto& v : voices)
        {
            v.setWaveforms(p.oscA.wave, p.oscB.wave);
            v.setEnvelopes(p.filterEnv, p.ampEnv);
        }

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
        velToAmp.setTarget(p.velToAmp);

        bendRange = p.pitchBendRange;
        pitchBend.setTarget(bendNormalized * bendRange);
        masterGain.setTarget(p.masterGain);

        if (firstParams) // после prepare не «въезжаем» в значения из нуля
        {
            for (auto* s : smoothers())
                s->reset(s->getTarget());
            firstParams = false;
        }
    }

    void noteOn(int note, float velocity) noexcept
    {
        const auto order = ++eventCounter;

        for (auto& v : voices)
            if (v.isActive() && v.getNote() == note)
            {
                v.start(note, velocity, order);
                return;
            }

        Voice* chosen = nullptr;
        for (auto& v : voices)
            if (!v.isActive() && (chosen == nullptr || v.getNoteOnOrder() < chosen->getNoteOnOrder()))
                chosen = &v;

        if (chosen != nullptr)
        {
            chosen->start(note, velocity, order);
            return;
        }

        findVoiceToSteal().steal(note, velocity, order);
    }

    void noteOff(int note) noexcept
    {
        const auto order = ++eventCounter;
        for (auto& v : voices)
            if (v.isActive() && v.isGateOn() && v.getNote() == note)
                v.release(order);
    }

    void allNotesOff() noexcept
    {
        const auto order = ++eventCounter;
        for (auto& v : voices)
            if (v.isActive() && v.isGateOn())
                v.release(order);
    }

    void allSoundOff() noexcept
    {
        for (auto& v : voices)
            if (v.isActive())
                v.fadeOut();
    }

    // normalized: −1..+1 (14-битное колесо питча)
    void setPitchBend(double normalized) noexcept
    {
        bendNormalized = normalized;
        pitchBend.setTarget(bendNormalized * bendRange);
    }

    // Mod wheel (CC1) пока только запоминаем — подключим на этапе 2 (Poly-Mod/LFO).
    void setModWheel(double value) noexcept { modWheel = value; }
    double getModWheel() const noexcept { return modWheel; }

    const Voice& getVoice(int index) const noexcept { return voices[static_cast<size_t>(index)]; }

    int getNumActiveVoices() const noexcept
    {
        int n = 0;
        for (const auto& v : voices)
            n += v.isActive() ? 1 : 0;
        return n;
    }

    // Рендер в моно-буфер (перезаписывает). Вызывается кусками между MIDI-событиями.
    void render(float* out, int numSamples) noexcept
    {
        for (int i = 0; i < numSamples; ++i)
        {
            VoiceFrame frame;
            frame.pitchBend = pitchBend.next();
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
            frame.velToAmp = velToAmp.next();
            const float gain = static_cast<float>(masterGain.next());

            // Молчащие голоса не считаются вовсе: на тишине после релиза CPU ≈ 0.
            float sum = 0.0f;
            for (auto& v : voices)
                if (v.isActive())
                    sum += v.process(frame);

            out[i] = sum * gain;
        }
    }

private:
    static constexpr double kSmoothingSeconds = 0.02;

    Voice& findVoiceToSteal() noexcept
    {
        Voice* released = nullptr;
        Voice* oldest = nullptr;
        for (auto& v : voices)
        {
            if (v.isStealing())
                continue; // уже затухает — его не трогаем, пока есть другие кандидаты
            if (!v.isGateOn() && (released == nullptr || v.getReleaseOrder() < released->getReleaseOrder()))
                released = &v;
            if (oldest == nullptr || v.getNoteOnOrder() < oldest->getNoteOnOrder())
                oldest = &v;
        }
        if (released != nullptr)
            return *released;
        if (oldest != nullptr)
            return *oldest;

        // Все 8 голосов уже затухают (9+ нот за 4 мс): подменяем ожидающую ноту у самого старого.
        Voice* any = &voices[0];
        for (auto& v : voices)
            if (v.getNoteOnOrder() < any->getNoteOnOrder())
                any = &v;
        return *any;
    }

    std::array<LinearSmoother*, 14> smoothers() noexcept
    {
        return { &oscAFine,       &oscBFine,  &oscAPulseWidth, &oscBPulseWidth, &oscALevel,
                 &oscBLevel,      &cutoffLog2, &resonance,     &filterEnvAmount, &keyTrack,
                 &velToFilterEnv, &velToAmp,   &pitchBend,     &masterGain };
    }

    std::array<Voice, kNumVoices> voices;
    std::uint64_t eventCounter = 0;

    int oscACoarse = 0, oscBCoarse = 0;
    LinearSmoother oscAFine, oscBFine, oscAPulseWidth, oscBPulseWidth, oscALevel, oscBLevel;
    LinearSmoother cutoffLog2, resonance, filterEnvAmount, keyTrack, velToFilterEnv;
    LinearSmoother velToAmp;
    LinearSmoother pitchBend, masterGain;
    bool firstParams = true;

    double bendNormalized = 0.0;
    double bendRange = 2.0;
    double modWheel = 0.0;
};

} // namespace neutron::dsp
