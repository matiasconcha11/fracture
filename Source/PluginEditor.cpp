#include "PluginEditor.h"

#include <map>

namespace
{
    constexpr int headerHeight = 56;
    constexpr int toolbarHeight = 46;
    constexpr int footerHeight = 92;
    constexpr int laneHeaderWidth = 290;
    constexpr int margin = 16;

    juce::Colour laneColour (int lane)
    {
        static const juce::Colour colours[] {
            juce::Colour (0xffffb347), juce::Colour (0xff6fd3c6), juce::Colour (0xffb18cff), juce::Colour (0xff9aa7ff),
            juce::Colour (0xff5fa8ff), juce::Colour (0xffc2e05a), juce::Colour (0xffff7a59), juce::Colour (0xff7fe08a),
            juce::Colour (0xffe58bff), juce::Colour (0xff8fe3ff), juce::Colour (0xffffd166), juce::Colour (0xffff74b8),
            juce::Colour (0xffd9d9d9),
        };
        return colours[juce::jlimit (0, (int) std::size (colours) - 1, lane)];
    }

    // Short labels for the small knobs in each lane.
    juce::String caption (const juce::String& paramId)
    {
        static const std::map<juce::String, juce::String> captions {
            { Params::repeatCount, "REPEATS" },    { Params::repeatPitch, "PITCH" },      { Params::tapeCurve, "CURVE" },
            { Params::stretchSpeed, "SPEED" },     { Params::gatePulses, "PULSES" },      { Params::gateDuty, "LENGTH" },
            { Params::crushAmount, "AMOUNT" },     { Params::distortAmount, "DRIVE" },    { Params::filterDirection, "SWEEP" },
            { Params::filterResonance, "RES" },    { Params::ringFrequency, "FREQ" },     { Params::shuffleRange, "RANGE" },
            { Params::echoTime, "TIME" },          { Params::echoFeedback, "FEEDBACK" },  { Params::pitchSemitones, "SHIFT" },
        };

        const auto found = captions.find (paramId);
        return found != captions.end() ? found->second : paramId.toUpperCase();
    }

    // What a block's value means for its lane, e.g. "8 repeats".
    juce::String valueDescription (int lane, float value)
    {
        GlitchEngine::LaneSettings s;
        GlitchEngine::applyValue (lane, value, s);
        const auto pct = [] (float v) { return juce::String (juce::roundToInt (v * 100.0f)) + "%"; };

        switch (lane)
        {
            case GlitchEngine::repeat: return juce::String (s.repeats) + " repeats";
            case GlitchEngine::tapeStop: return "Curve " + pct (s.tapeCurve);
            case GlitchEngine::stretch: return "Speed " + pct (s.stretchSpeed);
            case GlitchEngine::gate: return juce::String (s.pulses) + " pulses";
            case GlitchEngine::crush: return "Amount " + pct (s.crushAmount);
            case GlitchEngine::distort: return "Drive " + pct (s.driveAmount);
            case GlitchEngine::filter: return "Resonance " + pct (s.resonance);
            case GlitchEngine::ringMod: return juce::String (juce::roundToInt (s.ringFrequency)) + " Hz";
            case GlitchEngine::echo: return "Feedback " + pct (s.echoFeedback);
            case GlitchEngine::pitch: return (s.semitones > 0 ? "+" : "") + juce::String ((int) s.semitones) + " semitones";
            case GlitchEngine::shuffle: return juce::String (s.shuffleRange) + " steps back";
            default: return "No setting";
        }
    }

    bool laneHasValue (int lane) { return lane != GlitchEngine::reverse && lane != GlitchEngine::randomLane; }

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

    // Writes one block, keeping the start step's overrides, and detaches whatever followed it.
    void writeBlock (Patterns::Pattern& pattern, int lane, int start, int end, const GlitchEngine::Step& overrides)
    {
        for (int i = start; i <= end; ++i)
            pattern[(size_t) i] = { lane, i > start, i == start ? overrides.chance : -1.0f, i == start ? overrides.value : -1.0f };

        if (end + 1 < GlitchEngine::numSteps)
            pattern[(size_t) end + 1].tie = false;
    }

    void clearRange (Patterns::Pattern& pattern, int start, int end)
    {
        for (int i = start; i <= end; ++i)
            pattern[(size_t) i] = {};

        if (end + 1 < GlitchEngine::numSteps)
            pattern[(size_t) end + 1].tie = false;
    }
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
            if (cell (lane, step).expanded (2.0f, 3.0f).contains (position))
                return { lane, step };

    return { -1, -1 };
}

int StepGrid::stepAtX (float x) const
{
    for (int step = GlitchEngine::numSteps - 1; step >= 0; --step)
        if (x >= column (step).getX() - 2.0f)
            return step;

    return 0;
}

int StepGrid::blockStart (const Patterns::Pattern& pattern, int step) const
{
    while (step > 0 && pattern[(size_t) step].tie)
        --step;

    return step;
}

int StepGrid::blockEnd (const Patterns::Pattern& pattern, int step) const
{
    while (step + 1 < GlitchEngine::numSteps && pattern[(size_t) step + 1].tie && pattern[(size_t) step + 1].lane == pattern[(size_t) step].lane)
        ++step;

    return step;
}

