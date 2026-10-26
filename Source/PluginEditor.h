#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "ChoiceSelector.h"
#include "LookAndFeel.h"
#include "PluginProcessor.h"

// The step grid: one row per effect lane, one column per step. Click or drag to place effects.
class StepGrid : public juce::Component
{
public:
    explicit StepGrid (FractureProcessor&);

    std::function<void()> onEdit;

    void setRowHeight (int height) { rowHeight = height; }
    float stepCentre (int step) const { return cell (0, step).getCentreX(); }
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;

private:
    juce::Rectangle<float> cell (int lane, int step) const;
    std::pair<int, int> cellAt (juce::Point<float>) const;
    void paintCell (int lane, int step);

    FractureProcessor& processor;
    int rowHeight = 52;
    int dragLane = -1;
    bool dragSets = true;
};

class FractureEditor : public juce::AudioProcessorEditor,
                       private juce::Timer
{
public:
    explicit FractureEditor (FractureProcessor&);
    ~FractureEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct MiniKnob
    {
        juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox };
        juce::String caption;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    void timerCallback() override;
    void showPatternMenu();
    void randomize();
    MiniKnob& addKnob (const juce::String& paramId, const juce::String& caption, juce::Colour);

    FractureProcessor& processor;
    ContourLookAndFeel lookAndFeel;
    juce::TooltipWindow tooltips { this, 600 };
    juce::Random random;

    juce::TextButton patternButton { "Patterns" }, randomButton { "Randomize" }, clearButton { "Clear" }, bypassButton { "Bypass" };
    juce::Slider density { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAttachment;

    StepGrid grid;
    std::vector<std::unique_ptr<MiniKnob>> knobs;
    std::array<std::vector<MiniKnob*>, GlitchEngine::numLanes> laneKnobs;

    ChoiceSelector stepSelector;
    juce::Slider mix { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxRight };
    juce::Slider output { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::TextBoxRight };
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> mixAttachment, outputAttachment;

    juce::Rectangle<int> lanesArea, footerArea;
    int rowHeight = 52;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FractureEditor)
};
