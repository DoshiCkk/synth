#pragma once

#include <algorithm>
#include <cmath>

namespace neutron::dsp
{

struct EnvelopeSettings
{
    double attack = 0.005;  // секунды
    double decay = 0.3;     // секунды
    double sustain = 0.8;   // 0..1
    double release = 0.3;   // секунды

    bool operator==(const EnvelopeSettings&) const = default;
};

// ADSR с экспоненциальными сегментами, как у аналоговой огибающей.
//
// В аналоге огибающая — это конденсатор, который заряжается/разряжается через резистор:
// уровень экспоненциально стремится к целевому напряжению. Цифровой аналог — one-pole:
//     level = target + (level − target) · coef
//
// Хитрость (как в схемах): цель ставится ЗА концом сегмента («overshoot»).
// Атака стремится к 1 + kAttackOvershoot, и когда уровень доходит до 1, переключаемся
// на decay. Поэтому атака заканчивается за конечное время и имеет характерную
// выпуклую форму. Decay/release стремятся чуть ниже sustain/нуля и тоже
// заканчиваются ровно — без бесконечного хвоста и денормалов.
//
// Retrigger без щелчка: noteOn() не обнуляет уровень, атака идёт с текущего значения.
class Envelope
{
public:
    enum class Stage
    {
        Idle,
        Attack,
        Decay,
        Sustain,
        Release
    };

    void prepare(double newSampleRate) noexcept
    {
        sampleRate = newSampleRate;
        updateCoefficients();
        reset();
    }

    void setSettings(const EnvelopeSettings& s) noexcept
    {
        if (s == settings)
            return;
        settings = s;
        updateCoefficients();
    }

    void noteOn() noexcept { stage = Stage::Attack; }

    void noteOff() noexcept
    {
        if (stage != Stage::Idle)
            stage = Stage::Release;
    }

    void reset() noexcept
    {
        stage = Stage::Idle;
        level = 0.0;
    }

    bool isActive() const noexcept { return stage != Stage::Idle; }
    Stage getStage() const noexcept { return stage; }
    double getLevel() const noexcept { return level; }

    double process() noexcept
    {
        switch (stage)
        {
            case Stage::Idle:
                break;

            case Stage::Attack:
                level = attackBase + level * attackCoef;
                if (level >= 1.0)
                {
                    level = 1.0;
                    stage = Stage::Decay;
                }
                break;

            case Stage::Decay:
                level = decayBase + level * decayCoef;
                if (level <= settings.sustain)
                    stage = Stage::Sustain; // уровень не «прыгает»: sustain-стадия доведёт его плавно
                break;

            case Stage::Sustain:
                // Sustain могут крутить, пока нота держится: подтягиваемся к новому
                // значению с той же скоростью, что и decay, вместо скачка.
                level = settings.sustain + (level - settings.sustain) * decayCoef;
                if (std::abs(level - settings.sustain) < 1.0e-7)
                    level = settings.sustain;
                break;

            case Stage::Release:
                level = releaseBase + level * releaseCoef;
                if (level <= 0.0)
                {
                    level = 0.0;
                    stage = Stage::Idle;
                }
                break;
        }
        return level;
    }

private:
    // Насколько цель уходит за конец сегмента. Больше overshoot — форма ближе к линейной.
    static constexpr double kAttackOvershoot = 0.3;    // выпуклая атака, как у RC-цепи
    static constexpr double kDecayOvershoot = 0.001;   // decay/release — почти чистая экспонента

    // Коэффициент one-pole, при котором путь от 0 до 1 (с overshoot r) занимает seconds.
    double coefficientFor(double seconds, double overshoot) const noexcept
    {
        const double samples = std::max(1.0, seconds * sampleRate);
        return std::exp(std::log(overshoot / (1.0 + overshoot)) / samples);
    }

    void updateCoefficients() noexcept
    {
        attackCoef = coefficientFor(settings.attack, kAttackOvershoot);
        attackBase = (1.0 + kAttackOvershoot) * (1.0 - attackCoef);

        // Время decay задано для полного пути 1 → 0, как постоянная RC в аналоге:
        // до высокого sustain огибающая доходит быстрее, до низкого — дольше.
        decayCoef = coefficientFor(settings.decay, kDecayOvershoot);
        decayBase = (settings.sustain - kDecayOvershoot) * (1.0 - decayCoef);

        releaseCoef = coefficientFor(settings.release, kDecayOvershoot);
        releaseBase = -kDecayOvershoot * (1.0 - releaseCoef);
    }

    // double: при атаке 10 с на 96 кГц coef = 0.9999986, во float это было бы ±7% ошибки времени.
    double sampleRate = 48000.0;
    EnvelopeSettings settings;
    Stage stage = Stage::Idle;
    double level = 0.0;

    double attackCoef = 0.0, attackBase = 0.0;
    double decayCoef = 0.0, decayBase = 0.0;
    double releaseCoef = 0.0, releaseBase = 0.0;
};

} // namespace neutron::dsp
