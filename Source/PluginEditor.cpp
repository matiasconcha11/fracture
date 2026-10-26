#include "PluginEditor.h"

#include <map>

namespace
{
    constexpr int headerHeight = 56;
    constexpr int footerHeight = 76;
    constexpr int laneHeaderWidth = 300;
    constexpr int margin = 16;

    juce::Colour laneColour (int lane) { return Theme::modules[lane % 7]; }

    // Short labels for the small knobs in each lane.
    juce::String caption (const juce::String& paramId)
    {
        static const std::map<juce::String, juce::String> captions {
            { Params::repeatCount, "REPEATS" }, { Params::repeatPitch, "PITCH" },    { Params::tapeCurve, "CURVE" },
            { Params::gatePulses, "PULSES" },   { Params::gateDuty, "LENGTH" },      { Params::crushAmount, "AMOUNT" },
            { Params::filterDirection, "SWEEP" }, { Params::filterResonance, "RES" }, { Params::shuffleRange, "RANGE" },
        };

        const auto found = captions.find (paramId);
        return found != captions.end() ? found->second : paramId.toUpperCase();
    }
}

//==============================================================================
StepGrid::StepGrid (FractureProcessor& p) : processor (p) {}

juce::Rectangle<float> StepGrid::cell (int lane, int step) const
{
    // A little extra space between groups of four steps (beats at 1/16).
    const auto groupGap = 6.0f;
    const auto gap = 3.0f;
    const auto width = ((float) getWidth() - groupGap * 3.0f - gap * 12.0f) / (float) GlitchEngine::numSteps;
    const auto x = (float) step * (width + gap) + (float) (step / 4) * (groupGap - gap);
    return { x, (float) (lane * rowHeight) + 4.0f, width, (float) rowHeight - 8.0f };
}

std::pair<int, int> StepGrid::cellAt (juce::Point<float> position) const
{
    for (int lane = 0; lane < GlitchEngine::numLanes; ++lane)
        for (int step = 0; step < GlitchEngine::numSteps; ++step)
            if (cell (lane, step).expanded (1.5f).contains (position))
                return { lane, step };

    return { -1, -1 };
}

void StepGrid::paint (juce::Graphics& g)
{
    const auto playing = processor.engine.currentStep.load();
    const auto firing = processor.engine.firingLane.load();

    for (int lane = 0; lane < GlitchEngine::numLanes; ++lane)
    {
        const auto colour = laneColour (lane);

        for (int step = 0; step < GlitchEngine::numSteps; ++step)
        {
            const auto area = cell (lane, step);
            const auto on = processor.engine.getStep (step) == lane;
            const auto isPlaying = step == playing;

            auto fill = step / 4 % 2 == 0 ? Theme::raised : Theme::raised.brighter (0.03f);

            if (on)
                fill = colour.withAlpha (isPlaying && firing == lane ? 1.0f : 0.75f);
            else if (isPlaying)
                fill = fill.brighter (0.12f);

            g.setColour (fill);
            g.fillRoundedRectangle (area, 3.0f);

            if (on && isPlaying && firing == lane)
            {
                g.setColour (colour.withAlpha (0.35f));
                g.drawRoundedRectangle (area.expanded (2.0f), 4.0f, 2.0f);
            }
        }
    }

    // Step numbers under the grid are drawn by the editor.
}

void StepGrid::paintCell (int lane, int step)
{
    const auto current = processor.engine.getStep (step);

    if (dragSets && current != lane)
        processor.engine.setStep (step, lane);
    else if (! dragSets && current == lane)
        processor.engine.setStep (step, GlitchEngine::noEffect);
    else
        return;

    repaint();

    if (onEdit)
        onEdit();
}

void StepGrid::mouseDown (const juce::MouseEvent& e)
{
    const auto [lane, step] = cellAt (e.position);
    dragLane = lane;

    if (lane < 0)
        return;

    // Clicking a lit cell clears it (and dragging keeps clearing); otherwise it paints.
    dragSets = processor.engine.getStep (step) != lane;
    paintCell (lane, step);
}

void StepGrid::mouseDrag (const juce::MouseEvent& e)
{
    if (dragLane < 0)
        return;

    const auto [lane, step] = cellAt ({ e.position.x, (float) (dragLane * rowHeight + rowHeight / 2) });

    if (lane == dragLane && step >= 0)
        paintCell (lane, step);
}

