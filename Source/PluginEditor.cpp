#include "PluginEditor.h"

#include <map>

namespace
{
    constexpr int headerHeight = 60;
    constexpr int footerHeight = 92;
    constexpr int laneHeaderWidth = 300;
    constexpr int margin = 16;

    juce::Colour laneColour (int lane) { return Theme::modules[juce::jlimit (0, 8, lane)]; }

    // Short labels for the small knobs in each lane.
    juce::String caption (const juce::String& paramId)
    {
        static const std::map<juce::String, juce::String> captions {
            { Params::repeatCount, "REPEATS" },  { Params::repeatPitch, "PITCH" },     { Params::tapeCurve, "CURVE" },
            { Params::gatePulses, "PULSES" },    { Params::gateDuty, "LENGTH" },       { Params::crushAmount, "AMOUNT" },
            { Params::filterDirection, "SWEEP" }, { Params::filterResonance, "RES" },  { Params::shuffleRange, "RANGE" },
            { Params::echoTime, "TIME" },        { Params::echoFeedback, "FEEDBACK" }, { Params::pitchSemitones, "SHIFT" },
        };

        const auto found = captions.find (paramId);
        return found != captions.end() ? found->second : paramId.toUpperCase();
    }

    // Records one change to a pattern so it can be undone.
    class PatternEdit : public juce::UndoableAction
    {
    public:
        PatternEdit (FractureProcessor& p, int index, Patterns::Pattern b, Patterns::Pattern a)
            : processor (p), patternIndex (index), before (b), after (a) {}

        bool perform() override { processor.setPattern (patternIndex, after); return true; }
        bool undo() override { processor.setPattern (patternIndex, before); return true; }

    private:
        FractureProcessor& processor;
        int patternIndex;
        Patterns::Pattern before, after;
    };

    float paramValue (FractureProcessor& p, const juce::String& id) { return p.apvts.getRawParameterValue (id)->load(); }
}

//==============================================================================
StepGrid::StepGrid (FractureProcessor& p) : processor (p) {}

juce::Rectangle<float> StepGrid::column (int step) const
{
    // A little extra space between groups of four steps (beats at 1/16).
    const auto groupGap = 8.0f;
    const auto gap = 3.0f;
    const auto width = ((float) getWidth() - groupGap * 3.0f - gap * 12.0f) / (float) GlitchEngine::numSteps;
    const auto x = (float) step * (width + gap) + (float) (step / 4) * (groupGap - gap);
    return { x, 0.0f, width, (float) getHeight() };
}

juce::Rectangle<float> StepGrid::cell (int lane, int step) const
{
    return column (step).withY ((float) rowTop (lane) + 3.0f).withHeight ((float) rowHeight - 6.0f);
}

std::pair<int, int> StepGrid::cellAt (juce::Point<float> position) const
{
    for (int lane = 0; lane < GlitchEngine::numLanes; ++lane)
        for (int step = 0; step < GlitchEngine::numSteps; ++step)
            if (cell (lane, step).expanded (1.5f, 3.0f).contains (position))
                return { lane, step };

    return { -1, -1 };
}

void StepGrid::paintStrip (juce::Graphics& g)
{
    const auto strip = getLocalBounds().toFloat().withHeight ((float) stripHeight);
    g.setColour (juce::Colour (0xff101115));
    g.fillRoundedRectangle (strip, 6.0f);

    const auto centre = strip.getCentreY();
    const auto halfHeight = strip.getHeight() * 0.42f;
    const auto& engine = processor.engine;
    const auto binsPerStep = GlitchEngine::waveformBins / GlitchEngine::numSteps;

    // One bar per bin, mirrored around the centre, in the colour of the effect that was playing.
    for (int step = 0; step < GlitchEngine::numSteps; ++step)
    {
        const auto col = column (step);
        const auto binWidth = col.getWidth() / (float) binsPerStep;

        for (int b = 0; b < binsPerStep; ++b)
        {
            const auto index = (size_t) (step * binsPerStep + b);
            const auto peak = std::sqrt (juce::jlimit (0.0f, 1.0f, engine.waveform[index].load (std::memory_order_relaxed)));
            const auto lane = engine.waveformLane[index].load (std::memory_order_relaxed);
            const auto h = juce::jmax (1.0f, peak * halfHeight);

            g.setColour (lane == GlitchEngine::noEffect ? Theme::text.withAlpha (0.28f) : laneColour (lane).withAlpha (0.9f));
            g.fillRect (col.getX() + (float) b * binWidth, centre - h, juce::jmax (1.0f, binWidth - 0.5f), h * 2.0f);
        }
    }
}

