#include "Engine.h"

#include <cmath>

namespace
{
    constexpr double historySeconds = 10.0;
    constexpr double fadeSeconds = 0.002; // at step and slice edges, against clicks
}

GlitchEngine::GlitchEngine()
{
    for (auto& step : pattern)
        step.store (noEffect);
}

void GlitchEngine::prepare (double newSampleRate, int numChannels)
{
    sampleRate = newSampleRate;
    history.assign ((size_t) juce::jmax (1, numChannels), std::vector<float> ((size_t) (historySeconds * sampleRate), 0.0f));
    written = 0;
    currentStepNumber = -1;
    activeLane = noEffect;
    gateEnvelope = 1.0f;

    for (auto& f : filters)
        f.reset();

    for (auto& c : crushers)
        c = {};
}

float GlitchEngine::read (int channel, double position) const
{
    // Only what has been recorded and is still in the buffer can be read.
    const auto& buffer = history[(size_t) juce::jmin (channel, (int) history.size() - 1)];
    const auto size = (juce::int64) buffer.size();
    position = juce::jlimit ((double) juce::jmax ((juce::int64) 0, written - size + 2), (double) (written - 1), position);

    const auto whole = (juce::int64) std::floor (position);
    const auto frac = (float) (position - (double) whole);
    const auto a = buffer[(size_t) (whole % size)];
    const auto b = buffer[(size_t) (juce::jmin (whole + 1, written - 1) % size)];
    return a + (b - a) * frac;
}

void GlitchEngine::startStep (juce::int64 stepNumber, double samplesIntoStep, const Settings& settings)
{
    currentStepNumber = stepNumber;
    stepStart = written - 1 - (juce::int64) samplesIntoStep;

    const auto step = (int) (stepNumber % numSteps);
    const auto lane = pattern[(size_t) step].load();
    activeLane = lane != noEffect && random.nextFloat() < settings[(size_t) lane].chance ? lane : noEffect;

    if (activeLane == shuffle)
        shuffleOffset = 1 + random.nextInt (juce::jmax (1, settings[shuffle].shuffleRange));

    if (activeLane == filter)
        for (auto& f : filters)
            f.reset();

    currentStep.store (step);
    firingLane.store (activeLane);
}

float GlitchEngine::effectSample (int channel, float input, double t, const LaneSettings& s)
{
    const auto length = stepLength;
    const auto here = (double) stepStart;

    switch (activeLane)
    {
        case repeat:
        {
            // Slice the start of the step and play it again; each repeat can drop in pitch.
            const auto slice = length / juce::jmax (1, s.repeats);
            const auto index = std::floor (t / slice);
            const auto local = t - index * slice;
            const auto rate = std::pow (1.0 - s.pitch * 0.25, index);
            const auto fade = fadeSeconds * sampleRate;
            const auto edge = (float) juce::jlimit (0.0, 1.0, juce::jmin (local, slice - local) / fade);
            return read (channel, here + local * rate) * (index > 0 ? edge : 1.0f);
        }

        case reverse:
            // The previous step, backwards.
            return read (channel, here - 1.0 - t);

        case tapeStop:
        {
            // Speed falls from 1 to 0 across the step: speed(x) = 1 - x^e, position is its integral.
            const auto e = 0.5 + s.tapeCurve * 4.0;
            const auto x = t / length;
            const auto travelled = length * (x - std::pow (x, e + 1.0) / (e + 1.0));
            return read (channel, here + travelled);
        }

        case gate:
        {
            const auto pulse = std::fmod (t / length * s.pulses, 1.0);
            const auto open = pulse < s.duty ? 1.0f : 0.0f;

            if (channel == 0)
                gateEnvelope += (open - gateEnvelope) * (float) (1.0 - std::exp (-1.0 / (0.001 * sampleRate)));

            return input * gateEnvelope;
        }

        case crush:
            return crushers[(size_t) juce::jmin (channel, 1)].process (input, s.crushAmount);

        case filter:
        {
            // Sweep between 20 kHz and 100 Hz across the step.
            const auto x = (float) (t / length);
            const auto position = s.filterOpens ? x : 1.0f - x;
            const auto cutoff = juce::jmin ((float) (sampleRate * 0.45), 100.0f * std::pow (200.0f, position));
            const auto g = std::tan (juce::MathConstants<float>::pi * cutoff / (float) sampleRate);
            const auto k = 1.0f / (0.5f + s.resonance * s.resonance * 11.5f);
            return filters[(size_t) juce::jmin (channel, 1)].process (input, g, k, 0);
        }

        case shuffle:
            // An earlier step plays in place of this one.
            return read (channel, here - (double) shuffleOffset * length + t);

        default:
            return input;
    }
}

void GlitchEngine::process (juce::AudioBuffer<float>& buffer, const Timing& timing, const Settings& settings, float mix)
{
    const auto numSamples = buffer.getNumSamples();
    const auto numChannels = juce::jmin (buffer.getNumChannels(), (int) history.size());
    const auto historySize = (juce::int64) history[0].size();

    stepLength = timing.stepBeats * 60.0 / timing.bpm * sampleRate;
    const auto ppqPerSample = timing.bpm / 60.0 / sampleRate;
    const auto fade = fadeSeconds * sampleRate;

    for (int i = 0; i < numSamples; ++i)
    {
        // Record first: effects may read the sample that just arrived.
        for (int ch = 0; ch < numChannels; ++ch)
            history[(size_t) ch][(size_t) (written % historySize)] = buffer.getSample (ch, i);

        ++written;

        if (! timing.running)
        {
            currentStepNumber = -1;
            continue;
        }

        const auto stepPosition = (timing.ppq + i * ppqPerSample) / timing.stepBeats;
        const auto stepNumber = (juce::int64) std::floor (stepPosition);

        if (stepNumber != currentStepNumber)
            startStep (stepNumber, (stepPosition - (double) stepNumber) * stepLength, settings);

        if (activeLane == noEffect)
            continue;

        const auto t = (double) (written - 1 - stepStart);

        // Fade the effect in and out at the step edges so switching never clicks.
        const auto edge = (float) juce::jlimit (0.0, 1.0, juce::jmin (t + 1.0, stepLength - t) / fade);
        const auto amount = edge * mix;

        for (int ch = 0; ch < numChannels; ++ch)
        {
            const auto dry = buffer.getSample (ch, i);
            const auto wet = effectSample (ch, dry, t, settings[(size_t) activeLane]);
            buffer.setSample (ch, i, dry + (wet - dry) * amount);
        }
    }

    if (! timing.running)
    {
        currentStep.store (-1);
        firingLane.store (noEffect);
    }
}
