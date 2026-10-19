#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

class NeutronProcessor;

// На этапе 1 UI — стандартный список параметров JUCE. Кастомный UI будет позже.
class NeutronEditor final : public juce::GenericAudioProcessorEditor
{
public:
    explicit NeutronEditor(NeutronProcessor& processor);

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NeutronEditor)
};