void StepGrid::paintGlyph (juce::Graphics& g, int lane, juce::Rectangle<float> area, juce::Colour colour)
{
    // Small pictures of what each effect does.
    const auto r = area.withSizeKeepingCentre (juce::jmin (area.getWidth() - 8.0f, 26.0f), juce::jmin (area.getHeight() - 10.0f, 16.0f));
    juce::Path p;
    const juce::PathStrokeType stroke (1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
    g.setColour (colour);

    switch (lane)
    {
        case GlitchEngine::repeat:
            for (int i = 0; i < 4; ++i)
            {
                const auto x = r.getX() + r.getWidth() * (0.12f + 0.25f * (float) i);
                p.startNewSubPath (x, r.getY());
                p.lineTo (x, r.getBottom());
            }
            break;

        case GlitchEngine::reverse:
            p.addTriangle (r.getRight() - 4.0f, r.getY(), r.getRight() - 4.0f, r.getBottom(), r.getX() + 4.0f, r.getCentreY());
            g.fillPath (p);
            return;

        case GlitchEngine::tapeStop:
            p.startNewSubPath (r.getX(), r.getY());
            p.quadraticTo (r.getCentreX() + 4.0f, r.getY(), r.getRight(), r.getBottom());
            break;

        case GlitchEngine::gate:
            p.startNewSubPath (r.getX(), r.getBottom());
            for (int i = 0; i < 3; ++i)
            {
                const auto x = r.getX() + r.getWidth() * (float) i / 3.0f;
                const auto w = r.getWidth() / 3.0f;
                p.lineTo (x, r.getY());
                p.lineTo (x + w * 0.5f, r.getY());
                p.lineTo (x + w * 0.5f, r.getBottom());
                p.lineTo (x + w, r.getBottom());
            }
            break;

        case GlitchEngine::crush:
            p.startNewSubPath (r.getX(), r.getBottom());
            for (int i = 0; i < 4; ++i)
            {
                const auto x = r.getX() + r.getWidth() * (float) i / 4.0f;
                const auto y = r.getBottom() - r.getHeight() * (float) (i + 1) / 4.0f;
                p.lineTo (x, y);
                p.lineTo (x + r.getWidth() / 4.0f, y);
            }
            break;

        case GlitchEngine::filter:
            p.startNewSubPath (r.getX(), r.getY() + 3.0f);
            p.lineTo (r.getCentreX() - 2.0f, r.getY() + 3.0f);
            p.quadraticTo (r.getCentreX() + 3.0f, r.getY() + 1.0f, r.getRight(), r.getBottom());
            break;

        case GlitchEngine::shuffle:
            p.startNewSubPath (r.getX(), r.getBottom());
            p.lineTo (r.getRight(), r.getY());
            p.startNewSubPath (r.getX(), r.getY());
            p.lineTo (r.getRight(), r.getBottom());
            p.startNewSubPath (r.getRight() - 5.0f, r.getY());
            p.lineTo (r.getRight(), r.getY());
            p.lineTo (r.getRight(), r.getY() + 5.0f);
            break;

        case GlitchEngine::echo:
            for (int i = 0; i < 3; ++i)
            {
                const auto size = r.getHeight() * (1.0f - 0.25f * (float) i);
                g.setColour (colour.withAlpha (1.0f - 0.3f * (float) i));
                g.drawEllipse (juce::Rectangle<float> (size, size).withCentre ({ r.getX() + size * 0.5f + (float) i * r.getWidth() * 0.3f, r.getCentreY() }), 1.5f);
            }
            return;

        case GlitchEngine::pitch:
        {
            const auto down = paramValue (processor, Params::pitchSemitones) < 0.0f;
            const auto tip = down ? r.getBottom() : r.getY();
            const auto tail = down ? r.getY() : r.getBottom();
            p.startNewSubPath (r.getCentreX(), tail);
            p.lineTo (r.getCentreX(), tip);
            p.startNewSubPath (r.getCentreX() - 5.0f, tip + (down ? -5.0f : 5.0f));
            p.lineTo (r.getCentreX(), tip);
            p.lineTo (r.getCentreX() + 5.0f, tip + (down ? -5.0f : 5.0f));
            break;
        }

        default:
            return;
    }

    g.strokePath (p, stroke);
}

void StepGrid::paint (juce::Graphics& g)
{
    paintStrip (g);

    const auto& engine = processor.engine;
    const auto editing = processor.selectedPattern();
    const auto playingPattern = engine.currentPattern.load();
    const auto playing = engine.currentStep.load();
    const auto progress = engine.stepProgress.load();
    const auto firing = engine.firingLane.load();
    const auto length = (int) paramValue (processor, Params::length);

    for (int lane = 0; lane < GlitchEngine::numLanes; ++lane)
    {
        const auto colour = laneColour (lane);

        for (int step = 0; step < GlitchEngine::numSteps; ++step)
        {
            const auto area = cell (lane, step);
            const auto on = engine.getStep (editing, step) == lane;
            const auto inLoop = step < length;
            const auto isPlaying = step == playing && editing == playingPattern;
            const auto isFiring = on && isPlaying && firing == lane;

            if (on)
            {
                g.setGradientFill (juce::ColourGradient (colour.brighter (0.15f), area.getTopLeft(), colour.darker (0.25f), area.getBottomLeft(), false));
                g.setOpacity (inLoop ? (isFiring ? 1.0f : 0.82f) : 0.3f);
                g.fillRoundedRectangle (area, 4.0f);
                g.setOpacity (1.0f);

                // Sweep across the cell while its effect plays.
                if (isFiring)
                {
                    g.setColour (juce::Colours::white.withAlpha (0.22f));
                    g.fillRoundedRectangle (area.withWidth (area.getWidth() * progress), 4.0f);
                    g.setColour (colour.withAlpha (0.45f));
                    g.drawRoundedRectangle (area.expanded (2.5f), 5.0f, 2.0f);
                }

                paintGlyph (g, lane, area, Theme::background.withAlpha (inLoop ? 0.75f : 0.4f));
            }
            else
            {
                auto fill = step / 4 % 2 == 0 ? Theme::raised : Theme::raised.brighter (0.035f);

                if (isPlaying)
                    fill = fill.brighter (0.1f);

                g.setColour (inLoop ? fill : fill.withAlpha (0.35f));
                g.fillRoundedRectangle (area, 4.0f);
            }

            if (hovered == std::pair { lane, step })
            {
                g.setColour (colour.withAlpha (0.8f));
                g.drawRoundedRectangle (area.reduced (0.5f), 4.0f, 1.2f);
            }
        }
    }

    // Playhead line through strip and grid.
    if (playing >= 0 && editing == playingPattern)
    {
        const auto col = column (playing);
        const auto x = col.getX() + col.getWidth() * progress;
        g.setColour (Theme::text.withAlpha (0.7f));
        g.drawVerticalLine (juce::roundToInt (x), 0.0f, (float) (rowTop (GlitchEngine::numLanes) - 4));
    }

    // Step numbers.
    g.setFont (Theme::font (10.0f));

    for (int step = 0; step < GlitchEngine::numSteps; ++step)
    {
        g.setColour (step == playing ? Theme::text : (step < length ? Theme::faint : Theme::faint.withAlpha (0.4f)));
        g.drawText (juce::String (step + 1), column (step).withY ((float) rowTop (GlitchEngine::numLanes)).withHeight ((float) numbersHeight),
                    juce::Justification::centred);
    }
}

void StepGrid::paintCell (int lane, int step)
{
    const auto editing = processor.selectedPattern();
    const auto current = processor.engine.getStep (editing, step);

    if (dragSets && current != lane)
        processor.engine.setStep (editing, step, lane);
    else if (! dragSets && current == lane)
        processor.engine.setStep (editing, step, GlitchEngine::noEffect);
}

void StepGrid::mouseDown (const juce::MouseEvent& e)
{
    const auto [lane, step] = cellAt (e.position);
    dragLane = lane;

    if (lane < 0)
        return;

    if (onEditStart)
        onEditStart();

    // Clicking a lit cell clears it (and dragging keeps clearing); otherwise it paints.
    dragSets = processor.engine.getStep (processor.selectedPattern(), step) != lane;
    paintCell (lane, step);
    repaint();
}

void StepGrid::mouseDrag (const juce::MouseEvent& e)
{
    if (dragLane < 0)
        return;

    const auto [lane, step] = cellAt ({ e.position.x, (float) (rowTop (dragLane) + rowHeight / 2) });

    if (lane == dragLane && step >= 0)
    {
        paintCell (lane, step);
        repaint();
    }
}

void StepGrid::mouseUp (const juce::MouseEvent&)
{
    if (dragLane >= 0 && onEditEnd)
        onEditEnd();

    dragLane = -1;
}

void StepGrid::mouseMove (const juce::MouseEvent& e)
{
    hovered = cellAt (e.position);
    setMouseCursor (hovered.first >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
}

void StepGrid::mouseExit (const juce::MouseEvent&)
{
    hovered = { -1, -1 };
}

//==============================================================================
FractureEditor::FractureEditor (FractureProcessor& p)
    : AudioProcessorEditor (&p),
      processor (p),
      patternSelector (*p.apvts.getParameter (Params::pattern), { "A", "B", "C", "D" }),
      grid (p),
      stepSelector (*p.apvts.getParameter (Params::stepLength), Params::stepLengthNames())
{
    setLookAndFeel (&lookAndFeel);
    setWantsKeyboardFocus (true);

    // Header.
    patternSelector.setTooltip ("Pattern to edit and play");
    addAndMakeVisible (patternSelector);

    chainButton.setClickingTogglesState (true);
    chainButton.setTooltip ("Play the patterns that have steps in them one after another, a loop each");
    chainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (p.apvts, Params::chain, chainButton);
    addAndMakeVisible (chainButton);

    for (auto* button : { &patternsButton, &randomButton, &undoButton, &redoButton, &clearButton, &bypassButton })
        addAndMakeVisible (button);

    patternsButton.onClick = [this] { showPatternMenu(); };
    randomButton.onClick = [this] { replacePattern (Patterns::randomize ((float) density.getValue(), random)); };
    clearButton.onClick = [this] { replacePattern (Patterns::get (0)); };
    undoButton.onClick = [this] { undoManager.undo(); };
    redoButton.onClick = [this] { undoManager.redo(); };
    randomButton.setTooltip ("A new random pattern; the slider sets how busy it is");
    undoButton.setTooltip ("Undo pattern edit (Cmd+Z)");
    redoButton.setTooltip ("Redo pattern edit (Cmd+Shift+Z)");

    density.setRange (0.1, 0.9);
    density.setValue (0.4, juce::dontSendNotification);
    density.setTooltip ("How busy Randomize makes the pattern");
    density.setColour (juce::Slider::trackColourId, Theme::modules[0]);
    density.setColour (juce::Slider::backgroundColourId, Theme::raised);
    density.setColour (juce::Slider::thumbColourId, Theme::text);
    addAndMakeVisible (density);

    bypassButton.setClickingTogglesState (true);
    bypassButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffff6b6b));
    bypassAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (p.apvts, Params::bypass, bypassButton);

    // Grid.
    grid.onEditStart = [this] { beginEdit(); };
    grid.onEditEnd = [this] { endEdit(); };
    addAndMakeVisible (grid);

    for (int lane = 0; lane < GlitchEngine::numLanes; ++lane)
    {
        laneKnobs[(size_t) lane].push_back (&addKnob (Params::chanceId (lane), "CHANCE", laneColour (lane), false));

        for (const auto& id : Params::laneControls (lane))
            laneKnobs[(size_t) lane].push_back (&addKnob (id, caption (id), laneColour (lane), false));
    }

    // Footer.
    stepSelector.setTooltip ("Length of each step");
    addAndMakeVisible (stepSelector);
    lengthKnob = &addKnob (Params::length, "LENGTH", Theme::text.withAlpha (0.85f), true);
    swingKnob = &addKnob (Params::swing, "SWING", Theme::text.withAlpha (0.85f), true);
    mutateKnob = &addKnob (Params::mutate, "MUTATE", Theme::modules[6], true);
    mixKnob = &addKnob (Params::mix, "MIX", Theme::text.withAlpha (0.85f), true);
    outputKnob = &addKnob (Params::outputGain, "OUTPUT", Theme::text.withAlpha (0.85f), true);
    outputKnob->slider.getProperties().set ("bipolar", true);
    mutateKnob->slider.setTooltip ("Chance each loop that a step or two change by themselves");

    setSize (1120, 760);
    startTimerHz (40);
}

