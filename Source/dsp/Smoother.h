#pragma once

#include <algorithm>

namespace neutron::dsp
{

// Линейное сглаживание параметра (против zipper-шума).
//
// Почему линейное, а не one-pole: экспонента приближается к цели бесконечно долго,
// и разность (цель − текущее) через пару секунд уходит в денормалы. Линейная рампа
// приходит в цель ровно за rampSamples и дальше не считает ничего.
// Если цель меняется посреди рампы (автоматизация), новая рампа стартует
// с текущего значения — разрывов нет.
class LinearSmoother
{
public:
    void prepare(double sampleRate, double rampSeconds) noexcept
    {
        rampSamples = std::max(1, static_cast<int>(sampleRate * rampSeconds));
        reset(target);
    }

    void reset(double value) noexcept
    {
        current = target = value;
        step = 0.0;
        remaining = 0;
    }

    void setTarget(double newTarget) noexcept
    {
        if (newTarget == target)
            return;

        target = newTarget;
        step = (target - current) / rampSamples;
        remaining = rampSamples;
    }

    double next() noexcept
    {
        if (remaining > 0)
        {
            current += step;
            if (--remaining == 0)
                current = target;
        }
        return current;
    }

    double getCurrent() const noexcept { return current; }
    double getTarget() const noexcept { return target; }

private:
    double current = 0.0;
    double target = 0.0;
    double step = 0.0;
    int rampSamples = 1;
    int remaining = 0;
};

} // namespace neutron::dsp
