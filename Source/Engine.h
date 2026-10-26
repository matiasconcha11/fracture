#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include "Dsp.h"

#include <array>
#include <atomic>

// The glitch engine: four 16-step patterns where each step can hold one effect. Audio is recorded
// continuously, so effects can replay, reverse or slow down what just happened.
class GlitchEngine
{
public:
    static constexpr int numSteps = 16;
    static constexpr int numPatterns = 4;
    static constexpr int waveformBins = numSteps * 16;

    enum Lane
    {
        repeat,
        reverse,
        tapeStop,
        gate,
        crush,
        filter,
        shuffle,
        echo,
        pitch,
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
        int echoDivision = 4;      // Echo: delay time as a fraction of the step
        float echoFeedback = 0.6f; // Echo
        float semitones = -12.0f;  // Pitch
    };

    using Settings = std::array<LaneSettings, numLanes>;

    struct Timing
    {
        bool running = false;    // false: pass audio through untouched
        double ppq = 0.0;        // position of the first sample, in quarter notes
        double bpm = 120.0;
        double stepBeats = 0.25; // length of one step in quarter notes
        double swing = 0.0;      // 0..0.5: how much later every second step starts
        int length = numSteps;   // steps before the pattern loops
        int pattern = 0;         // which pattern plays (when not chaining)
        bool chain = false;      // play the non-empty patterns one after another
        float mutate = 0.0f;     // chance per loop that a couple of steps change by themselves
    };

    GlitchEngine();

    // Clears the recording, not the patterns.
    void prepare (double sampleRate, int numChannels);

    // Patterns, shared with the editor: one lane per step or noEffect. Any thread.
    void setStep (int patternIndex, int step, int lane) { patterns[(size_t) patternIndex][(size_t) step].store (lane); }
    int getStep (int patternIndex, int step) const { return patterns[(size_t) patternIndex][(size_t) step].load(); }
    bool isEmpty (int patternIndex) const;

    void process (juce::AudioBuffer<float>& buffer, const Timing&, const Settings&, float mix);

    void setRandomSeed (juce::int64 seed) { random.setSeed (seed); }

    // For the editor.
    std::atomic<int> currentStep { -1 };       // step within the pattern, -1 when stopped
    std::atomic<float> stepProgress { 0.0f };  // 0..1 through the current step
    std::atomic<int> currentPattern { 0 };
    std::atomic<int> firingLane { noEffect };
    std::array<std::atomic<float>, waveformBins> waveform {}; // output peak across the 16 steps
    std::array<std::atomic<int>, waveformBins> waveformLane {};

private:
    void startStep (juce::int64 stepNumber, double samplesIntoStep, double length, const Timing&, const Settings&);
    void mutatePattern (int patternIndex, int length);
    int patternForCycle (juce::int64 cycle, const Timing&) const;
    float read (int channel, double position) const;
    float effectSample (int channel, float input, double t, const LaneSettings&);

    double sampleRate = 44100.0;
    double stepLength = 0.0; // samples, for the current step (swing makes steps differ)

    // Recording of everything that came in, addressed by absolute sample number.
    std::vector<std::vector<float>> history;
    juce::int64 written = 0;

    std::array<std::array<std::atomic<int>, numSteps>, numPatterns> patterns;

    juce::int64 currentStepNumber = -1;
    juce::int64 stepStart = 0; // absolute sample number where the current step began
    int activeLane = noEffect;
    int shuffleOffset = 1;     // steps back, chosen per step

    std::array<Dsp::StateVariableFilter, 2> filters;
    std::array<Dsp::Crusher, 2> crushers;
    std::array<std::vector<float>, 2> echoBuffers; // indexed by position within the step
    float gateEnvelope = 1.0f;

    int waveBin = 0;
    float wavePeak = 0.0f;

    juce::Random random;
};