FractureEditor::~FractureEditor()
{
    setLookAndFeel (nullptr);
}

FractureEditor::Knob& FractureEditor::addKnob (const juce::String& paramId, const juce::String& captionText, juce::Colour colour, bool withValue)
{
    auto knob = std::make_unique<Knob>();
    knob->caption = captionText;
    knob->slider.setColour (juce::Slider::rotarySliderFillColourId, colour);
    knob->slider.setTooltip (processor.apvts.getParameter (paramId)->getName (64));

    if (withValue)
    {
        knob->slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 72, 16);
        knob->slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        knob->slider.setColour (juce::Slider::textBoxTextColourId, Theme::text);
        knob->slider.lookAndFeelChanged();
    }
    else
    {
        knob->slider.setPopupDisplayEnabled (true, true, this);
    }

    knob->attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (processor.apvts, paramId, knob->slider);
    addAndMakeVisible (knob->slider);
    knobs.push_back (std::move (knob));
    return *knobs.back();
}

//==============================================================================
void FractureEditor::beginEdit()
{
    patternBeingEdited = processor.selectedPattern();
    patternBeforeEdit = processor.getPattern (patternBeingEdited);
}

void FractureEditor::endEdit()
{
    const auto after = processor.getPattern (patternBeingEdited);

    if (after == patternBeforeEdit)
        return;

    undoManager.beginNewTransaction();
    undoManager.perform (new PatternEdit (processor, patternBeingEdited, patternBeforeEdit, after));
}