//==============================================================================
FractureEditor::FractureEditor (FractureProcessor& p)
    : AudioProcessorEditor (&p),
      processor (p),
      grid (p),
      stepSelector (*p.apvts.getParameter (Params::stepLength), Params::stepLengthNames())
{
    setLookAndFeel (&lookAndFeel);

    for (auto* button : { &patternButton, &randomButton, &clearButton, &bypassButton })
        addAndMakeVisible (button);

    patternButton.onClick = [this] { showPatternMenu(); };
    randomButton.onClick = [this] { randomize(); };
    clearButton.onClick = [this] { processor.setPattern (Patterns::get (0)); grid.repaint(); };
    randomButton.setTooltip ("Generate a new pattern; the slider sets how busy it is");

    density.setRange (0.1, 0.9);
    density.setValue (0.4, juce::dontSendNotification);
    density.setTooltip ("Randomize density: how many steps get an effect");
    density.setColour (juce::Slider::trackColourId, Theme::modules[0]);
    density.setColour (juce::Slider::backgroundColourId, Theme::raised);
    density.setColour (juce::Slider::thumbColourId, Theme::text);
    addAndMakeVisible (density);

    bypassButton.setClickingTogglesState (true);
    bypassButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffff6b6b));
    bypassAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (p.apvts, Params::bypass, bypassButton);

    addAndMakeVisible (grid);

    // Per-lane controls: chance, then the lane's own settings.
    for (int lane = 0; lane < GlitchEngine::numLanes; ++lane)
    {
        laneKnobs[(size_t) lane].push_back (&addKnob (Params::chanceId (lane), "CHANCE", laneColour (lane)));

        for (const auto& id : Params::laneControls (lane))
            laneKnobs[(size_t) lane].push_back (&addKnob (id, caption (id), laneColour (lane)));
    }

    stepSelector.setTooltip ("Length of each step");
    addAndMakeVisible (stepSelector);

    for (auto* slider : { &mix, &output })
    {
        slider->setTextBoxStyle (juce::Slider::TextBoxRight, false, 66, 18);
        slider->setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        slider->setColour (juce::Slider::rotarySliderFillColourId, Theme::text.withAlpha (0.85f));
        slider->lookAndFeelChanged();
        addAndMakeVisible (slider);
    }

    output.getProperties().set ("bipolar", true);
    mixAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (p.apvts, Params::mix, mix);
    outputAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (p.apvts, Params::outputGain, output);

    setSize (1040, 590);
    startTimerHz (30);
}

FractureEditor::~FractureEditor()
{
    setLookAndFeel (nullptr);
}

FractureEditor::MiniKnob& FractureEditor::addKnob (const juce::String& paramId, const juce::String& caption, juce::Colour colour)
{
    auto knob = std::make_unique<MiniKnob>();
    knob->caption = caption;
    knob->slider.setColour (juce::Slider::rotarySliderFillColourId, colour);
    knob->slider.setPopupDisplayEnabled (true, true, this);
    knob->slider.setTooltip (processor.apvts.getParameter (paramId)->getName (64));
    knob->attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (processor.apvts, paramId, knob->slider);
    addAndMakeVisible (knob->slider);
    knobs.push_back (std::move (knob));
    return *knobs.back();
}

void FractureEditor::showPatternMenu()
{
    juce::PopupMenu menu;
    const auto names = Patterns::names();

    for (int i = 0; i < names.size(); ++i)
        menu.addItem (i + 1, names[i]);

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&patternButton), [this] (int result)
    {
        if (result > 0)
        {
            processor.setPattern (Patterns::get (result - 1));
            grid.repaint();
        }
    });
}

void FractureEditor::randomize()
{
    processor.setPattern (Patterns::randomize ((float) density.getValue(), random));
    grid.repaint();
}

