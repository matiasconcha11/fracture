#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

// Row of buttons acting as one choice parameter (e.g. Tempo | Sidechain).
class ChoiceSelector : public juce::Component
{
public:
    ChoiceSelector (juce::RangedAudioParameter&, const juce::StringArray& labels);

    void setTooltip (const juce::String&);
    void resized() override;

private:
    juce::OwnedArray<juce::TextButton> buttons;
    juce::ParameterAttachment attachment;
};
