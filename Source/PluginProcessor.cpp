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
            case GlitchEngine::gate: return { gatePulses, gateDuty };
            case GlitchEngine::crush: return { crushAmount };
            case GlitchEngine::filter: return { filterDirection, filterResonance };
            case GlitchEngine::shuffle: return { shuffleRange };
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
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { gatePulses, 1 }, "Gate Pulses", StringArray { "2", "3", "4", "6", "8" }, 2));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { gateDuty, 1 }, "Gate Length", NormalisableRange<float> (0.1f, 0.9f), 0.5f, pct));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { crushAmount, 1 }, "Crush Amount", NormalisableRange<float> (0.0f, 1.0f), 0.6f, pct));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { filterDirection, 1 }, "Filter Sweep", StringArray { "Close", "Open" }, 0));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { filterResonance, 1 }, "Filter Resonance", NormalisableRange<float> (0.0f, 1.0f), 0.4f, pct));
    layout.add (std::make_unique<AudioParameterInt> (ParameterID { shuffleRange, 1 }, "Shuffle Range", 1, 8, 4,
                                                     AudioParameterIntAttributes().withStringFromValueFunction (
                                                         [] (int v, int) { return String (v) + (v == 1 ? " step" : " steps"); })));

    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { stepLength, 1 }, "Step Length", stepLengthNames(), 1));
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { mix, 1 }, "Mix", NormalisableRange<float> (0.0f, 1.0f), 1.0f, pct));
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
    settings[GlitchEngine::gate].pulses = pulseChoices[juce::jlimit (0, 4, (int) param (gatePulses))];
    settings[GlitchEngine::gate].duty = param (gateDuty);
    settings[GlitchEngine::crush].crushAmount = param (crushAmount);
    settings[GlitchEngine::filter].filterOpens = (int) param (filterDirection) == 1;
    settings[GlitchEngine::filter].resonance = param (filterResonance);
    settings[GlitchEngine::shuffle].shuffleRange = (int) param (shuffleRange);
    return settings;
}

void FractureProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    for (auto ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());

    GlitchEngine::Timing timing;
    timing.stepBeats = Params::stepBeats ((int) param (Params::stepLength));
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

    engine.process (buffer, timing, readSettings(), mix);

    outputSmoothed.setTargetValue (juce::Decibels::decibelsToGain (param (Params::outputGain)));
    outputSmoothed.applyGain (buffer, buffer.getNumSamples());
}

Patterns::Pattern FractureProcessor::getPattern() const
{
    Patterns::Pattern pattern;

    for (int i = 0; i < GlitchEngine::numSteps; ++i)
        pattern[(size_t) i] = engine.getStep (i);

    return pattern;
}

void FractureProcessor::setPattern (const Patterns::Pattern& pattern)
{
    for (int i = 0; i < GlitchEngine::numSteps; ++i)
        engine.setStep (i, pattern[(size_t) i]);

    updateHostDisplay (ChangeDetails().withNonParameterStateChanged (true));
}

void FractureProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("pattern", Patterns::toString (getPattern()), nullptr);

    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void FractureProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);

    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        return;

    auto state = juce::ValueTree::fromXml (*xml);

    // The pattern lives in atomics, so it can be restored from any thread.
    if (state.hasProperty ("pattern"))
        setPattern (Patterns::fromString (state.getProperty ("pattern").toString()));

    state.removeProperty ("pattern", nullptr);
    apvts.replaceState (state);
}

juce::AudioProcessorEditor* FractureProcessor::createEditor()
{
    return new FractureEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new FractureProcessor();
}