void FractureEditor::replacePattern (const Patterns::Pattern& pattern)
{
    beginEdit();
    processor.setPattern (patternBeingEdited, pattern);
    endEdit();
    grid.repaint();
}

bool FractureEditor::keyPressed (const juce::KeyPress& key)
{
    // Hosts often keep Cmd+Z for themselves; the Undo/Redo buttons always work.
    if (key == juce::KeyPress ('z', juce::ModifierKeys::commandModifier, 0))
        return undoManager.undo();

    if (key == juce::KeyPress ('z', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier, 0))
        return undoManager.redo();

    return false;
}

void FractureEditor::showPatternMenu()
{
    juce::PopupMenu menu;
    const auto names = Patterns::names();

    menu.addSectionHeader ("Load into pattern " + juce::String::charToString ((juce::juce_wchar) ('A' + processor.selectedPattern())));

    for (int i = 0; i < names.size(); ++i)
        menu.addItem (i + 1, names[i]);

    juce::PopupMenu copyTo;

    for (int p = 0; p < GlitchEngine::numPatterns; ++p)
        if (p != processor.selectedPattern())
            copyTo.addItem (100 + p, juce::String::charToString ((juce::juce_wchar) ('A' + p)));

    menu.addSeparator();
    menu.addSubMenu ("Copy this pattern to", copyTo);

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&patternsButton), [this] (int result)
    {
        if (result >= 100)
        {
            // Copy into another slot, as one undoable step on that slot.
            const auto source = processor.getPattern (processor.selectedPattern());
            const auto target = result - 100;
            undoManager.beginNewTransaction();
            undoManager.perform (new PatternEdit (processor, target, processor.getPattern (target), source));
        }
        else if (result > 0)
        {
            replacePattern (Patterns::get (result - 1));
        }
    });
}

