#pragma once

#include <algorithm>
#include <cmath>

namespace neutron::dsp
{

enum class Waveform
{
    Saw = 0,
    Pulse,
    Triangle
};

// PolyBLEP (polynomial band-limited step).
//
// Наивная пила 2t−1 имеет мгновенный скачок на сбросе фазы. У скачка бесконечный
// спектр: гармоники выше Найквиста «заворачиваются» вниз и звучат как негармонический
// свист (алиасинг), особенно на высоких нотах.
// Идея BLEP: вычесть из скачка его не-band-limited часть («остаток»). PolyBLEP приближает
// этот остаток полиномом 2-й степени на 1 сэмпл по обе стороны скачка: пара умножений,
// без таблиц, и алиасинг падает на десятки дБ.
//
// t — фаза [0, 1), dt — приращение фазы за сэмпл (f / fs).
// Возвращает остаток для скачка высотой +2 (от −1 к +1).
inline double polyBlep(double t, double dt) noexcept
{
    if (t < dt) // сэмпл сразу после скачка
    {
        const double x = t / dt;
        return x + x - x * x - 1.0;
    }
    if (t > 1.0 - dt) // сэмпл сразу перед скачком
    {
        const double x = (t - 1.0) / dt;
        return x * x + x + x + 1.0;
    }
    return 0.0;
}

// PolyBLAMP — то же для излома (скачка производной), а не скачка значения.
// У треугольника нет скачков, но есть острые углы; их спектр спадает как 1/n²,
// так что алиасинга меньше, чем у пилы, но на высоких нотах он всё равно слышен.
// BLAMP-остаток — это интеграл BLEP-остатка: кубический полином на ±1 сэмпл.
// Возвращает остаток для излома с изменением наклона на +1 (амплитуда/сэмпл).
inline double polyBlamp(double t, double dt) noexcept
{
    if (t < dt)
    {
        const double x = 1.0 - t / dt;
        return x * x * x / 6.0;
    }
    if (t > 1.0 - dt)
    {
        const double x = (t - 1.0) / dt + 1.0;
        return x * x * x / 6.0;
    }
    return 0.0;
}

class Oscillator
{
public:
    void prepare(double sampleRate) noexcept { invSampleRate = 1.0 / sampleRate; }

    void setWaveform(Waveform w) noexcept { waveform = w; }

    // Фаза свободная: при note-on её никто не сбрасывает (как у аналогового VCO).
    // setPhase нужен только чтобы разные голоса стартовали не в одной фазе.
    void setPhase(double p) noexcept { phase = p - std::floor(p); }

    // pitch      — нота в полутонах (MIDI-номер, дробный)
    // pitchMod   — модуляция питча в полутонах, по сэмплу (вход под Poly-Mod на этапе 2)
    // pulseWidth — ширина импульса 0..1 (только для Pulse)
    float process(double pitch, double pitchMod, double pulseWidth) noexcept
    {
        const double freq = 440.0 * std::exp2((pitch + pitchMod - 69.0) / 12.0);
        // dt < 0.5: PolyBLEP-окна до и после скачка не должны перекрываться.
        const double dt = std::min(freq * invSampleRate, 0.49);

        double out = 0.0;
        switch (waveform)
        {
            case Waveform::Saw:
                out = 2.0 * phase - 1.0 - polyBlep(phase, dt); // скачок −2 на сбросе ⇒ вычитаем
                break;

            case Waveform::Pulse:
            {
                // Два скачка за период: +2 в t = 0 и −2 в t = pw. Для второго сдвигаем фазу
                // так, чтобы скачок оказался в нуле, и применяем тот же polyBlep.
                const double pw = std::clamp(pulseWidth, 0.01, 0.99);
                double fallPhase = phase - pw;
                if (fallPhase < 0.0)
                    fallPhase += 1.0;
                out = (phase < pw ? 1.0 : -1.0) + polyBlep(phase, dt) - polyBlep(fallPhase, dt);
                break;
            }

            case Waveform::Triangle:
            {
                // 1 − 4|t − 0.5|: минимум −1 в t = 0, максимум +1 в t = 0.5.
                // Наклон ±4 за период = ±4·dt за сэмпл; в углах он меняется на ±8·dt.
                double peakPhase = phase + 0.5;
                if (peakPhase >= 1.0)
                    peakPhase -= 1.0;
                out = 1.0 - 4.0 * std::abs(phase - 0.5)
                    + 8.0 * dt * (polyBlamp(phase, dt) - polyBlamp(peakPhase, dt));
                break;
            }
        }

        phase += dt;
        if (phase >= 1.0)
            phase -= 1.0;

        return static_cast<float>(out);
    }

private:
    double phase = 0.0; // double: на низких нотах dt ~ 1e-4, float накопил бы ошибку питча
    double invSampleRate = 1.0 / 48000.0;
    Waveform waveform = Waveform::Saw;
};

} // namespace neutron::dsp