juce::Rectangle<float> StepGrid::blockBounds (int startStep) const
{
    const auto pattern = processor.getPattern (processor.selectedPattern());
    const auto lane = pattern[(size_t) startStep].lane;

    if (lane < 0)
        return {};

    return cell (lane, startStep).getUnion (cell (lane, blockEnd (pattern, startStep)));
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
    const auto r = area.withSizeKeepingCentre (juce::jmin (area.getWidth() - 10.0f, 24.0f), juce::jmin (area.getHeight() - 12.0f, 14.0f));
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

        case GlitchEngine::stretch:
            for (int i = 0; i < 3; ++i)
            {
                const auto x = r.getX() + r.getWidth() * (float) i / 3.0f;
                p.addRoundedRectangle (x + 1.0f, r.getY() + 2.0f, r.getWidth() / 3.0f - 3.0f, r.getHeight() - 4.0f, 2.0f);
            }
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

        case GlitchEngine::distort:
            p.startNewSubPath (r.getX(), r.getCentreY());
            p.lineTo (r.getX() + r.getWidth() * 0.2f, r.getY());
            p.lineTo (r.getX() + r.getWidth() * 0.45f, r.getY());
            p.lineTo (r.getX() + r.getWidth() * 0.55f, r.getBottom());
            p.lineTo (r.getX() + r.getWidth() * 0.8f, r.getBottom());
            p.lineTo (r.getRight(), r.getCentreY());
            break;

        case GlitchEngine::filter:
            p.startNewSubPath (r.getX(), r.getY() + 3.0f);
            p.lineTo (r.getCentreX() - 2.0f, r.getY() + 3.0f);
            p.quadraticTo (r.getCentreX() + 3.0f, r.getY() + 1.0f, r.getRight(), r.getBottom());
            break;

        case GlitchEngine::ringMod:
            p.startNewSubPath (r.getX(), r.getCentreY());
            for (int i = 1; i <= 24; ++i)
            {
                const auto x = (float) i / 24.0f;
                p.lineTo (r.getX() + x * r.getWidth(), r.getCentreY() - std::sin (x * 4.0f * juce::MathConstants<float>::twoPi) * r.getHeight() * 0.5f);
            }
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

        case GlitchEngine::shuffle:
            p.startNewSubPath (r.getX(), r.getBottom());
            p.lineTo (r.getRight(), r.getY());
            p.startNewSubPath (r.getX(), r.getY());
            p.lineTo (r.getRight(), r.getBottom());
            p.startNewSubPath (r.getRight() - 5.0f, r.getY());
            p.lineTo (r.getRight(), r.getY());
            p.lineTo (r.getRight(), r.getY() + 5.0f);
            break;

        case GlitchEngine::randomLane:
        {
            // A die: rounded square with three pips.
            const auto die = r.withSizeKeepingCentre (r.getHeight(), r.getHeight());
            g.drawRoundedRectangle (die, 2.5f, 1.5f);

            for (const auto pip : { 0.25f, 0.5f, 0.75f })
                g.fillEllipse (juce::Rectangle<float> (2.6f, 2.6f).withCentre ({ die.getX() + die.getWidth() * pip, die.getY() + die.getHeight() * pip }));

            return;
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
    const auto pattern = processor.getPattern (editing);
    const auto playingPattern = engine.currentPattern.load();
    const auto playing = engine.currentStep.load();
    const auto progress = engine.stepProgress.load();
    const auto blockProgress = engine.blockProgress.load();
    const auto firing = engine.firingLane.load();
    const auto length = (int) paramValue (processor, Params::length);
    const auto showingPlayback = editing == playingPattern;

    // Empty cells.
    for (int lane = 0; lane < GlitchEngine::numLanes; ++lane)
    {
        for (int step = 0; step < GlitchEngine::numSteps; ++step)
        {
            auto fill = step / 4 % 2 == 0 ? Theme::raised : Theme::raised.brighter (0.035f);

            if (step == playing && showingPlayback)
                fill = fill.brighter (0.1f);

            g.setColour (step < length ? fill : fill.withAlpha (0.35f));
            g.fillRoundedRectangle (cell (lane, step), 4.0f);

            if (hovered == std::pair { lane, step } && pattern[(size_t) step].lane != lane)
            {
                g.setColour (laneColour (lane).withAlpha (0.7f));
                g.drawRoundedRectangle (cell (lane, step).reduced (0.5f), 4.0f, 1.2f);
            }
        }
    }

    // Blocks.
    for (int start = 0; start < GlitchEngine::numSteps; ++start)
    {
        const auto& s = pattern[(size_t) start];

        if (s.lane < 0 || s.tie)
            continue;

        const auto end = blockEnd (pattern, start);
        const auto area = cell (s.lane, start).getUnion (cell (s.lane, end));
        const auto colour = laneColour (s.lane);
        const auto inLoop = start < length;
        const auto isFiring = showingPlayback && playing >= start && playing <= end && firing == s.lane;

        g.setGradientFill (juce::ColourGradient (colour.brighter (0.15f), area.getTopLeft(), colour.darker (0.25f), area.getBottomLeft(), false));
        g.setOpacity (inLoop ? (isFiring ? 1.0f : 0.82f) : 0.3f);
        g.fillRoundedRectangle (area, 4.0f);
        g.setOpacity (1.0f);

        // Sweep across the block while it plays.
        if (isFiring)
        {
            g.setColour (juce::Colours::white.withAlpha (0.22f));
            g.fillRoundedRectangle (area.withWidth (area.getWidth() * juce::jlimit (0.0f, 1.0f, blockProgress)), 4.0f);
            g.setColour (colour.withAlpha (0.45f));
            g.drawRoundedRectangle (area.expanded (2.5f), 5.0f, 2.0f);
        }

        paintGlyph (g, s.lane, cell (s.lane, start), Theme::background.withAlpha (inLoop ? 0.75f : 0.4f));

        // Marks for blocks with their own settings: a dot for a value, the percentage for chance.
        if (s.value >= 0.0f)
        {
            g.setColour (Theme::background.withAlpha (0.8f));
            g.fillEllipse (juce::Rectangle<float> (5.0f, 5.0f).withCentre ({ area.getRight() - 6.0f, area.getY() + 6.0f }));
        }

        if (s.chance >= 0.0f && area.getWidth() > 34.0f)
        {
            g.setColour (Theme::background.withAlpha (0.8f));
            g.setFont (Theme::font (9.5f, true));
            g.drawText (juce::String (juce::roundToInt (s.chance * 100.0f)) + "%", area.reduced (4.0f, 2.0f), juce::Justification::bottomRight);
        }

        if (hovered.second >= start && hovered.second <= end && hovered.first == s.lane)
        {
            // Resize handle on the right edge.
            g.setColour (Theme::background.withAlpha (0.5f));
            g.fillRoundedRectangle (area.withLeft (area.getRight() - 5.0f).reduced (1.5f, 8.0f), 1.5f);
        }
    }

    // Playhead line through strip and grid.
    if (playing >= 0 && showingPlayback)
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

void StepGrid::applyGesture (int current)
{
    auto pattern = before;
    const auto overrides = originalStart >= 0 ? before[(size_t) originalStart] : GlitchEngine::Step {};

    switch (gesture)
    {
        case Gesture::draw:
            writeBlock (pattern, gestureLane, juce::jmin (anchorStep, current), juce::jmax (anchorStep, current), {});
            break;

        case Gesture::move:
        {
            const auto blockLength = originalEnd - originalStart;
            const auto newStart = juce::jlimit (0, GlitchEngine::numSteps - 1 - blockLength, originalStart + current - anchorStep);
            clearRange (pattern, originalStart, originalEnd);
            writeBlock (pattern, gestureLane, newStart, newStart + blockLength, overrides);
            break;
        }

        case Gesture::resize:
            clearRange (pattern, originalStart, originalEnd);
            writeBlock (pattern, gestureLane, originalStart, juce::jmax (originalStart, current), overrides);
            break;

        case Gesture::none:
            return;
    }

    processor.setPattern (processor.selectedPattern(), pattern);
    repaint();
}

void StepGrid::mouseDown (const juce::MouseEvent& e)
{
    const auto [lane, step] = cellAt (e.position);
    gesture = Gesture::none;

    if (lane < 0)
        return;

    before = processor.getPattern (processor.selectedPattern());
    const auto onBlock = before[(size_t) step].lane == lane;

    if (e.mods.isPopupMenu())
    {
        if (onBlock && onBlockMenu)
        {
            const auto start = blockStart (before, step);
            onBlockMenu (start, blockBounds (start).toNearestInt());
        }

        return;
    }

    if (onEditStart)
        onEditStart();

    gestureLane = lane;
    anchorStep = step;
    dragged = false;

    if (onBlock)
    {
        originalStart = blockStart (before, step);
        originalEnd = blockEnd (before, step);
        const auto nearRightEdge = e.position.x > cell (lane, originalEnd).getRight() - 8.0f;
        gesture = nearRightEdge ? Gesture::resize : Gesture::move;
    }
    else
    {
        originalStart = originalEnd = -1;
        gesture = Gesture::draw;
        applyGesture (step);
    }
}

void StepGrid::mouseDrag (const juce::MouseEvent& e)
{
    if (gesture == Gesture::none)
        return;

    const auto current = stepAtX (e.position.x);
    dragged |= current != anchorStep || e.getDistanceFromDragStart() > 4;

    if (dragged)
        applyGesture (current);
}

void StepGrid::mouseUp (const juce::MouseEvent&)
{
    if (gesture == Gesture::none)
        return;

    // A click on a block without dragging deletes it.
    if (gesture == Gesture::move && ! dragged)
    {
        auto pattern = before;
        clearRange (pattern, originalStart, originalEnd);
        processor.setPattern (processor.selectedPattern(), pattern);
        repaint();
    }

    gesture = Gesture::none;

    if (onEditEnd)
        onEditEnd();
}

void StepGrid::mouseMove (const juce::MouseEvent& e)
{
    hovered = cellAt (e.position);

    if (hovered.first < 0)
    {
        setMouseCursor (juce::MouseCursor::NormalCursor);
        return;
    }

    const auto pattern = processor.getPattern (processor.selectedPattern());
    const auto onBlock = pattern[(size_t) hovered.second].lane == hovered.first;
    const auto nearRightEdge = onBlock && e.position.x > cell (hovered.first, blockEnd (pattern, hovered.second)).getRight() - 8.0f;
    setMouseCursor (nearRightEdge ? juce::MouseCursor::LeftRightResizeCursor
                                  : (onBlock ? juce::MouseCursor::DraggingHandCursor : juce::MouseCursor::PointingHandCursor));
}

void StepGrid::mouseExit (const juce::MouseEvent&)
{
    hovered = { -1, -1 };
}

//==============================================================================
BlockPanel::BlockPanel()
{
    chance.setRange (0.0, 1.0);
    chance.setTextValueSuffix ("");
    chance.textFromValueFunction = [] (double v) { return juce::String (juce::roundToInt (v * 100.0)) + "%"; };
    chance.setTextBoxStyle (juce::Slider::TextBoxRight, true, 48, 18);
    chance.onValueChange = [this]
    {
        step.chance = (float) chance.getValue();

        if (onChange)
            onChange();
    };

    value.setRange (0.0, 1.0);
    value.onValueChange = [this]
    {
        step.value = (float) value.getValue();
        updateValueText();

        if (onChange)
            onChange();
    };

    for (auto* slider : { &chance, &value })
    {
        slider->setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        slider->setColour (juce::Slider::backgroundColourId, Theme::background);
        slider->setColour (juce::Slider::thumbColourId, Theme::text);
        addAndMakeVisible (slider);
    }

    valueText.setFont (Theme::font (12.0f));
    valueText.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (valueText);

    resetButton.onClick = [this]
    {
        step.chance = step.value = -1.0f;
        chance.setValue (laneChance, juce::dontSendNotification);
        value.setValue (0.5, juce::dontSendNotification);
        updateValueText();

        if (onChange)
            onChange();
    };
    closeButton.onClick = [this] { if (onClose) onClose(); };
    addAndMakeVisible (resetButton);
    addAndMakeVisible (closeButton);
}

void BlockPanel::show (int lane, const GlitchEngine::Step& s, float chanceOfLane)
{
    step = s;
    step.lane = lane;
    laneChance = chanceOfLane;

    const auto colour = laneColour (lane);
    chance.setColour (juce::Slider::trackColourId, colour);
    value.setColour (juce::Slider::trackColourId, colour);
    chance.setValue (s.chance >= 0.0f ? s.chance : laneChance, juce::dontSendNotification);
    value.setValue (s.value >= 0.0f ? s.value : 0.5, juce::dontSendNotification);
    value.setEnabled (laneHasValue (lane));
    updateValueText();
    setVisible (true);
    toFront (true);
}

void BlockPanel::updateValueText()
{
    valueText.setText (step.value >= 0.0f ? valueDescription (step.lane, step.value) : "Lane setting", juce::dontSendNotification);
}

void BlockPanel::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat().reduced (1.0f);
    g.setColour (Theme::raised.brighter (0.04f));
    g.fillRoundedRectangle (bounds, 8.0f);
    g.setColour (laneColour (step.lane).withAlpha (0.7f));
    g.drawRoundedRectangle (bounds, 8.0f, 1.2f);

    g.setFont (Theme::caps (11.0f));
    g.setColour (laneColour (step.lane));
    g.drawText (Params::laneNames[step.lane].toUpperCase() + " BLOCK", 14, 10, getWidth() - 28, 16, juce::Justification::centredLeft);

    g.setFont (Theme::caps (9.0f));
    g.setColour (Theme::dim);
    g.drawText ("CHANCE", 14, 36, 70, 14, juce::Justification::centredLeft);
    g.drawText ("VALUE", 14, 72, 70, 14, juce::Justification::centredLeft);
}

void BlockPanel::resized()
{
    chance.setBounds (70, 32, getWidth() - 84, 22);
    value.setBounds (70, 68, getWidth() - 84, 22);
    valueText.setBounds (70, 88, getWidth() - 84, 16);
    resetButton.setBounds (14, getHeight() - 36, 140, 24);
    closeButton.setBounds (getWidth() - 74, getHeight() - 36, 60, 24);
}

//==============================================================================
PromptOverlay::PromptOverlay()
{
    text.setFont (Theme::font (15.0f));
    text.setIndents (8, 6);
    text.onReturnKey = [this] { confirm(); };
    text.onEscapeKey = [this] { setVisible (false); };
    addChildComponent (text);

    okButton.setToggleState (true, juce::dontSendNotification);
    okButton.onClick = [this] { confirm(); };
    cancelButton.onClick = [this] { setVisible (false); };
    addAndMakeVisible (okButton);
    addAndMakeVisible (cancelButton);
}

void PromptOverlay::showPrompt (const juce::String& newTitle, const juce::String& initialText, std::function<void (const juce::String&)> onConfirm)
{
    title = newTitle;
    wantsText = true;
    callback = std::move (onConfirm);
    okButton.setButtonText ("Save");
    text.setText (initialText, false);
    text.setVisible (true);
    text.selectAll();
    setVisible (true);
    toFront (true);
    resized();
    text.grabKeyboardFocus();
}

void PromptOverlay::showConfirm (const juce::String& newTitle, const juce::String& confirmText, std::function<void()> onConfirm)
{
    title = newTitle;
    wantsText = false;
    callback = [onConfirm] (const juce::String&) { onConfirm(); };
    okButton.setButtonText (confirmText);
    text.setVisible (false);
    setVisible (true);
    toFront (true);
    resized();
}

void PromptOverlay::confirm()
{
    if (wantsText && text.getText().trim().isEmpty())
        return;

    auto action = std::move (callback);
    setVisible (false);

    if (action)
        action (text.getText().trim());
}

void PromptOverlay::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black.withAlpha (0.6f));
    g.setColour (Theme::raised);
    g.fillRoundedRectangle (panel().toFloat(), 8.0f);
    g.setColour (Theme::outline);
    g.drawRoundedRectangle (panel().toFloat(), 8.0f, 1.0f);
    g.setColour (Theme::text);
    g.setFont (Theme::font (15.0f, true));
    g.drawFittedText (title, panel().reduced (18, 14).removeFromTop (24), juce::Justification::centredLeft, 1);
}

