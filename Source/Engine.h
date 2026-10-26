#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include "Dsp.h"

#include <array>
#include <atomic>

// The glitch engine: four 16-step patterns of effect blocks. A block starts on a step and can be
// tied over the following steps, so one effect can last several steps. Audio is recorded
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
        stretch,
        gate,
        crush,
        distort,
        filter,
        ringMod,
        echo,
        pitch,
        shuffle,
        randomLane, // plays a randomly chosen effect each time
        numLanes
    };

    static constexpr int noEffect = -1;

    struct LaneSettings
    {
        float chance = 1.0f;          // probability a block on this lane fires
        int repeats = 4;              // Repeat: slices per block
        float pitch = 0.0f;           // Repeat: how much each repeat drops in pitch
        float tapeCurve = 0.5f;       // Tape Stop: 0 slows evenly, 1 holds speed then stops late
        float stretchSpeed = 0.5f;    // Stretch: playback speed, 0.25..1
        int pulses = 4;               // Gate: pulses per block
        float duty = 0.5f;            // Gate: open part of each pulse
        float crushAmount = 0.6f;     // Crush
        float driveAmount = 0.6f;     // Distort
        bool filterOpens = false;     // Filter: sweep up instead of down
        float resonance = 0.4f;       // Filter
        float ringFrequency = 220.0f; // Ring Mod, Hz
        int echoDivision = 4;         // Echo: delay time as a fraction of the step
        float echoFeedback = 0.6f;    // Echo
        float semitones = -12.0f;     // Pitch
        int shuffleRange = 4;         // Shuffle: how many steps back it may reach
    };

    using Settings = std::array<LaneSettings, numLanes>;

    // Overrides a lane's main setting for one block; value is 0..1 across that setting's range.
    static void applyValue (int lane, float value, LaneSettings&);

    // One step of a pattern. A tied step continues the block that started before it.
    struct Step
    {
        int lane = noEffect;
        bool tie = false;
        float chance = -1.0f; // -1: use the lane's chance
        float value = -1.0f;  // -1: use the lane's settings
    };

    struct Timing
    {
        bool running = false;    // false: only live (MIDI) effects play
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

    // Patterns, shared with the editor. Any thread.
    Step getStep (int patternIndex, int step) const;
    void setStep (int patternIndex, int step, const Step&);
    void setStep (int patternIndex, int step, int lane) { setStep (patternIndex, step, Step { lane }); }
    bool isEmpty (int patternIndex) const;

    // MIDI: switch patterns (latched until another switch), and hold an effect live.
    void setPatternOverride (int patternIndex) { patternOverride.store (patternIndex); }
    int getPatternOverride() const { return patternOverride.load(); }
    void setLiveLane (int lane) { liveLane.store (lane); }

    void process (juce::AudioBuffer<float>& buffer, const Timing&, const Settings&, float mix);

    void setRandomSeed (juce::int64 seed) { random.setSeed (seed); }

    // For the editor.
    std::atomic<int> currentStep { -1 };      // step within the pattern, -1 when stopped
    std::atomic<float> stepProgress { 0.0f }; // 0..1 through the current step
    std::atomic<float> blockProgress { 0.0f };// 0..1 through the current block
    std::atomic<int> currentPattern { 0 };
    std::atomic<int> firingLane { noEffect }; // the lane whose block is playing (Random shows itself)
    std::array<std::atomic<float>, waveformBins> waveform {}; // output peak across the 16 steps
    std::array<std::atomic<int>, waveformBins> waveformLane {};

private:
    struct StepAtoms
    {
        std::atomic<int> lane { noEffect };
        std::atomic<bool> tie { false };
        std::atomic<float> chance { -1.0f };
        std::atomic<float> value { -1.0f };
    };

    void onStep (juce::int64 stepNumber, double samplesIntoStep, double length, const Timing&, const Settings&);
    void startEvent (int lane, int sourceLane, double samplesIntoEvent, double eventLength, const LaneSettings&);
    void mutatePattern (int patternIndex, int length);
    int patternForCycle (juce::int64 cycle, const Timing&) const;
    float read (int channel, double position) const;
    float effectSample (int channel, float input, double t);

    double sampleRate = 44100.0;

    // Recording of everything that came in, addressed by absolute sample number.
    std::vector<std::vector<float>> history;
    juce::int64 written = 0;

    std::array<std::array<StepAtoms, numSteps>, numPatterns> patterns;
    std::atomic<int> patternOverride { -1 };
    std::atomic<int> liveLane { noEffect };

    // The event (block or live note) that is playing.
    juce::int64 currentStepNumber = -1;
    juce::int64 eventStart = 0;  // absolute sample number where it began
    double eventLength = 0.0;    // samples
    double stepLength = 0.0;     // samples, for Echo and Shuffle timing
    int activeLane = noEffect;   // the effect actually playing
    int shownLane = noEffect;    // the lane it came from (Random stays Random in the display)
    int playingLiveLane = noEffect;
    LaneSettings eventSettings;
    int shuffleOffset = 1;

    std::array<Dsp::StateVariableFilter, 2> filters;
    std::array<Dsp::Crusher, 2> crushers;
    std::array<std::vector<float>, 2> echoBuffers; // indexed by position within the event
    float gateEnvelope = 1.0f;

    int waveBin = 0;
    float wavePeak = 0.0f;

    juce::Random random;
};
