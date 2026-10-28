#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace Params
{
    juce::StringArray stepLengthNames() { return { "1/8", "1/16", "1/32", "1/8T", "1/16T" }; }

    double stepBeats (int index)
    {
        static constexpr double beats[] { 0.5, 0.25, 0.125, 1.0 / 3.0, 1.0 / 6.0 };
        return beats[juce::jlimit (0, (int) std::size (beats) - 1, index)];
    }

    juce::StringArray laneControls (int lane)
    {
        switch (lane)
        {
            case GlitchEngine::repeat: return { repeatCount, repeatPitch };
            case GlitchEngine::tapeStop: return { tapeCurve };
            case GlitchEngine::stretch: return { stretchSpeed };
            case GlitchEngine::distort: return { distortAmount };
            case GlitchEngine::ringMod: return { ringFrequency };
            case GlitchEngine::gate: return { gatePulses, gateDuty };
            case GlitchEngine::crush: return { crushAmount };
            case GlitchEngine::filter: return { filterDirection, filterResonance };
            case GlitchEngine::shuffle: return { shuffleRange };
            case GlitchEngine::echo: return { echoTime, echoFeedback };
            case GlitchEngine::pitch: return { pitchSemitones };
            default: return {};
        }
    }
}

namespace
{
    juce::String percent (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)) + "%"; }
}

FractureProcessor::FractureProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PARAMS", createLayout())
{
    for (auto* parameter : getParameters())
        parameter->addListener (this);
}

FractureProcessor::~FractureProcessor()
{
    for (auto* parameter : getParameters())
        parameter->removeListener (this);
}

void FractureProcessor::parameterValueChanged (int parameterIndex, float)
{
    // Bypass and the selected pattern don't change the sound a preset describes.
    const auto* parameter = getParameters()[parameterIndex];

    if (parameter != getBypassParameter() && parameter != apvts.getParameter (Params::pattern))
        markPresetDirty();
}

juce::AudioProcessorValueTreeState::ParameterLayout FractureProcessor::createLayout()
{
    using namespace juce;
    using namespace Params;

    AudioProcessorValueTreeState::ParameterLayout layout;
    const auto pct = AudioParameterFloatAttributes().withStringFromValueFunction (percent);

    for (int lane = 0; lane < GlitchEngine::numLanes; ++lane)
        layout.add (std::make_unique<AudioParameterFloat> (ParameterID { chanceId (lane), 1 }, laneNames[lane] + " Chance",
                                                           NormalisableRange<float> (0.0f, 1.0f), 1.0f, pct));

    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { repeatCount, 1 }, "Repeats", StringArray { "2", "3", "4", "6", "8", "16" }, 2));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { repeatPitch, 1 }, "Repeat Pitch Drop", NormalisableRange<float> (0.0f, 1.0f), 0.0f, pct));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { tapeCurve, 1 }, "Tape Stop Curve", NormalisableRange<float> (0.0f, 1.0f), 0.5f, pct));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { stretchSpeed, 1 }, "Stretch Speed", NormalisableRange<float> (0.25f, 1.0f), 0.5f, pct));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { distortAmount, 1 }, "Distort Amount", NormalisableRange<float> (0.0f, 1.0f), 0.6f, pct));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { ringFrequency, 1 }, "Ring Mod Frequency",
                                                       NormalisableRange<float> (30.0f, 2000.0f, 0.0f, 0.3f), 220.0f,
                                                       AudioParameterFloatAttributes().withStringFromValueFunction (
                                                           [] (float v, int) { return String (roundToInt (v)) + " Hz"; })));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { gatePulses, 1 }, "Gate Pulses", StringArray { "2", "3", "4", "6", "8" }, 2));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { gateDuty, 1 }, "Gate Length", NormalisableRange<float> (0.1f, 0.9f), 0.5f, pct));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { crushAmount, 1 }, "Crush Amount", NormalisableRange<float> (0.0f, 1.0f), 0.6f, pct));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { filterDirection, 1 }, "Filter Sweep", StringArray { "Close", "Open" }, 0));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { filterResonance, 1 }, "Filter Resonance", NormalisableRange<float> (0.0f, 1.0f), 0.4f, pct));
    layout.add (std::make_unique<AudioParameterInt> (ParameterID { shuffleRange, 1 }, "Shuffle Range", 1, 8, 4,
                                                     AudioParameterIntAttributes().withStringFromValueFunction (
                                                         [] (int v, int) { return String (v) + (v == 1 ? " step" : " steps"); })));

    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { echoTime, 1 }, "Echo Time", StringArray { "1/2", "1/3", "1/4", "1/8" }, 2));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { echoFeedback, 1 }, "Echo Feedback", NormalisableRange<float> (0.0f, 0.9f), 0.6f, pct));
    layout.add (std::make_unique<AudioParameterInt> (ParameterID { pitchSemitones, 1 }, "Pitch", -24, 24, -12,
                                                     AudioParameterIntAttributes().withStringFromValueFunction (
                                                         [] (int v, int) { return (v > 0 ? "+" : "") + String (v) + " st"; })));

    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { pattern, 1 }, "Pattern", StringArray { "A", "B", "C", "D" }, 0));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { chain, 1 }, "Chain Patterns", false));
    layout.add (std::make_unique<AudioParameterInt> (ParameterID { length, 1 }, "Pattern Length", 1, GlitchEngine::numSteps, GlitchEngine::numSteps,
                                                     AudioParameterIntAttributes().withStringFromValueFunction (
                                                         [] (int v, int) { return String (v) + " steps"; })));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { swing, 1 }, "Swing", NormalisableRange<float> (0.0f, 0.5f), 0.0f,
                                                       AudioParameterFloatAttributes().withStringFromValueFunction (
                                                           [] (float v, int) { return String (roundToInt (v * 200.0f)) + "%"; })));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { mutate, 1 }, "Mutate", NormalisableRange<float> (0.0f, 1.0f), 0.0f, pct));

    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { stepLength, 1 }, "Step Length", stepLengthNames(), 1));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { mix, 1 }, "Mix", NormalisableRange<float> (0.0f, 1.0f), 1.0f, pct));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { protect, 1 }, "Protect Attacks", NormalisableRange<float> (0.0f, 1.0f), 0.0f, pct));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { outputGain, 1 }, "Output", NormalisableRange<float> (-24.0f, 12.0f, 0.1f), 0.0f,
                                                       AudioParameterFloatAttributes().withStringFromValueFunction (
                                                           [] (float v, int) { return (v > 0.0f ? "+" : "") + String (v, 1) + " dB"; })));
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { bypass, 1 }, "Bypass", false));

    return layout;
}