void PromptOverlay::resized()
{
    auto area = panel().reduced (18, 14);
    area.removeFromTop (30);

    if (wantsText)
    {
        text.setBounds (area.removeFromTop (34));
        area.removeFromTop (14);
    }

    auto buttons = area.removeFromBottom (30);
    okButton.setBounds (buttons.removeFromRight (100));
    buttons.removeFromRight (8);
    cancelButton.setBounds (buttons.removeFromRight (90));
}

void PromptOverlay::mouseDown (const juce::MouseEvent& e)
{
    if (! panel().contains (e.getPosition()))
        setVisible (false);
}

//==============================================================================
FractureEditor::FractureEditor (FractureProcessor& p)
    : AudioProcessorEditor (&p),
      processor (p),
      presets (p),
      patternSelector (*p.apvts.getParameter (Params::pattern), { "A", "B", "C", "D" }),
      grid (p),
      stepSelector (*p.apvts.getParameter (Params::stepLength), Params::stepLengthNames())
{
    setLookAndFeel (&lookAndFeel);
    setWantsKeyboardFocus (true);

    // Header: presets and bypass.
    for (auto* button : { &prevButton, &presetButton, &nextButton, &saveButton, &bypassButton })
        addAndMakeVisible (button);

    prevButton.onClick = [this] { presets.step (-1); undoManager.clearUndoHistory(); };
    nextButton.onClick = [this] { presets.step (1); undoManager.clearUndoHistory(); };
    presetButton.onClick = [this] { showPresetMenu(); };
    saveButton.onClick = [this] { savePreset(); };

    bypassButton.setClickingTogglesState (true);
    bypassButton.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffff6b6b));
    bypassAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (p.apvts, Params::bypass, bypassButton);

    // Toolbar.
    patternSelector.setTooltip ("Pattern to edit and play. MIDI notes C1 to D#1 switch patterns too.");
    addAndMakeVisible (patternSelector);

    chainButton.setClickingTogglesState (true);
    chainButton.setTooltip ("Play the patterns that have blocks in them one after another, a loop each");
    chainAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (p.apvts, Params::chain, chainButton);
    addAndMakeVisible (chainButton);

    for (auto* button : { &patternsButton, &randomButton, &undoButton, &redoButton, &clearButton })
        addAndMakeVisible (button);

    patternsButton.onClick = [this] { showPatternMenu(); };
    randomButton.onClick = [this] { replacePattern (Patterns::randomize ((float) density.getValue(), random)); };
    clearButton.onClick = [this] { replacePattern (Patterns::get (0)); };
    undoButton.onClick = [this] { undoManager.undo(); };
    redoButton.onClick = [this] { undoManager.redo(); };
    randomButton.setTooltip ("A new random pattern; the slider sets how busy it is");
    undoButton.setTooltip ("Undo pattern edit (Cmd+Z)");
    redoButton.setTooltip ("Redo pattern edit (Cmd+Shift+Z)");

    density.setRange (0.1, 1.0);
    density.setValue (0.45, juce::dontSendNotification);
    density.setTooltip ("How busy Randomize makes the pattern");
    density.setColour (juce::Slider::trackColourId, Theme::modules[0]);
    density.setColour (juce::Slider::backgroundColourId, Theme::raised);
    density.setColour (juce::Slider::thumbColourId, Theme::text);
    addAndMakeVisible (density);

    // Grid and lanes.
    grid.onEditStart = [this] { beginEdit(); };
    grid.onEditEnd = [this] { endEdit(); };
    grid.onBlockMenu = [this] (int step, juce::Rectangle<int> bounds) { openBlockPanel (step, bounds); };
    addAndMakeVisible (grid);

    for (int lane = 0; lane < GlitchEngine::numLanes; ++lane)
    {
        laneKnobs[(size_t) lane].push_back (&addKnob (Params::chanceId (lane), "CHANCE", laneColour (lane), false));

        for (const auto& id : Params::laneControls (lane))
            laneKnobs[(size_t) lane].push_back (&addKnob (id, caption (id), laneColour (lane), false));
    }

    blockPanel.onChange = [this]
    {
        if (blockPanelStep < 0)
            return;

        auto pattern = processor.getPattern (processor.selectedPattern());
        const auto edited = blockPanel.getStep();
        pattern[(size_t) blockPanelStep].chance = edited.chance;
        pattern[(size_t) blockPanelStep].value = edited.value;
        processor.setPattern (processor.selectedPattern(), pattern);
    };
    blockPanel.onClose = [this]
    {
        blockPanel.setVisible (false);
        blockPanelStep = -1;
        endEdit();
    };
    addChildComponent (blockPanel);

    // Footer.
    stepSelector.setTooltip ("Length of each step");
    addAndMakeVisible (stepSelector);
    lengthKnob = &addKnob (Params::length, "LENGTH", Theme::text.withAlpha (0.85f), true);
    swingKnob = &addKnob (Params::swing, "SWING", Theme::text.withAlpha (0.85f), true);
    mutateKnob = &addKnob (Params::mutate, "MUTATE", laneColour (GlitchEngine::randomLane), true);
    mixKnob = &addKnob (Params::mix, "MIX", Theme::text.withAlpha (0.85f), true);
    outputKnob = &addKnob (Params::outputGain, "OUTPUT", Theme::text.withAlpha (0.85f), true);
    outputKnob->slider.getProperties().set ("bipolar", true);
    mutateKnob->slider.setTooltip ("Chance each loop that a step or two change by themselves");

    addChildComponent (prompt);

    refreshPresetLabel();
    setResizable (true, true);
    setResizeLimits (1000, 760, 2200, 1600);
    setSize (1180, 880);
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

    if (Patterns::same (after, patternBeforeEdit))
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

