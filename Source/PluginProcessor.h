#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "Engine.h"
#include "Patterns.h"

namespace Params
{
    inline const juce::StringArray laneIds { "repeat", "reverse", "tape", "gate", "crush", "filter", "shuffle" };
    inline const juce::StringArray laneNames { "Repeat", "Reverse", "Tape Stop", "Gate", "Crush", "Filter", "Shuffle" };

    inline juce::String chanceId (int lane) { return laneIds[lane] + "Chance"; }

    inline const juce::String repeatCount { "repeatCount" };
    inline const juce::String repeatPitch { "repeatPitch" };
    inline const juce::String tapeCurve { "tapeCurve" };
    inline const juce::String gatePulses { "gatePulses" };
    inline const juce::String gateDuty { "gateDuty" };
    inline const juce::String crushAmount { "crushAmount" };
    inline const juce::String filterDirection { "filterDirection" };
    inline const juce::String filterResonance { "filterResonance" };
    inline const juce::String shuffleRange { "shuffleRange" };

    inline const juce::String stepLength { "stepLength" };
    inline const juce::String mix { "mix" };
    inline const juce::String outputGain { "outputGain" };
    inline const juce::String bypass { "bypass" };

    juce::StringArray stepLengthNames();
    double stepBeats (int index);

    // The extra controls each lane shows next to its chance knob.
    juce::StringArray laneControls (int lane);
}

class FractureProcessor : public juce::AudioProcessor
{
public:
    FractureProcessor();

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorParameter* getBypassParameter() const override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Fracture"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    Patterns::Pattern getPattern() const;
    void setPattern (const Patterns::Pattern&);

    juce::AudioProcessorValueTreeState apvts;
    GlitchEngine engine;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    GlitchEngine::Settings readSettings() const;
    float param (const juce::String& id) const { return apvts.getRawParameterValue (id)->load(); }

    double freeRunPpq = 0.0;
    juce::SmoothedValue<float> mixSmoothed, outputSmoothed;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (FractureProcessor)
};
