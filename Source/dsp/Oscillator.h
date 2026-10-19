#pragma once

#include <algorithm>
#include <cmath>

namespace neutron::dsp
{

enum class Waveform
{
    Saw = 0
};

// PolyBLEP (polynomial band-limited step).
//
// Наивная пила 2t−1 имеет мгновенный скачок на сбросе фазы. Скачок — это бесконечный
// спектр: гармоники выше Найквиста «заворачиваются» вниз и звучат как неградуированный
// свист (алиасинг), особенно на высоких нотах.
// Идеальный вариант — вычесть из скачка его «не-band-limited часть» (BLEP-остаток).
// PolyBLEP приближает этот остаток полиномом 2-й степени на 1 сэмпл по обе стороны
// скачка: дешево (пара умножений), без таблиц, и подавляет алиасинг на десятки дБ.
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

class Oscillator
{
public:
    void prepare(double sampleRate) noexcept { invSampleRate = 1.0 / sampleRate; }

    void setWaveform(Waveform w) noexcept { waveform = w; }

    // Фаза свободная: при note-on её никто не сбрасывает (как у аналогового VCO).
    // setPhase нужен только чтобы разные голоса стартовали не в одной фазе.
    void setPhase(double p) noexcept { phase = p - std::floor(p); }

    // pitch     — нота в полутонах (MIDI-номер, дробный)
    // pitchMod  — модуляция питча в полутонах, по сэмплу (вход под Poly-Mod на этапе 2)
    float process(double pitch, double pitchMod) noexcept
    {
        const double freq = 440.0 * std::exp2((pitch + pitchMod - 69.0) / 12.0);
        // dt < 0.5: PolyBLEP-окна до и после скачка не должны перекрываться.
        const double dt = std::min(freq * invSampleRate, 0.49);

        double out = 0.0;
        switch (waveform)
        {
            case Waveform::Saw:
                out = 2.0 * phase - 1.0 - polyBlep(phase, dt); // скачок вниз на −2 ⇒ вычитаем
                break;
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