void FractureEditor::openBlockPanel (int step, juce::Rectangle<int> bounds)
{
    const auto pattern = processor.getPattern (processor.selectedPattern());
    const auto lane = pattern[(size_t) step].lane;

    if (lane < 0)
        return;

    beginEdit();
    blockPanelStep = step;
    blockPanel.show (lane, pattern[(size_t) step], paramValue (processor, Params::chanceId (lane)));

    // Below the block if there's room, otherwise above it.
    const auto blockInEditor = bounds.translated (grid.getX(), grid.getY());
    auto panelBounds = juce::Rectangle<int> (300, 150).withPosition (blockInEditor.getX(), blockInEditor.getBottom() + 6);

    if (panelBounds.getBottom() > getHeight() - 8)
        panelBounds.setY (blockInEditor.getY() - panelBounds.getHeight() - 6);

    blockPanel.setBounds (panelBounds.constrainedWithin (getLocalBounds().reduced (8)));
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
void FractureEditor::refreshPresetLabel()
{
    const auto text = processor.getPresetName() + (processor.isPresetDirty() ? " *" : "");

    if (presetButton.getButtonText() != text)
        presetButton.setButtonText (text);
}

void FractureEditor::showPresetMenu()
{
    presets.refresh();

    const auto& list = presets.getPresets();
    const auto current = presets.getCurrentIndex();

    // Factory presets as a tree: categories like "Glitch 2 Library/Supremeja/Delay" become nested submenus.
    struct Node
    {
        juce::String name;
        std::vector<Node> children;
        std::vector<int> items;

        Node& child (const juce::String& childName)
        {
            for (auto& c : children)
                if (c.name == childName)
                    return c;

            children.push_back ({ childName });
            return children.back();
        }
    };

    Node root;

    for (int i = 0; i < (int) list.size(); ++i)
    {
        if (! list[(size_t) i].factory)
            continue;

        auto* node = &root;

        for (const auto& part : juce::StringArray::fromTokens (list[(size_t) i].category, "/", ""))
            node = &node->child (part);

        node->items.push_back (i);
    }

    std::function<bool (const Node&, juce::PopupMenu&)> build = [&] (const Node& node, juce::PopupMenu& target)
    {
        auto containsCurrent = false;

        for (const auto i : node.items)
        {
            target.addItem (i + 1, list[(size_t) i].name, true, i == current);
            containsCurrent |= i == current;
        }

        for (const auto& c : node.children)
        {
            juce::PopupMenu sub;
            const auto inside = build (c, sub);
            target.addSubMenu (c.name, sub, true, nullptr, inside);
            containsCurrent |= inside;
        }

        return containsCurrent;
    };

    juce::PopupMenu menu;
    build (root, menu);

    menu.addSectionHeader ("User");
    auto anyUser = false;

    for (int i = 0; i < (int) list.size(); ++i)
    {
        if (! list[(size_t) i].factory)
        {
            menu.addItem (i + 1, list[(size_t) i].name, true, i == current);
            anyUser = true;
        }
    }

    if (! anyUser)
        menu.addItem (-1, "No saved presets yet", false, false);

    constexpr int saveId = 10000, saveAsId = 10001, deleteId = 10002, revealId = 10003;
    menu.addSeparator();
    menu.addItem (saveId, "Save");
    menu.addItem (saveAsId, "Save As...");
    menu.addItem (deleteId, "Delete \"" + processor.getPresetName() + "\"...", presets.isCurrentUserPreset());
    menu.addSeparator();
    menu.addItem (revealId, "Show Presets Folder");

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&presetButton).withMinimumWidth (presetButton.getWidth()),
                        [this, current] (int result)
                        {
                            if (result <= 0)
                                return;

                            switch (result)
                            {
                                case saveId: savePreset(); break;
                                case saveAsId: savePresetAs(); break;
                                case revealId:
                                    presets.getUserDirectory().createDirectory();
                                    presets.getUserDirectory().revealToUser();
                                    break;
                                case deleteId:
                                    prompt.showConfirm ("Delete \"" + processor.getPresetName() + "\"?", "Delete",
                                                        [this, current] { presets.remove (current); });
                                    break;
                                default:
                                    presets.load (result - 1);
                                    undoManager.clearUndoHistory();
                                    break;
                            }
                        });
}

