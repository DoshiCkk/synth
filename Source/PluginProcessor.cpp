#include "PluginProcessor.h"
#include "PluginEditor.h"

NeutronProcessor::NeutronProcessor()
    : AudioProcessor(BusesProperties().withOutput("Output", juce::AudioChannelSet::stereo(), true)),
      apvts(*this, nullptr, "NEUTRON", createParameterLayout())
{
}

juce::AudioProcessorValueTreeState::ParameterLayout NeutronProcessor::createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { "master_volume", 1 }, "Master Volume",
        juce::NormalisableRange<float>(-60.0f, 6.0f, 0.1f), -6.0f,
        juce::AudioParameterFloatAttributes().withLabel("dB")));

    return layout;
}

void NeutronProcessor::prepareToPlay(double, int)
{
}

void NeutronProcessor::releaseResources()
{
}

bool NeutronProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
}

void NeutronProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();
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
