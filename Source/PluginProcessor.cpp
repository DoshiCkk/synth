#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
using Apvts = juce::AudioProcessorValueTreeState;

// Логарифмическая шкала: равные шаги ручки = равные отношения значений (октавы, декады).
juce::NormalisableRange<float> logRange(float min, float max)
{
    return { min, max,
             [](float start, float end, float t) { return start * std::pow(end / start, t); },
             [](float start, float end, float v) { return std::log(v / start) / std::log(end / start); },
             [](float start, float end, float v) { return juce::jlimit(start, end, v); } };
}

juce::String formatMs(float ms, int)
{
    return ms < 1000.0f ? juce::String(ms, 1) + " ms" : juce::String(ms / 1000.0f, 2) + " s";
}

std::unique_ptr<juce::AudioParameterFloat> floatParam(const char* id, const char* name,
                                                      juce::NormalisableRange<float> range, float def,
                                                      const char* label = "")
{
    return std::make_unique<juce::AudioParameterFloat>(juce::ParameterID { id, 1 }, name, range, def,
                                                       juce::AudioParameterFloatAttributes().withLabel(label));
}

std::unique_ptr<juce::AudioParameterFloat> timeParam(const char* id, const char* name, float maxMs, float defMs)
{
    return std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { id, 1 }, name, logRange(1.0f, maxMs), defMs,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction(formatMs));
}

float load(const std::atomic<float>* p) noexcept { return p->load(std::memory_order_relaxed); }
} // namespace

NeutronProcessor::NeutronProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "NEUTRON", createParameterLayout())
{
    params.oscALevel = apvts.getRawParameterValue("osc_a_level");
    params.ampAttack = apvts.getRawParameterValue("amp_env_attack");
    params.ampDecay = apvts.getRawParameterValue("amp_env_decay");
    params.ampSustain = apvts.getRawParameterValue("amp_env_sustain");
    params.ampRelease = apvts.getRawParameterValue("amp_env_release");
    params.masterVolume = apvts.getRawParameterValue("master_volume");
}

Apvts::ParameterLayout NeutronProcessor::createParameterLayout()
{
    Apvts::ParameterLayout layout;

    layout.add(floatParam("osc_a_level", "Osc A Level", { 0.0f, 1.0f }, 1.0f));

    layout.add(timeParam("amp_env_attack", "Amp Env Attack", 10000.0f, 2.0f));
    layout.add(timeParam("amp_env_decay", "Amp Env Decay", 20000.0f, 400.0f));
    layout.add(floatParam("amp_env_sustain", "Amp Env Sustain", { 0.0f, 1.0f }, 0.8f));
    layout.add(timeParam("amp_env_release", "Amp Env Release", 20000.0f, 350.0f));

    layout.add(floatParam("master_volume", "Master Volume", { -60.0f, 6.0f, 0.1f }, -6.0f, "dB"));

    return layout;
}

void NeutronProcessor::prepareToPlay(double sampleRate, int)
{
    voiceManager.prepare(sampleRate);
    updateSynthParams();
}

void NeutronProcessor::releaseResources()
{
}

bool NeutronProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
}

void NeutronProcessor::updateSynthParams() noexcept
{
    neutron::dsp::SynthParams p;
    p.oscALevel = load(params.oscALevel);
    p.ampEnv = { load(params.ampAttack) * 0.001, load(params.ampDecay) * 0.001, load(params.ampSustain),
                 load(params.ampRelease) * 0.001 };
    p.masterGain = juce::Decibels::decibelsToGain(load(params.masterVolume));
    voiceManager.setParams(p);
}

void NeutronProcessor::handleMidi(const juce::uint8* data, int numBytes) noexcept
{
    if (numBytes < 2)
        return;

    const int status = data[0] & 0xF0;
    const int d1 = data[1] & 0x7F;
    const int d2 = numBytes > 2 ? (data[2] & 0x7F) : 0;

    switch (status)
    {
        case 0x90:
            if (d2 > 0)
                voiceManager.noteOn(d1, static_cast<float>(d2) / 127.0f);
            else
                voiceManager.noteOff(d1); // note-on с velocity 0 = note-off
            break;

        case 0x80:
            voiceManager.noteOff(d1);
            break;

        default:
            break;
    }
}

void NeutronProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    if (buffer.getNumChannels() == 0 || numSamples == 0)
        return;

    updateSynthParams();

    // Sample-accurate MIDI: рендерим до позиции события, обрабатываем его, идём дальше.
    // Сырые байты вместо MidiMessage — без копирования и возможных аллокаций (SysEx).
    float* out = buffer.getWritePointer(0);
    int position = 0;
    for (const auto event : midi)
    {
        const int eventPos = juce::jlimit(0, numSamples, event.samplePosition);
        if (eventPos > position)
        {
            voiceManager.render(out + position, eventPos - position);
            position = eventPos;
        }
        handleMidi(event.data, event.numBytes);
    }
    if (position < numSamples)
        voiceManager.render(out + position, numSamples - position);

    // Синт моно — копируем в остальные каналы.
    for (int ch = 1; ch < buffer.getNumChannels(); ++ch)
        buffer.copyFrom(ch, 0, buffer, 0, 0, numSamples);
}

juce::AudioProcessorEditor* NeutronProcessor::createEditor()
{
    return new NeutronEditor(*this);
}

void NeutronProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary(*xml, destData);
}

void NeutronProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary(data, sizeInBytes))
        if (xml->hasTagName(apvts.state.getType()))
            apvts.replaceState(juce::ValueTree::fromXml(*xml));
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new NeutronProcessor();
}