void FractureEditor::savePreset()
{
    // Factory presets can't be overwritten, so saving one becomes Save As.
    if (presets.isCurrentUserPreset())
        presets.save (processor.getPresetName());
    else
        savePresetAs();
}

void FractureEditor::savePresetAs()
{
    const auto suggestion = processor.getPresetName() + (presets.isCurrentUserPreset() ? "" : " (copy)");

    prompt.showPrompt ("Save preset", suggestion, [this] (const juce::String& name)
    {
        if (! presets.save (name))
            prompt.showConfirm ("Couldn't save to " + presets.getUserDirectory().getFullPathName(), "OK", [] {});
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
    const auto m = juce::Rectangle<float> ((float) margin, 17.0f, 26.0f, 22.0f);
    juce::Path logo;
    logo.startNewSubPath (m.getX(), m.getCentreY());
    logo.lineTo (m.getX() + 8.0f, m.getCentreY());
    logo.startNewSubPath (m.getX() + 11.0f, m.getY());
    logo.lineTo (m.getX() + 15.0f, m.getBottom());
    logo.startNewSubPath (m.getX() + 18.0f, m.getCentreY());
    logo.lineTo (m.getRight(), m.getCentreY());
    g.setGradientFill (juce::ColourGradient (laneColour (GlitchEngine::shuffle), m.getTopLeft(), laneColour (GlitchEngine::gate), m.getBottomRight(), false));
    g.strokePath (logo, juce::PathStrokeType (2.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    auto title = juce::Rectangle<int> (margin + 36, 0, 260, headerHeight);
    g.setColour (Theme::text);
    g.setFont (Theme::caps (18.0f).withExtraKerningFactor (0.3f));
    g.drawText ("FRACTURE", title, juce::Justification::centredLeft);
    g.setColour (Theme::faint);
    g.setFont (Theme::font (11.0f));
    g.drawText ("v" FRACTURE_VERSION, title.withTrimmedLeft (150), juce::Justification::centredLeft);

    // Toolbar captions.
    g.setFont (Theme::caps (9.5f));
    g.setColour (Theme::dim);
    g.drawText ("PATTERN", toolbarArea.withWidth (70), juce::Justification::centredLeft);

    // Strip caption: what's playing right now.
    const auto& engine = processor.engine;
    const auto stripLabel = juce::Rectangle<int> (lanesArea.getX(), grid.getY(), laneHeaderWidth, StepGrid::stripHeight);
    g.setColour (Theme::panel);
    g.fillRoundedRectangle (stripLabel.toFloat(), 6.0f);

    const auto playing = engine.currentStep.load();
    const auto firing = engine.firingLane.load();
    auto label = stripLabel.reduced (14, 10);
    g.setFont (Theme::caps (10.0f));
    g.setColour (Theme::dim);
    g.drawText ("NOW PLAYING", label.removeFromTop (14), juce::Justification::centredLeft);
    g.setFont (Theme::caps (16.0f));
    g.setColour (firing >= 0 ? laneColour (firing) : Theme::text);
    g.drawText (firing >= 0 ? Params::laneNames[firing].toUpperCase() : (playing < 0 ? "STOPPED" : "DRY"),
                label.removeFromTop (24), juce::Justification::centredLeft);
    g.setFont (Theme::font (11.5f));
    g.setColour (Theme::dim);
    const auto patternLetter = juce::String::charToString ((juce::juce_wchar) ('A' + engine.currentPattern.load()));
    g.drawText (playing < 0 ? "Press play, or hold a MIDI note from C2" : "Pattern " + patternLetter + ", step " + juce::String (playing + 1),
                label, juce::Justification::centredLeft);

    // Lane headers.
    for (int lane = 0; lane < GlitchEngine::numLanes; ++lane)
    {
        const auto row = juce::Rectangle<int> (lanesArea.getX(), grid.getY() + grid.rowTop (lane), laneHeaderWidth, rowHeight);
        const auto colour = laneColour (lane);
        const auto isFiring = firing == lane;

        g.setColour (isFiring ? colour.withAlpha (0.14f) : Theme::panel);
        g.fillRoundedRectangle (row.reduced (0, 2).toFloat(), 4.0f);
        g.setColour (colour);
        g.fillRoundedRectangle (row.reduced (0, 10).withWidth (3).toFloat(), 1.5f);

        g.setFont (Theme::caps (10.5f));
        g.setColour (isFiring ? colour.brighter (0.4f) : Theme::text);
        g.drawText (Params::laneNames[lane].toUpperCase(), row.withTrimmedLeft (14).withWidth (96), juce::Justification::centredLeft);

        g.setFont (Theme::caps (7.5f));
        g.setColour (Theme::dim);

        for (auto* knob : laneKnobs[(size_t) lane])
            g.drawText (knob->caption, knob->slider.getBounds().withY (knob->slider.getBottom() - 1).withHeight (10).expanded (12, 0),
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

    g.setFont (Theme::font (10.5f));
    g.setColour (Theme::faint);
    g.drawText ("MIDI: C1-D#1 switch patterns, C2 and up play an effect while held. Right-click a block for its own settings.",
                stepSelector.getBounds().withY (stepSelector.getBottom() + 6).withHeight (14).withWidth (620), juce::Justification::centredLeft);
}

void FractureEditor::resized()
{
    auto bounds = getLocalBounds();
    prompt.setBounds (bounds);

    // Header: logo | preset bar | bypass.
    auto header = bounds.removeFromTop (headerHeight).reduced (margin, 12);
    bypassButton.setBounds (header.removeFromRight (80));

    auto presetBar = header.withSizeKeepingCentre (juce::jmin (440, header.getWidth() - 420), header.getHeight());
    saveButton.setBounds (presetBar.removeFromRight (64));
    presetBar.removeFromRight (6);
    nextButton.setBounds (presetBar.removeFromRight (32));
    presetBar.removeFromRight (4);
    prevButton.setBounds (presetBar.removeFromLeft (32));
    presetBar.removeFromLeft (4);
    presetButton.setBounds (presetBar);

    // Toolbar: PATTERN A B C D Chain | Patterns Randomize density | Undo Redo Clear.
    toolbarArea = bounds.removeFromTop (toolbarHeight).reduced (margin, 8);
    auto toolbar = toolbarArea;
    toolbar.removeFromLeft (70);
    patternSelector.setBounds (toolbar.removeFromLeft (176));
    toolbar.removeFromLeft (8);
    chainButton.setBounds (toolbar.removeFromLeft (66));

    clearButton.setBounds (toolbar.removeFromRight (64));
    toolbar.removeFromRight (6);
    redoButton.setBounds (toolbar.removeFromRight (56));
    toolbar.removeFromRight (4);
    undoButton.setBounds (toolbar.removeFromRight (56));
    toolbar.removeFromRight (20);
    density.setBounds (toolbar.removeFromRight (90));
    toolbar.removeFromRight (4);
    randomButton.setBounds (toolbar.removeFromRight (98));
    toolbar.removeFromRight (6);
    patternsButton.setBounds (toolbar.removeFromRight (92));

    // Footer: step length | length, swing, mutate | mix, output.
    footerArea = bounds.removeFromBottom (footerHeight);
    auto footer = footerArea.reduced (margin, 10);
    stepSelector.setBounds (footer.removeFromLeft (360).withTrimmedTop (22).withHeight (28));

    const auto placeKnob = [] (Knob* knob, juce::Rectangle<int> area) { knob->slider.setBounds (area.withTrimmedTop (14)); };

    auto right = footer.removeFromRight (180);
    placeKnob (outputKnob, right.removeFromRight (90));
    placeKnob (mixKnob, right);

    auto middle = footer.withSizeKeepingCentre (270, footer.getHeight()).translated (60, 0);
    placeKnob (lengthKnob, middle.removeFromLeft (90));
    placeKnob (swingKnob, middle.removeFromLeft (90));
    placeKnob (mutateKnob, middle);

    // Lanes: headers on the left, grid (with the waveform strip on top) on the right.
    lanesArea = bounds.reduced (margin, 8);
    rowHeight = (lanesArea.getHeight() - StepGrid::stripHeight - 8 - StepGrid::numbersHeight) / GlitchEngine::numLanes;
    grid.setRowHeight (rowHeight);
    grid.setBounds (lanesArea.withTrimmedLeft (laneHeaderWidth + 12));

    const auto knobSize = juce::jlimit (22, 30, rowHeight - 16);

    for (int lane = 0; lane < GlitchEngine::numLanes; ++lane)
    {
        auto row = juce::Rectangle<int> (lanesArea.getX() + 108, grid.getY() + grid.rowTop (lane), laneHeaderWidth - 112, rowHeight);

        for (auto* knob : laneKnobs[(size_t) lane])
            knob->slider.setBounds (row.removeFromLeft (60).withSizeKeepingCentre (knobSize, knobSize).translated (0, -5));
    }
}

void FractureEditor::timerCallback()
{
    grid.repaint();
    repaint (lanesArea.withWidth (laneHeaderWidth));

    undoButton.setEnabled (undoManager.canUndo());
    redoButton.setEnabled (undoManager.canRedo());
    refreshPresetLabel();
}
