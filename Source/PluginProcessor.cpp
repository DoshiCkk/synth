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

juce::String formatHz(float hz, int)
{
    return hz < 1000.0f ? juce::String(hz, 1) + " Hz" : juce::String(hz / 1000.0f, 2) + " kHz";
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
int loadInt(const std::atomic<float>* p) noexcept { return juce::roundToInt(load(p)); }

// Параметры одного осциллятора: ID вида osc_a_wave, osc_b_fine, …
void addOscParams(Apvts::ParameterLayout& layout, const juce::String& id, const juce::String& name,
                  float defaultFine, float defaultLevel)
{
    const auto pid = [&](const char* suffix) { return juce::ParameterID { id + "_" + suffix, 1 }; };

    layout.add(std::make_unique<juce::AudioParameterChoice>(pid("wave"), name + " Wave",
                                                            juce::StringArray { "Saw", "Pulse", "Triangle" }, 0));
    layout.add(std::make_unique<juce::AudioParameterInt>(pid("octave"), name + " Octave", -2, 2, 0));
    layout.add(std::make_unique<juce::AudioParameterInt>(pid("semi"), name + " Semitone", -12, 12, 0));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        pid("fine"), name + " Fine", juce::NormalisableRange<float>(-50.0f, 50.0f, 0.1f), defaultFine,
        juce::AudioParameterFloatAttributes().withLabel("ct")));
    layout.add(std::make_unique<juce::AudioParameterFloat>(
        pid("pw"), name + " Pulse Width", juce::NormalisableRange<float>(5.0f, 95.0f, 0.1f), 50.0f,
        juce::AudioParameterFloatAttributes().withLabel("%")));
    layout.add(std::make_unique<juce::AudioParameterFloat>(pid("level"), name + " Level",
                                                           juce::NormalisableRange<float>(0.0f, 1.0f), defaultLevel));
}

neutron::dsp::OscSettings readOsc(const auto& ptrs) noexcept
{
    neutron::dsp::OscSettings s;
    s.wave = static_cast<neutron::dsp::Waveform>(loadInt(ptrs.wave));
    s.octave = loadInt(ptrs.octave);
    s.semitone = loadInt(ptrs.semitone);
    s.fineCents = load(ptrs.fine);
    s.pulseWidth = load(ptrs.pulseWidth) * 0.01;
    s.level = load(ptrs.level);
    return s;
}
} // namespace

NeutronProcessor::NeutronProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "NEUTRON", createParameterLayout())
{
    const auto bindOsc = [this](OscPointers& o, const juce::String& id)
    {
        o.wave = apvts.getRawParameterValue(id + "_wave");
        o.octave = apvts.getRawParameterValue(id + "_octave");
        o.semitone = apvts.getRawParameterValue(id + "_semi");
        o.fine = apvts.getRawParameterValue(id + "_fine");
        o.pulseWidth = apvts.getRawParameterValue(id + "_pw");
        o.level = apvts.getRawParameterValue(id + "_level");
    };
    bindOsc(params.oscA, "osc_a");
    bindOsc(params.oscB, "osc_b");

    params.cutoff = apvts.getRawParameterValue("filter_cutoff");
    params.resonance = apvts.getRawParameterValue("filter_resonance");
    params.filterEnvAmount = apvts.getRawParameterValue("filter_env_amount");
    params.keyTrack = apvts.getRawParameterValue("filter_key_track");
    params.velToFilterEnv = apvts.getRawParameterValue("filter_vel_amount");
    params.filterAttack = apvts.getRawParameterValue("filter_env_attack");
    params.filterDecay = apvts.getRawParameterValue("filter_env_decay");
    params.filterSustain = apvts.getRawParameterValue("filter_env_sustain");
    params.filterRelease = apvts.getRawParameterValue("filter_env_release");

    params.ampAttack = apvts.getRawParameterValue("amp_env_attack");
    params.ampDecay = apvts.getRawParameterValue("amp_env_decay");
    params.ampSustain = apvts.getRawParameterValue("amp_env_sustain");
    params.ampRelease = apvts.getRawParameterValue("amp_env_release");
    params.masterVolume = apvts.getRawParameterValue("master_volume");
}

Apvts::ParameterLayout NeutronProcessor::createParameterLayout()
{
    Apvts::ParameterLayout layout;

    addOscParams(layout, "osc_a", "Osc A", 0.0f, 1.0f);
    addOscParams(layout, "osc_b", "Osc B", 7.0f, 0.5f);

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { "filter_cutoff", 1 }, "Filter Cutoff", logRange(20.0f, 20000.0f), 3000.0f,
        juce::AudioParameterFloatAttributes().withStringFromValueFunction(formatHz)));
    layout.add(floatParam("filter_resonance", "Filter Resonance", { 0.0f, 1.0f }, 0.15f));
    layout.add(floatParam("filter_env_amount", "Filter Env Amount", { -1.0f, 1.0f }, 0.35f));
    layout.add(floatParam("filter_key_track", "Filter Key Track", { 0.0f, 100.0f, 0.1f }, 50.0f, "%"));
    layout.add(floatParam("filter_vel_amount", "Velocity > Filter Env", { 0.0f, 1.0f }, 0.3f));

    layout.add(timeParam("filter_env_attack", "Filter Env Attack", 10000.0f, 2.0f));
    layout.add(timeParam("filter_env_decay", "Filter Env Decay", 20000.0f, 600.0f));
    layout.add(floatParam("filter_env_sustain", "Filter Env Sustain", { 0.0f, 1.0f }, 0.25f));
    layout.add(timeParam("filter_env_release", "Filter Env Release", 20000.0f, 400.0f));

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
    p.oscA = readOsc(params.oscA);
    p.oscB = readOsc(params.oscB);
    p.cutoffHz = load(params.cutoff);
    p.resonance = load(params.resonance);
    p.filterEnvAmount = load(params.filterEnvAmount);
    p.keyTrack = load(params.keyTrack) * 0.01;
    p.velToFilterEnv = load(params.velToFilterEnv);
    p.filterEnv = { load(params.filterAttack) * 0.001, load(params.filterDecay) * 0.001,
                    load(params.filterSustain), load(params.filterRelease) * 0.001 };
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
