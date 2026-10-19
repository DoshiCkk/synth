#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "dsp/VoiceManager.h"

class NeutronProcessor final : public juce::AudioProcessor
{
public:
    NeutronProcessor();
    ~NeutronProcessor() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    void updateSynthParams() noexcept;
    void handleMidi(const juce::uint8* data, int numBytes) noexcept;

    // Указатели на атомарные значения параметров: чтение из аудиопотока без блокировок.
    struct OscPointers
    {
        std::atomic<float>* wave = nullptr;
        std::atomic<float>* octave = nullptr;
        std::atomic<float>* semitone = nullptr;
        std::atomic<float>* fine = nullptr;
        std::atomic<float>* pulseWidth = nullptr;
        std::atomic<float>* level = nullptr;
    };

    struct ParamPointers
    {
        OscPointers oscA, oscB;
        std::atomic<float>* cutoff = nullptr;
        std::atomic<float>* resonance = nullptr;
        std::atomic<float>* filterEnvAmount = nullptr;
        std::atomic<float>* keyTrack = nullptr;
        std::atomic<float>* velToFilterEnv = nullptr;
        std::atomic<float>* filterAttack = nullptr;
        std::atomic<float>* filterDecay = nullptr;
        std::atomic<float>* filterSustain = nullptr;
        std::atomic<float>* filterRelease = nullptr;
        std::atomic<float>* ampAttack = nullptr;
        std::atomic<float>* ampDecay = nullptr;
        std::atomic<float>* ampSustain = nullptr;
        std::atomic<float>* ampRelease = nullptr;
        std::atomic<float>* masterVolume = nullptr;
    };

    ParamPointers params;
    neutron::dsp::VoiceManager voiceManager;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NeutronProcessor)
};
