// render_test — рендер тестовых нот в WAV без хоста и без JUCE.
//
//   render_test [папка_вывода]      (по умолчанию ./render_out)
//
// Использует тот же DSP-код (Source/dsp), что и плагин. Рендер идёт блоками по 256 сэмплов,
// параметры передаются раз в блок — как в processBlock.
//
// FTZ/DAZ (сброс денормалов в ноль) здесь НАМЕРЕННО не включается, в отличие от плагина
// (juce::ScopedNoDenormals). Если DSP где-то копит денормалы, тест 4 покажет это
// резким ростом времени рендера тишины.

#include "dsp/VoiceManager.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

namespace
{
using namespace neutron::dsp;

constexpr double kSampleRate = 48000.0;
constexpr int kBlockSize = 256;

// WAV 32-bit float, моно: сохраняет значения > 0 dBFS без клиппинга — удобно для анализа.
bool writeWav(const std::filesystem::path& path, const std::vector<float>& samples)
{
    std::ofstream f(path, std::ios::binary);
    if (!f)
        return false;

    const auto u32 = [&](std::uint32_t v) { f.write(reinterpret_cast<const char*>(&v), 4); };
    const auto u16 = [&](std::uint16_t v) { f.write(reinterpret_cast<const char*>(&v), 2); };
    const auto dataBytes = static_cast<std::uint32_t>(samples.size() * sizeof(float));

    f.write("RIFF", 4);
    u32(36 + dataBytes);
    f.write("WAVE", 4);
    f.write("fmt ", 4);
    u32(16);
    u16(3); // WAVE_FORMAT_IEEE_FLOAT
    u16(1); // моно
    u32(static_cast<std::uint32_t>(kSampleRate));
    u32(static_cast<std::uint32_t>(kSampleRate) * 4);
    u16(4);
    u16(32);
    f.write("data", 4);
    u32(dataBytes);
    f.write(reinterpret_cast<const char*>(samples.data()), dataBytes);
    return static_cast<bool>(f);
}

int seconds(double s) { return static_cast<int>(s * kSampleRate); }

// Тестовый синт: VoiceManager + запись в буфер блоками.
class Renderer
{
public:
    explicit Renderer(const SynthParams& p) : params(p)
    {
        vm.prepare(kSampleRate);
        vm.setParams(params);
    }

    VoiceManager vm;
    SynthParams params;
    std::vector<float> out;

    // onBlock(samplePosition) вызывается перед каждым блоком — для автоматизации параметров.
    template <typename OnBlock>
    void render(int numSamples, OnBlock&& onBlock)
    {
        const size_t start = out.size();
        out.resize(start + static_cast<size_t>(numSamples));
        for (int pos = 0; pos < numSamples; pos += kBlockSize)
        {
            onBlock(static_cast<int>(start) + pos);
            vm.setParams(params);
            vm.render(out.data() + start + pos, std::min(kBlockSize, numSamples - pos));
        }
    }

    void render(int numSamples)
    {
        render(numSamples, [](int) {});
    }
};

// Базовый патч для тестов: одна пила, фильтр открыт, огибающие без сюрпризов.
SynthParams basePatch()
{
    SynthParams p;
    p.oscA = { Waveform::Saw, 0, 0, 0.0, 0.5, 1.0 };
    p.oscB = { Waveform::Saw, 0, 0, 0.0, 0.5, 0.0 };
    p.cutoffHz = 20000.0;
    p.resonance = 0.0;
    p.filterEnvAmount = 0.0;
    p.keyTrack = 0.0;
    p.filterEnv = { 0.001, 0.5, 1.0, 0.3 };
    p.ampEnv = { 0.002, 0.5, 1.0, 0.05 };
    p.velToAmp = 0.0;
    p.masterGain = 1.0;
    return p;
}

struct Timer
{
    std::chrono::steady_clock::time_point t0 = std::chrono::steady_clock::now();
    double ms() const
    {
        return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    }
};

float peakOf(const std::vector<float>& v, size_t from = 0)
{
    float m = 0.0f;
    for (size_t i = from; i < v.size(); ++i)
        m = std::max(m, std::abs(v[i]));
    return m;
}

void report(const char* name, double ms, double audioSeconds, const std::filesystem::path& file)
{
    std::printf("  %-38s render %8.2f ms  (%6.0fx realtime)  -> %s\n", name, ms, audioSeconds * 1000.0 / ms,
                file.filename().string().c_str());
}
} // namespace

