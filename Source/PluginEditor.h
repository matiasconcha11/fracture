#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "ChoiceSelector.h"
#include "LookAndFeel.h"
#include "PluginProcessor.h"

// The waveform strip and the step grid, sharing one set of columns. One row per effect lane;
// click or drag to place effects on the selected pattern.
class StepGrid : public juce::Component
{
public:
    static constexpr int stripHeight = 76;
    static constexpr int numbersHeight = 18;

    explicit StepGrid (FractureProcessor&);

    // Gesture brackets, so a whole drag becomes one undo step.
    std::function<void()> onEditStart, onEditEnd;

    void setRowHeight (int height) { rowHeight = height; }
    int rowTop (int lane) const { return stripHeight + 10 + lane * rowHeight; }

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

private:
    juce::Rectangle<float> column (int step) const;
    juce::Rectangle<float> cell (int lane, int step) const;
    std::pair<int, int> cellAt (juce::Point<float>) const;
    void paintCell (int lane, int step);
    void paintStrip (juce::Graphics&);
    void paintGlyph (juce::Graphics&, int lane, juce::Rectangle<float> area, juce::Colour);

    FractureProcessor& processor;
    int rowHeight = 48;
    int dragLane = -1;
    bool dragSets = true;
    std::pair<int, int> hovered { -1, -1 };
};

class FractureEditor : public juce::AudioProcessorEditor,
                       private juce::Timer
{
public:
    explicit FractureEditor (FractureProcessor&);
    ~FractureEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    struct Knob
    {
        juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox };
        juce::String caption;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    void timerCallback() override;
    void showPatternMenu();
    Knob& addKnob (const juce::String& paramId, const juce::String& caption, juce::Colour, bool withValue);

    // Pattern edits go through the undo manager.
    void beginEdit();
    void endEdit();
    void replacePattern (const Patterns::Pattern&);

    FractureProcessor& processor;
    ContourLookAndFeel lookAndFeel;
    juce::TooltipWindow tooltips { this, 600 };
    juce::UndoManager undoManager;
    juce::Random random;
    Patterns::Pattern patternBeforeEdit {};
    int patternBeingEdited = 0;

    // Header
    ChoiceSelector patternSelector;
    juce::TextButton chainButton { "Chain" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> chainAttachment;
    juce::TextButton patternsButton { "Patterns" }, randomButton { "Randomize" }, undoButton { "Undo" }, redoButton { "Redo" },
        clearButton { "Clear" }, bypassButton { "Bypass" };
    juce::Slider density { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAttachment;

    // Grid and lanes
    StepGrid grid;
    std::vector<std::unique_ptr<Knob>> knobs;
    std::array<std::vector<Knob*>, GlitchEngine::numLanes> laneKnobs;

    // Footer
    ChoiceSelector stepSelector;
    Knob* lengthKnob = nullptr;
    Knob* swingKnob = nullptr;
    Knob* mutateKnob = nullptr;
    Knob* mixKnob = nullptr;
    Knob* outputKnob = nullptr;

    juce::Rectangle<int> lanesArea, footerArea;
    int rowHeight = 48;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FractureEditor)
};
