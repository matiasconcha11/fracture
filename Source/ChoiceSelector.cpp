#include "ChoiceSelector.h"

ChoiceSelector::ChoiceSelector (juce::RangedAudioParameter& parameter, const juce::StringArray& labels)
    : attachment (parameter, [this] (float value)
                  {
                      for (int i = 0; i < buttons.size(); ++i)
                          buttons[i]->setToggleState (i == juce::roundToInt (value), juce::dontSendNotification);
                  })
{
    for (int i = 0; i < labels.size(); ++i)
    {
        auto* button = buttons.add (new juce::TextButton (labels[i]));
        button->onClick = [this, i] { attachment.setValueAsCompleteGesture ((float) i); };
        addAndMakeVisible (button);
    }

    attachment.sendInitialUpdate();
}

void ChoiceSelector::setTooltip (const juce::String& text)
{
    for (auto* button : buttons)
        button->setTooltip (text);
}

void ChoiceSelector::resized()
{
    auto bounds = getLocalBounds();
    const auto gap = 4;
    const auto width = (bounds.getWidth() - gap * (buttons.size() - 1)) / juce::jmax (1, buttons.size());

    for (auto* button : buttons)
    {
        button->setBounds (bounds.removeFromLeft (width));
        bounds.removeFromLeft (gap);
    }
}