int main(int argc, char** argv)
{
    const std::filesystem::path outDir = argc > 1 ? argv[1] : "render_out";
    std::filesystem::create_directories(outDir);
    std::printf("NEUTRON render_test, %.0f Hz, block %d, output: %s\n\n", kSampleRate, kBlockSize,
                std::filesystem::absolute(outDir).string().c_str());

    // 1. Алиасинг: пила C2 / C5 / C7 по 1 с (+50 мс релиза), фильтр открыт.
    std::printf("1. Saw aliasing\n");
    for (const auto& [note, name] : { std::pair { 36, "C2" }, std::pair { 72, "C5" }, std::pair { 96, "C7" } })
    {
        Renderer r(basePatch());
        Timer t;
        r.vm.noteOn(note, 1.0f);
        r.render(seconds(1.0));
        r.vm.noteOff(note);
        r.render(seconds(0.1));
        const double ms = t.ms();
        const auto file = outDir / ("01_saw_" + std::string(name) + ".wav");
        writeWav(file, r.out);
        report((std::string("saw ") + name).c_str(), ms, 1.1, file);
    }

    // 2. Свип среза 20 Гц → 20 кГц за 8 с при резонансе 0.9. Срез меняется раз в блок,
    //    как автоматизация из хоста: проверка сглаживания и устойчивости TPT при модуляции.
    std::printf("2. Filter sweep\n");
    {
        SynthParams p = basePatch();
        p.oscB.level = 1.0;
        p.oscB.fineCents = 7.0;
        p.resonance = 0.9;
        p.cutoffHz = 20.0;
        p.ampEnv = { 0.002, 0.5, 1.0, 0.2 };
        Renderer r(p);
        const double sweepSeconds = 8.0;
        Timer t;
        r.vm.noteOn(48, 1.0f); // C3
        r.render(seconds(sweepSeconds), [&](int pos)
                 { r.params.cutoffHz = 20.0 * std::pow(1000.0, pos / (sweepSeconds * kSampleRate)); });
        r.vm.noteOff(48);
        r.render(seconds(0.5));
        const double ms = t.ms();
        const auto file = outDir / "02_cutoff_sweep_res09.wav";
        writeWav(file, r.out);
        report("cutoff sweep 20 Hz-20 kHz, res 0.9", ms, sweepSeconds + 0.5, file);
        std::printf("     peak %.3f\n", peakOf(r.out));
    }

    // 3. Аккорд из 8 нот + 9-я нота: кража самого старого голоса с затуханием 4 мс.
    std::printf("3. Voice stealing\n");
    {
        SynthParams p = basePatch();
        p.oscB.level = 0.7;
        p.oscB.fineCents = 5.0;
        p.cutoffHz = 3000.0;
        p.resonance = 0.2;
        p.ampEnv = { 0.005, 0.5, 0.8, 0.4 };
        p.masterGain = 0.5;
        Renderer r(p);
        const int chord[] = { 48, 52, 55, 59, 62, 64, 67, 71 }; // Cmaj9 в широком расположении
        Timer t;
        for (int note : chord)
            r.vm.noteOn(note, 0.9f);
        r.render(seconds(1.0));
        r.vm.noteOn(74, 0.9f); // 9-я нота: все 8 голосов держатся → крадётся C3 (самый старый)
        r.render(seconds(1.5));
        for (int note : chord)
            r.vm.noteOff(note);
        r.vm.noteOff(74);
        r.render(seconds(1.0));
        const double ms = t.ms();
        const auto file = outDir / "03_voice_steal.wav";
        writeWav(file, r.out);
        report("8-note chord + 9th note", ms, 3.5, file);
        std::printf("     peak %.3f, active voices at end: %d\n", peakOf(r.out), r.vm.getNumActiveVoices());
    }

    // 4. Тишина после релиза. Голоса, у которых закончился Amp Env, не считаются вовсе,
    //    а огибающие и сглаживание приходят в цель за конечное время — денормалов быть не должно.
    std::printf("4. Silence after release (denormals)\n");
    {
        SynthParams p = basePatch();
        p.resonance = 0.95; // фильтр «звенит» — худший случай для денормалов в его состоянии
        p.cutoffHz = 1000.0;
        p.ampEnv = { 0.002, 0.5, 1.0, 0.3 };
        Renderer r(p);
        for (int note = 48; note < 56; ++note)
            r.vm.noteOn(note, 1.0f);
        Timer tPlay;
        r.render(seconds(1.0));
        const double playMs = tPlay.ms();
        for (int note = 48; note < 56; ++note)
            r.vm.noteOff(note);
        r.render(seconds(1.0)); // хвост релиза
        const size_t silenceStart = r.out.size();
        Timer tSilence;
        r.render(seconds(5.0));
        const double silenceMs = tSilence.ms();
        const auto file = outDir / "04_silence_after_release.wav";
        writeWav(file, r.out);
        report("1 s of 8 voices (reference)", playMs, 1.0, file);
        report("5 s of silence after release", silenceMs, 5.0, file);
        std::printf("     active voices during silence: %d, peak in silence: %g\n", r.vm.getNumActiveVoices(),
                    static_cast<double>(peakOf(r.out, silenceStart)));
    }

    std::printf("\nDone.\n");
    return 0;
}