void FractureEditor::paint (juce::Graphics& g)
{
    g.fillAll (Theme::background);

    // Header.
    g.setColour (Theme::panel);
    g.fillRect (0, 0, getWidth(), headerHeight);
    g.setColour (Theme::outline);
    g.drawHorizontalLine (headerHeight - 1, 0.0f, (float) getWidth());

    // Logo: a broken line.
    const auto m = juce::Rectangle<float> ((float) margin, 17.0f, 26.0f, 22.0f);
    juce::Path logo;
    logo.startNewSubPath (m.getX(), m.getCentreY());
    logo.lineTo (m.getX() + 8.0f, m.getCentreY());
    logo.startNewSubPath (m.getX() + 11.0f, m.getY());
    logo.lineTo (m.getX() + 15.0f, m.getBottom());
    logo.startNewSubPath (m.getX() + 18.0f, m.getCentreY());
    logo.lineTo (m.getRight(), m.getCentreY());
    g.setGradientFill (juce::ColourGradient (Theme::modules[6], m.getTopLeft(), Theme::modules[3], m.getBottomRight(), false));
    g.strokePath (logo, juce::PathStrokeType (2.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    auto title = juce::Rectangle<int> (margin + 36, 0, 300, headerHeight);
    g.setColour (Theme::text);
    g.setFont (Theme::caps (18.0f).withExtraKerningFactor (0.3f));
    g.drawText ("FRACTURE", title, juce::Justification::centredLeft);
    g.setColour (Theme::faint);
    g.setFont (Theme::font (11.0f));
    g.drawText ("v" FRACTURE_VERSION, title.withTrimmedLeft (150), juce::Justification::centredLeft);

    // Lane headers: colour, name, knob captions.
    for (int lane = 0; lane < GlitchEngine::numLanes; ++lane)
    {
        const auto row = juce::Rectangle<int> (lanesArea.getX(), lanesArea.getY() + lane * rowHeight, laneHeaderWidth, rowHeight);
        const auto colour = laneColour (lane);
        const auto firing = processor.engine.firingLane.load() == lane;

        g.setColour (firing ? colour.withAlpha (0.12f) : Theme::panel);
        g.fillRoundedRectangle (row.reduced (0, 4).toFloat(), 4.0f);
        g.setColour (colour);
        g.fillRoundedRectangle (row.reduced (0, 12).withWidth (3).toFloat(), 1.5f);

        g.setFont (Theme::caps (11.0f));
        g.setColour (Theme::text);
        g.drawText (Params::laneNames[lane].toUpperCase(), row.withTrimmedLeft (14).withWidth (96), juce::Justification::centredLeft);

        g.setFont (Theme::caps (8.5f));
        g.setColour (Theme::dim);

        for (auto* knob : laneKnobs[(size_t) lane])
            g.drawText (knob->caption, knob->slider.getBounds().withY (knob->slider.getBottom()).withHeight (11).expanded (8, 0),
                        juce::Justification::centredTop);
    }

    // Step numbers.
    g.setFont (Theme::font (10.0f));

    for (int step = 0; step < GlitchEngine::numSteps; ++step)
    {
        const auto x = grid.getX() + juce::roundToInt (grid.stepCentre (step));
        g.setColour (step == processor.engine.currentStep.load() ? Theme::text : Theme::faint);
        g.drawText (juce::String (step + 1), x - 10, grid.getBottom() + 2, 20, 14, juce::Justification::centred);
    }

    // Footer.
    g.setColour (Theme::panel);
    g.fillRect (footerArea);
    g.setColour (Theme::outline);
    g.drawHorizontalLine (footerArea.getY(), 0.0f, (float) getWidth());

    g.setFont (Theme::caps (10.0f));
    g.setColour (Theme::dim);
    g.drawText ("STEP", stepSelector.getBounds().withY (stepSelector.getY() - 18).withHeight (14), juce::Justification::centredLeft);
    g.drawText ("MIX", mix.getBounds().withY (mix.getY() - 18).withHeight (14), juce::Justification::centredLeft);
    g.drawText ("OUTPUT", output.getBounds().withY (output.getY() - 18).withHeight (14), juce::Justification::centredLeft);
}

void FractureEditor::resized()
{
    auto bounds = getLocalBounds();

    auto header = bounds.removeFromTop (headerHeight).reduced (margin, 12);
    bypassButton.setBounds (header.removeFromRight (80));
    header.removeFromRight (24);
    clearButton.setBounds (header.removeFromRight (70));
    header.removeFromRight (8);
    density.setBounds (header.removeFromRight (90));
    header.removeFromRight (6);
    randomButton.setBounds (header.removeFromRight (100));
    header.removeFromRight (8);
    patternButton.setBounds (header.removeFromRight (100));

    footerArea = bounds.removeFromBottom (footerHeight);
    auto footer = footerArea.reduced (margin, 12).withTrimmedTop (16);
    stepSelector.setBounds (footer.removeFromLeft (360).withHeight (28));
    output.setBounds (footer.removeFromRight (150).withHeight (40).translated (0, -6));
    footer.removeFromRight (20);
    mix.setBounds (footer.removeFromRight (150).withHeight (40).translated (0, -6));

    lanesArea = bounds.reduced (margin, 14).withTrimmedBottom (16);
    rowHeight = lanesArea.getHeight() / GlitchEngine::numLanes;

    grid.setRowHeight (rowHeight);
    grid.setBounds (lanesArea.withTrimmedLeft (laneHeaderWidth + 12).withHeight (rowHeight * GlitchEngine::numLanes));

    for (int lane = 0; lane < GlitchEngine::numLanes; ++lane)
    {
        auto row = juce::Rectangle<int> (lanesArea.getX() + 110, lanesArea.getY() + lane * rowHeight, laneHeaderWidth - 116, rowHeight);

        for (auto* knob : laneKnobs[(size_t) lane])
            knob->slider.setBounds (row.removeFromLeft (58).withSizeKeepingCentre (30, 30).translated (0, -6));
    }
}

void FractureEditor::timerCallback()
{
    grid.repaint();
    repaint (lanesArea.withWidth (laneHeaderWidth));
    repaint (grid.getBounds().withY (grid.getBottom()).withHeight (18));
}
