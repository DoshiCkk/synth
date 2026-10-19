#include "PluginEditor.h"
#include "PluginProcessor.h"

NeutronEditor::NeutronEditor(NeutronProcessor& processor)
    : juce::GenericAudioProcessorEditor(processor)
{
    setResizable(true, false);
    setSize(520, 640);
}
