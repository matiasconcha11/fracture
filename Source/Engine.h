#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include "Dsp.h"

#include <array>
#include <atomic>

// The glitch engine: a 16-step pattern where each step can hold one effect. Audio is recorded
// continuously, so effects can replay, reverse or slow down what just happened.
class GlitchEngine
{
public:
    static constexpr int numSteps = 16;

    enum Lane
    {
        repeat,
        reverse,
        tapeStop,
        gate,
        crush,
        filter,
        shuffle,
        numLanes
    };

    static constexpr int noEffect = -1;

    struct LaneSettings
    {
        float chance = 1.0f;       // probability a step on this lane fires
        int repeats = 4;           // Repeat: slices per step
        float pitch = 0.0f;        // Repeat: how much each repeat drops in pitch
        float tapeCurve = 0.5f;    // Tape Stop: 0 slows evenly, 1 holds speed then stops late
        int pulses = 4;            // Gate: pulses per step
        float duty = 0.5f;         // Gate: open part of each pulse
        float crushAmount = 0.6f;  // Crush
        bool filterOpens = false;  // Filter: sweep up instead of down
        float resonance = 0.4f;    // Filter
        int shuffleRange = 4;      // Shuffle: how many steps back it may reach
    };

    using Settings = std::array<LaneSettings, numLanes>;

    struct Timing
    {
        bool running = false;   // false: pass audio through untouched
        double ppq = 0.0;       // position of the first sample, in quarter notes
        double bpm = 120.0;
        double stepBeats = 0.25; // length of one step in quarter notes
    };

    GlitchEngine();

    // Clears the recording, not the pattern.
    void prepare (double sampleRate, int numChannels);

    // Pattern, shared with the editor: one lane per step or noEffect. Any thread.
    void setStep (int step, int lane) { pattern[(size_t) step].store (lane); }
    int getStep (int step) const { return pattern[(size_t) step].load(); }

    void process (juce::AudioBuffer<float>& buffer, const Timing&, const Settings&, float mix);

    // For the editor's playhead.
    std::atomic<int> currentStep { -1 };
    std::atomic<int> firingLane { noEffect };

    void setRandomSeed (juce::int64 seed) { random.setSeed (seed); }

private:
    void startStep (juce::int64 stepNumber, double samplesIntoStep, const Settings&);
    float read (int channel, double position) const;
    float effectSample (int channel, float input, double t, const LaneSettings&);

    double sampleRate = 44100.0;
    double stepLength = 0.0; // samples

    // Recording of everything that came in, addressed by absolute sample number.
    std::vector<std::vector<float>> history;
    juce::int64 written = 0;

    std::array<std::atomic<int>, numSteps> pattern {};

    juce::int64 currentStepNumber = -1;
    juce::int64 stepStart = 0; // absolute sample number where the current step began
    int activeLane = noEffect;
    int shuffleOffset = 1;     // steps back, chosen per step

    std::array<Dsp::StateVariableFilter, 2> filters;
    std::array<Dsp::Crusher, 2> crushers;
    float gateEnvelope = 1.0f;

    juce::Random random;
};
