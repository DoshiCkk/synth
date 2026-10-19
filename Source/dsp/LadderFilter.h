#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace neutron::dsp
{

// 4-полюсный lowpass в духе Moog ladder, ZDF/TPT (V. Zavalishin, «The Art of VA Filter Design»).
//
// Аналоговый ladder — это 4 одинаковых RC-звена (one-pole lowpass) подряд и обратная связь
// с выхода на вход с коэффициентом k (резонанс). На k = 4 контур самовозбуждается.
//
// TPT-интегратор (topology-preserving transform). Каждое звено — аналоговый интегратор,
// оцифрованный методом трапеций (билинейное преобразование), но с сохранением схемы:
//     v = (x − s) · G        G = g / (1 + g),  g = tan(π·fc/fs)
//     y = v + s              (выход звена)
//     s = y + v              (новое состояние интегратора)
// tan(…) — «pre-warping»: частота среза цифрового фильтра точно совпадает с fc даже у
// Найквиста. Состояние s хранит «заряд конденсатора», а не прошлые входы/выходы, как в
// biquad. Поэтому при резкой модуляции fc (огибающей, по сэмплу) фильтр не щёлкает и не
// теряет устойчивость: меняется только g, а накопленная энергия остаётся та же.
//
// ZDF (zero-delay feedback). Обычная цифровая реализация берёт обратную связь с выхода
// прошлого сэмпла: лишняя задержка в петле сдвигает частоту резонанса и ломает
// устойчивость на высоких fc. Здесь петля решается точно в том же сэмпле.
// Каждое звено линейно: y = G·x + S, где S = s/(1 + g). Значит весь каскад:
//     y4 = G⁴·u + (G³·S1 + G²·S2 + G·S3 + S4),   u = x − k·y4
// Это одно линейное уравнение на y4, его решаем в лоб, затем прогоняем звенья с готовым u.
class LadderFilter
{
public:
    // k = 4 — граница самовозбуждения. У ЛИНЕЙНОГО фильтра на ней ничто не ограничивает
    // амплитуду: если гармоника входа попадёт на частоту среза, выход растёт бесконечно.
    // Поэтому без нелинейности держимся чуть ниже: резонанс «звенит» сотни мс, но затухает.
    // Когда в контур встанет tanh (см. process), его можно будет поднять до 4 и выше.
    static constexpr double kMaxFeedback = 3.96;

    void prepare(double newSampleRate) noexcept
    {
        sampleRate = newSampleRate;
        reset();
    }

    void reset() noexcept { s.fill(0.0); }

    // cutoffHz — частота среза, resonance — 0..1 (0 = без резонанса, 1 = на грани самовозбуждения)
    float process(float input, double cutoffHz, double resonance) noexcept
    {
        const double fc = std::clamp(cutoffHz, 5.0, sampleRate * 0.49);
        const double g = std::tan(std::numbers::pi * fc / sampleRate);
        const double G = g / (1.0 + g);
        const double k = kMaxFeedback * std::clamp(resonance, 0.0, 1.0);

        const double G2 = G * G;
        const double G4 = G2 * G2;
        const double S = (G2 * G * s[0] + G2 * s[1] + G * s[2] + s[3]) / (1.0 + g);

        const double y4 = (G4 * input + S) / (1.0 + k * G4);
        double u = input - k * y4;

        // Место под нелинейность: в аналоге транзисторы входного каскада ограничивают u,
        // u = tanh(u). Тогда уравнение на y4 станет нелинейным — его решают итерациями
        // Ньютона или приближением (например, tanh от оценки y4). Пока фильтр линейный.

        for (auto& state : s)
        {
            const double v = (u - state) * G;
            const double y = v + state;
            state = y + v;
            u = y;
        }
        return static_cast<float>(u);
    }

private:
    double sampleRate = 48000.0;
    std::array<double, 4> s {}; // double: возле k≈4 петля чувствительна к ошибке округления
};

} // namespace neutron::dsp