//==============================================================================
void FractureEditor::paint (juce::Graphics& g)
{
    g.fillAll (Theme::background);

    // Header.
    g.setColour (Theme::panel);
    g.fillRect (0, 0, getWidth(), headerHeight);
    g.setColour (Theme::outline);
    g.drawHorizontalLine (headerHeight - 1, 0.0f, (float) getWidth());

    // Logo: a broken line.
    const auto m = juce::Rectangle<float> ((float) margin, 19.0f, 26.0f, 22.0f);
    juce::Path logo;
    logo.startNewSubPath (m.getX(), m.getCentreY());
    logo.lineTo (m.getX() + 8.0f, m.getCentreY());
    logo.startNewSubPath (m.getX() + 11.0f, m.getY());
    logo.lineTo (m.getX() + 15.0f, m.getBottom());
    logo.startNewSubPath (m.getX() + 18.0f, m.getCentreY());
    logo.lineTo (m.getRight(), m.getCentreY());
    g.setGradientFill (juce::ColourGradient (Theme::modules[6], m.getTopLeft(), Theme::modules[3], m.getBottomRight(), false));
    g.strokePath (logo, juce::PathStrokeType (2.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    auto title = juce::Rectangle<int> (margin + 36, 0, 260, headerHeight);
    g.setColour (Theme::text);
    g.setFont (Theme::caps (18.0f).withExtraKerningFactor (0.3f));
    g.drawText ("FRACTURE", title, juce::Justification::centredLeft);
    g.setColour (Theme::faint);
    g.setFont (Theme::font (11.0f));
    g.drawText ("v" FRACTURE_VERSION, title.withTrimmedLeft (150), juce::Justification::centredLeft);

    g.setFont (Theme::caps (9.5f));
    g.setColour (Theme::dim);
    g.drawText ("PATTERN", patternSelector.getBounds().withY (6).withHeight (12), juce::Justification::centredLeft);

    // Strip caption: which pattern is playing and where.
    const auto& engine = processor.engine;
    const auto stripLabel = juce::Rectangle<int> (lanesArea.getX(), grid.getY(), laneHeaderWidth, StepGrid::stripHeight);
    g.setColour (Theme::panel);
    g.fillRoundedRectangle (stripLabel.toFloat(), 6.0f);

    const auto playing = engine.currentStep.load();
    const auto firing = engine.firingLane.load();
    auto label = stripLabel.reduced (14, 12);
    g.setFont (Theme::caps (10.0f));
    g.setColour (Theme::dim);
    g.drawText ("NOW PLAYING", label.removeFromTop (14), juce::Justification::centredLeft);
    g.setFont (Theme::caps (16.0f));
    g.setColour (firing >= 0 ? laneColour (firing) : Theme::text);
    g.drawText (playing < 0 ? "STOPPED" : (firing >= 0 ? Params::laneNames[firing].toUpperCase() : "DRY"),
                label.removeFromTop (26), juce::Justification::centredLeft);
    g.setFont (Theme::font (11.5f));
    g.setColour (Theme::dim);
    g.drawText (playing < 0 ? "Press play in your DAW"
                            : "Pattern " + juce::String::charToString ((juce::juce_wchar) ('A' + engine.currentPattern.load()))
                                  + ", step " + juce::String (playing + 1),
                label, juce::Justification::centredLeft);

    // Lane headers.
    for (int lane = 0; lane < GlitchEngine::numLanes; ++lane)
    {
        const auto row = juce::Rectangle<int> (lanesArea.getX(), grid.getY() + grid.rowTop (lane), laneHeaderWidth, rowHeight);
        const auto colour = laneColour (lane);
        const auto isFiring = firing == lane;

        g.setColour (isFiring ? colour.withAlpha (0.14f) : Theme::panel);
        g.fillRoundedRectangle (row.reduced (0, 3).toFloat(), 4.0f);
        g.setColour (colour);
        g.fillRoundedRectangle (row.reduced (0, 11).withWidth (3).toFloat(), 1.5f);

        g.setFont (Theme::caps (11.0f));
        g.setColour (isFiring ? colour.brighter (0.4f) : Theme::text);
        g.drawText (Params::laneNames[lane].toUpperCase(), row.withTrimmedLeft (14).withWidth (100), juce::Justification::centredLeft);

        g.setFont (Theme::caps (8.0f));
        g.setColour (Theme::dim);

        for (auto* knob : laneKnobs[(size_t) lane])
            g.drawText (knob->caption, knob->slider.getBounds().withY (knob->slider.getBottom() - 1).withHeight (10).expanded (10, 0),
                        juce::Justification::centredTop);
    }

    // Footer.
    g.setColour (Theme::panel);
    g.fillRect (footerArea);
    g.setColour (Theme::outline);
    g.drawHorizontalLine (footerArea.getY(), 0.0f, (float) getWidth());

    g.setFont (Theme::caps (10.0f));
    g.setColour (Theme::dim);
    g.drawText ("STEP", stepSelector.getBounds().withY (stepSelector.getY() - 18).withHeight (14), juce::Justification::centredLeft);

    for (auto* knob : { lengthKnob, swingKnob, mutateKnob, mixKnob, outputKnob })
        g.drawText (knob->caption, knob->slider.getBounds().withY (knob->slider.getY() - 14).withHeight (12), juce::Justification::centred);
}

void FractureEditor::resized()
{
    auto bounds = getLocalBounds();

    // Header: logo | pattern slots + chain | tools | bypass.
    auto header = bounds.removeFromTop (headerHeight).reduced (margin, 14);
    bypassButton.setBounds (header.removeFromRight (80));
    header.removeFromRight (20);
    clearButton.setBounds (header.removeFromRight (64));
    header.removeFromRight (6);
    redoButton.setBounds (header.removeFromRight (56));
    header.removeFromRight (4);
    undoButton.setBounds (header.removeFromRight (56));
    header.removeFromRight (16);
    density.setBounds (header.removeFromRight (80));
    header.removeFromRight (4);
    randomButton.setBounds (header.removeFromRight (98));
    header.removeFromRight (6);
    patternsButton.setBounds (header.removeFromRight (92));

    header.removeFromLeft (230);
    patternSelector.setBounds (header.removeFromLeft (172).withTrimmedTop (6).translated (0, 2));
    header.removeFromLeft (8);
    chainButton.setBounds (header.removeFromLeft (64).withTrimmedTop (6).translated (0, 2));

    // Footer: step length | length, swing, mutate | mix, output.
    footerArea = bounds.removeFromBottom (footerHeight);
    auto footer = footerArea.reduced (margin, 10);
    stepSelector.setBounds (footer.removeFromLeft (360).withTrimmedTop (28).withHeight (28));

    const auto placeKnob = [] (Knob* knob, juce::Rectangle<int> area) { knob->slider.setBounds (area.withTrimmedTop (14)); };

    auto right = footer.removeFromRight (180);
    placeKnob (outputKnob, right.removeFromRight (90));
    placeKnob (mixKnob, right);

    auto middle = footer.withSizeKeepingCentre (270, footer.getHeight());
    placeKnob (lengthKnob, middle.removeFromLeft (90));
    placeKnob (swingKnob, middle.removeFromLeft (90));
    placeKnob (mutateKnob, middle);

    // Lanes: headers on the left, grid (with the waveform strip on top) on the right.
    lanesArea = bounds.reduced (margin, 12);
    rowHeight = (lanesArea.getHeight() - StepGrid::stripHeight - 10 - StepGrid::numbersHeight) / GlitchEngine::numLanes;
    grid.setRowHeight (rowHeight);
    grid.setBounds (lanesArea.withTrimmedLeft (laneHeaderWidth + 12));

    for (int lane = 0; lane < GlitchEngine::numLanes; ++lane)
    {
        auto row = juce::Rectangle<int> (lanesArea.getX() + 112, grid.getY() + grid.rowTop (lane), laneHeaderWidth - 116, rowHeight);

        for (auto* knob : laneKnobs[(size_t) lane])
            knob->slider.setBounds (row.removeFromLeft (60).withSizeKeepingCentre (28, 28).translated (0, -5));
    }
}

void FractureEditor::timerCallback()
{
    grid.repaint();
    repaint (lanesArea.withWidth (laneHeaderWidth));

    undoButton.setEnabled (undoManager.canUndo());
    redoButton.setEnabled (undoManager.canRedo());
}
