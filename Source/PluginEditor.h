#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "ChoiceSelector.h"
#include "LookAndFeel.h"
#include "PluginProcessor.h"
#include "PresetManager.h"

// The waveform strip and the step grid, sharing one set of columns. One row per effect lane.
// Drag across empty steps to draw a block; drag a block to move it, drag its right edge to resize
// it, click it to delete it, right-click it for its own settings.
class StepGrid : public juce::Component
{
public:
    static constexpr int stripHeight = 70;
    static constexpr int numbersHeight = 18;

    explicit StepGrid (FractureProcessor&);

    // Gesture brackets, so a whole drag becomes one undo step.
    std::function<void()> onEditStart, onEditEnd;
    std::function<void (int step, juce::Rectangle<int> blockBounds)> onBlockMenu;

    void setRowHeight (int height) { rowHeight = height; }
    int rowTop (int lane) const { return stripHeight + 8 + lane * rowHeight; }
    juce::Rectangle<float> blockBounds (int startStep) const;

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

private:
    enum class Gesture { none, draw, move, resize };

    juce::Rectangle<float> column (int step) const;
    juce::Rectangle<float> cell (int lane, int step) const;
    std::pair<int, int> cellAt (juce::Point<float>) const;
    int stepAtX (float x) const;
    int blockStart (const Patterns::Pattern&, int step) const;
    int blockEnd (const Patterns::Pattern&, int step) const;
    void applyGesture (int currentStep);
    void paintStrip (juce::Graphics&);
    void paintGlyph (juce::Graphics&, int lane, juce::Rectangle<float> area, juce::Colour);

    FractureProcessor& processor;
    int rowHeight = 44;

    Gesture gesture = Gesture::none;
    Patterns::Pattern before {};
    int gestureLane = -1, anchorStep = -1, originalStart = -1, originalEnd = -1;
    bool dragged = false;
    std::pair<int, int> hovered { -1, -1 };
};

// Floating panel for one block's own chance and value.
class BlockPanel : public juce::Component
{
public:
    BlockPanel();

    std::function<void()> onChange, onClose;

    void show (int lane, const GlitchEngine::Step&, float laneChance);
    GlitchEngine::Step getStep() const { return step; }

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void updateValueText();

    GlitchEngine::Step step;
    juce::Slider chance { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    juce::Slider value { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };
    juce::Label valueText;
    juce::TextButton resetButton { "Use lane settings" }, closeButton { "Done" };
    float laneChance = 1.0f;
};

// Inline prompt for naming or confirming, drawn over the editor.
class PromptOverlay : public juce::Component
{
public:
    PromptOverlay();

    void showPrompt (const juce::String& title, const juce::String& initialText, std::function<void (const juce::String&)> onConfirm);
    void showConfirm (const juce::String& title, const juce::String& confirmText, std::function<void()> onConfirm);

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    void confirm();
    juce::Rectangle<int> panel() const { return getLocalBounds().withSizeKeepingCentre (340, wantsText ? 150 : 120); }

    juce::String title;
    juce::TextEditor text;
    juce::TextButton okButton, cancelButton { "Cancel" };
    std::function<void (const juce::String&)> callback;
    bool wantsText = false;
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
    Knob& addKnob (const juce::String& paramId, const juce::String& caption, juce::Colour, bool withValue);

    // Pattern edits go through the undo manager.
    void beginEdit();
    void endEdit();
    void replacePattern (const Patterns::Pattern&);
    void openBlockPanel (int step, juce::Rectangle<int> blockBounds);

    void showPatternMenu();
    void showPresetMenu();
    void savePreset();
    void savePresetAs();
    void refreshPresetLabel();

    FractureProcessor& processor;
    PresetManager presets;
    ContourLookAndFeel lookAndFeel;
    juce::TooltipWindow tooltips { this, 600 };
    juce::UndoManager undoManager;
    juce::Random random;
    Patterns::Pattern patternBeforeEdit {};
    int patternBeingEdited = 0;
    int blockPanelStep = -1;

    // Header: presets and bypass.
    juce::TextButton prevButton { "<" }, presetButton, nextButton { ">" }, saveButton { "Save" }, bypassButton { "Bypass" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> bypassAttachment;

    // Toolbar: pattern slots and pattern tools.
    ChoiceSelector patternSelector;
    juce::TextButton chainButton { "Chain" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> chainAttachment;
    juce::TextButton patternsButton { "Patterns" }, randomButton { "Randomize" }, undoButton { "Undo" }, redoButton { "Redo" }, clearButton { "Clear" };
    juce::Slider density { juce::Slider::LinearHorizontal, juce::Slider::NoTextBox };

    // Grid and lanes.
    StepGrid grid;
    std::vector<std::unique_ptr<Knob>> knobs;
    std::array<std::vector<Knob*>, GlitchEngine::numLanes> laneKnobs;
    BlockPanel blockPanel;

    // Footer.
    ChoiceSelector stepSelector;
    Knob* lengthKnob = nullptr;
    Knob* swingKnob = nullptr;
    Knob* mutateKnob = nullptr;
    Knob* mixKnob = nullptr;
    Knob* protectKnob = nullptr;
    Knob* outputKnob = nullptr;

    PromptOverlay prompt;

    juce::Rectangle<int> toolbarArea, lanesArea, footerArea;
    int rowHeight = 44;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FractureEditor)
};