juce::AudioProcessorParameter* FractureProcessor::getBypassParameter() const
{
    return apvts.getParameter (Params::bypass);
}

bool FractureProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& out = layouts.getMainOutputChannelSet();
    return (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo()) && out == layouts.getMainInputChannelSet();
}

void FractureProcessor::prepareToPlay (double sampleRate, int)
{
    engine.prepare (sampleRate, getTotalNumOutputChannels());
    mixSmoothed.reset (sampleRate, 0.03);
    mixSmoothed.setCurrentAndTargetValue (param (Params::bypass) > 0.5f ? 0.0f : param (Params::mix));
    outputSmoothed.reset (sampleRate, 0.03);
    outputSmoothed.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (param (Params::outputGain)));
}

GlitchEngine::Settings FractureProcessor::readSettings() const
{
    using namespace Params;
    GlitchEngine::Settings settings;

    for (int lane = 0; lane < GlitchEngine::numLanes; ++lane)
        settings[(size_t) lane].chance = param (chanceId (lane));

    static constexpr int repeatChoices[] { 2, 3, 4, 6, 8, 16 };
    static constexpr int pulseChoices[] { 2, 3, 4, 6, 8 };

    auto& r = settings[GlitchEngine::repeat];
    r.repeats = repeatChoices[juce::jlimit (0, 5, (int) param (repeatCount))];
    r.pitch = param (repeatPitch);
    settings[GlitchEngine::tapeStop].tapeCurve = param (tapeCurve);
    settings[GlitchEngine::stretch].stretchSpeed = param (stretchSpeed);
    settings[GlitchEngine::distort].driveAmount = param (distortAmount);
    settings[GlitchEngine::ringMod].ringFrequency = param (ringFrequency);
    settings[GlitchEngine::gate].pulses = pulseChoices[juce::jlimit (0, 4, (int) param (gatePulses))];
    settings[GlitchEngine::gate].duty = param (gateDuty);
    settings[GlitchEngine::crush].crushAmount = param (crushAmount);
    settings[GlitchEngine::filter].filterOpens = (int) param (filterDirection) == 1;
    settings[GlitchEngine::filter].resonance = param (filterResonance);
    settings[GlitchEngine::shuffle].shuffleRange = (int) param (shuffleRange);

    static constexpr int echoDivisions[] { 2, 3, 4, 8 };
    settings[GlitchEngine::echo].echoDivision = echoDivisions[juce::jlimit (0, 3, (int) param (echoTime))];
    settings[GlitchEngine::echo].echoFeedback = param (echoFeedback);
    settings[GlitchEngine::pitch].semitones = param (pitchSemitones);
    return settings;
}

void FractureProcessor::handleMidi (const juce::MidiBuffer& midi)
{
    for (const auto metadata : midi)
    {
        const auto message = metadata.getMessage();
        const auto note = message.getNoteNumber();

        if (message.isNoteOn())
        {
            if (juce::isPositiveAndBelow (note - firstPatternNote, GlitchEngine::numPatterns))
                engine.setPatternOverride (note - firstPatternNote);
            else if (juce::isPositiveAndBelow (note - firstLiveNote, (int) GlitchEngine::numLanes))
            {
                heldLiveNote = note;
                engine.setLiveLane (note - firstLiveNote);
            }
        }
        else if (message.isNoteOff() && note == heldLiveNote)
        {
            heldLiveNote = -1;
            engine.setLiveLane (GlitchEngine::noEffect);
        }
        else if (message.isAllNotesOff() || message.isAllSoundOff())
        {
            heldLiveNote = -1;
            engine.setLiveLane (GlitchEngine::noEffect);
        }
    }
}

void FractureProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    handleMidi (midi);
    midi.clear();

    for (auto ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());

    GlitchEngine::Timing timing;
    timing.stepBeats = Params::stepBeats ((int) param (Params::stepLength));
    timing.swing = param (Params::swing);
    timing.length = (int) param (Params::length);
    timing.pattern = (int) param (Params::pattern);
    timing.chain = param (Params::chain) > 0.5f;
    timing.mutate = param (Params::mutate);
    auto hasHost = false;

    if (auto* playHead = getPlayHead())
    {
        if (auto position = playHead->getPosition())
        {
            hasHost = true;

            if (auto bpm = position->getBpm(); bpm && *bpm > 0.0)
                timing.bpm = *bpm;

            if (auto ppq = position->getPpqPosition(); ppq && position->getIsPlaying())
            {
                timing.running = true;
                timing.ppq = *ppq;
            }
        }
    }

    // The pattern plays only while the host is playing; without a host (standalone) it runs at 120 bpm.
    if (! hasHost)
    {
        timing.running = true;
        timing.ppq = freeRunPpq;
        freeRunPpq += buffer.getNumSamples() * timing.bpm / 60.0 / getSampleRate();
    }

    // Mix and bypass are smoothed per block; the engine fades each effect in and out itself.
    mixSmoothed.setTargetValue (param (Params::bypass) > 0.5f ? 0.0f : param (Params::mix));
    const auto mix = mixSmoothed.skip (buffer.getNumSamples());

    engine.process (buffer, timing, readSettings(), mix, param (Params::protect));

    outputSmoothed.setTargetValue (juce::Decibels::decibelsToGain (param (Params::outputGain)));
    outputSmoothed.applyGain (buffer, buffer.getNumSamples());
}

Patterns::Pattern FractureProcessor::getPattern (int index) const
{
    Patterns::Pattern pattern {};

    for (int i = 0; i < GlitchEngine::numSteps; ++i)
        pattern[(size_t) i] = engine.getStep (index, i);

    return pattern;
}

void FractureProcessor::setPattern (int index, const Patterns::Pattern& pattern)
{
    for (int i = 0; i < GlitchEngine::numSteps; ++i)
        engine.setStep (index, i, pattern[(size_t) i]);

    markPresetDirty();
    updateHostDisplay (ChangeDetails().withNonParameterStateChanged (true));
}

namespace
{
    juce::String patternKey (int index) { return "pattern" + juce::String::charToString ((juce::juce_wchar) ('A' + index)); }
}

juce::ValueTree FractureProcessor::getStateTree()
{
    auto state = apvts.copyState();

    for (int p = 0; p < GlitchEngine::numPatterns; ++p)
    {
        const auto pattern = getPattern (p);
        state.setProperty (patternKey (p), Patterns::toString (pattern), nullptr);

        if (const auto overrides = Patterns::overridesToString (pattern); overrides.isNotEmpty())
            state.setProperty (patternKey (p) + "Overrides", overrides, nullptr);
    }

    state.setProperty ("presetName", getPresetName(), nullptr);
    state.setProperty ("presetDirty", isPresetDirty(), nullptr);
    return state;
}

void FractureProcessor::setStateTree (const juce::ValueTree& source)
{
    if (! source.hasType (apvts.state.getType()))
        return;

    auto state = source.createCopy();
    const ScopedPresetLoad loading (*this);

    // Patterns live in atomics, so they can be restored from any thread. 0.1 had a single "pattern".
    if (state.hasProperty ("pattern"))
        state.setProperty (patternKey (0), state.getProperty ("pattern"), nullptr);

    for (int p = 0; p < GlitchEngine::numPatterns; ++p)
    {
        auto pattern = Patterns::fromString (state.getProperty (patternKey (p), "").toString());
        Patterns::overridesFromString (state.getProperty (patternKey (p) + "Overrides", "").toString(), pattern);
        setPattern (p, pattern);
        state.removeProperty (patternKey (p), nullptr);
        state.removeProperty (patternKey (p) + "Overrides", nullptr);
    }

    if (state.hasProperty ("presetName"))
        setPresetName (state.getProperty ("presetName").toString());

    const auto dirty = (bool) state.getProperty ("presetDirty", false);
    state.removeProperty ("pattern", nullptr);
    state.removeProperty ("presetName", nullptr);
    state.removeProperty ("presetDirty", nullptr);
    apvts.replaceState (state);
    presetDirty = dirty;
}

void FractureProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = getStateTree().createXml())
        copyXmlToBinary (*xml, destData);
}

void FractureProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes); xml != nullptr && xml->hasTagName (apvts.state.getType()))
        setStateTree (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* FractureProcessor::createEditor()
{
    return new FractureEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new FractureProcessor();
}
