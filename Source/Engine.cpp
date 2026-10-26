#include "Engine.h"

#include <cmath>

namespace
{
    constexpr double historySeconds = 10.0;
    constexpr double echoSeconds = 2.0;
    constexpr double fadeSeconds = 0.002;       // at step and slice edges, against clicks
    constexpr double pitchWindowSeconds = 0.06; // grain size of the pitch shifter

    double fractionalPart (double x) { return x - std::floor (x); }
}

GlitchEngine::GlitchEngine()
{
    for (auto& pattern : patterns)
        for (auto& step : pattern)
            step.store (noEffect);

    for (auto& lane : waveformLane)
        lane.store (noEffect);
}

void GlitchEngine::prepare (double newSampleRate, int numChannels)
{
    sampleRate = newSampleRate;
    history.assign ((size_t) juce::jmax (1, numChannels), std::vector<float> ((size_t) (historySeconds * sampleRate), 0.0f));
    written = 0;
    currentStepNumber = -1;
    activeLane = noEffect;
    gateEnvelope = 1.0f;

    for (auto& buffer : echoBuffers)
        buffer.assign ((size_t) (echoSeconds * sampleRate), 0.0f);

    for (auto& f : filters)
        f.reset();

    for (auto& c : crushers)
        c = {};
}

bool GlitchEngine::isEmpty (int patternIndex) const
{
    for (const auto& step : patterns[(size_t) patternIndex])
        if (step.load() != noEffect)
            return false;

    return true;
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

int GlitchEngine::patternForCycle (juce::int64 cycle, const Timing& timing) const
{
    if (! timing.chain)
        return juce::jlimit (0, numPatterns - 1, timing.pattern);

    // Chain plays the patterns that have something in them, in order.
    int used[numPatterns];
    int count = 0;

    for (int p = 0; p < numPatterns; ++p)
        if (! isEmpty (p))
            used[count++] = p;

    return count == 0 ? juce::jlimit (0, numPatterns - 1, timing.pattern) : used[cycle % count];
}

void GlitchEngine::mutatePattern (int patternIndex, int length)
{
    // Change one or two steps: fill an empty one, or clear or swap a used one.
    const auto changes = 1 + random.nextInt (2);

    for (int i = 0; i < changes; ++i)
    {
        const auto step = random.nextInt (juce::jmax (1, length));
        const auto current = getStep (patternIndex, step);
        const auto newLane = random.nextInt (numLanes);

        if (current == noEffect)
            setStep (patternIndex, step, newLane);
        else
            setStep (patternIndex, step, random.nextBool() ? noEffect : newLane);
    }
}

void GlitchEngine::startStep (juce::int64 stepNumber, double samplesIntoStep, double length, const Timing& timing, const Settings& settings)
{
    currentStepNumber = stepNumber;
    stepLength = length;
    stepStart = written - 1 - (juce::int64) samplesIntoStep;

    const auto patternLength = juce::jlimit (1, numSteps, timing.length);
    const auto cycle = stepNumber / patternLength;
    const auto step = (int) (stepNumber % patternLength);
    const auto patternIndex = patternForCycle (cycle, timing);

    if (step == 0 && timing.mutate > 0.0f && random.nextFloat() < timing.mutate)
        mutatePattern (patternIndex, patternLength);

    const auto lane = getStep (patternIndex, step);
    activeLane = lane != noEffect && random.nextFloat() < settings[(size_t) lane].chance ? lane : noEffect;

    if (activeLane == shuffle)
        shuffleOffset = 1 + random.nextInt (juce::jmax (1, settings[shuffle].shuffleRange));

    if (activeLane == filter)
        for (auto& f : filters)
            f.reset();

    currentStep.store (step);
    currentPattern.store (patternIndex);
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

        case echo:
        {
            // A feedback delay that starts fresh with the step, so the echoes belong to this step only.
            auto& buffer = echoBuffers[(size_t) juce::jmin (channel, 1)];
            const auto size = (juce::int64) buffer.size();
            const auto position = (juce::int64) t;
            const auto delay = juce::jmax ((juce::int64) 1, (juce::int64) (length / juce::jmax (1, s.echoDivision)));
            const auto echoed = position >= delay ? buffer[(size_t) ((position - delay) % size)] : 0.0f;
            const auto wet = input + s.echoFeedback * echoed;
            buffer[(size_t) (position % size)] = wet;
            return wet;
        }

        case pitch:
        {
            // Two-tap delay-line pitch shifter: each tap's delay ramps through a short window, and the
            // taps are offset by half a window and crossfaded so the jumps are never heard.
            const auto speed = std::pow (2.0, s.semitones / 12.0);
            const auto window = pitchWindowSeconds * sampleRate;
            const auto phase = t * (1.0 - speed) / window;
            const auto now = here + t;
            auto out = 0.0f;

            for (const auto offset : { 0.0, 0.5 })
            {
                const auto f = fractionalPart (phase + offset);
                const auto weight = (float) std::pow (std::sin (juce::MathConstants<double>::pi * f), 2.0);
                out += weight * read (channel, now - f * window);
            }

            return out;
        }

        default:
            return input;
    }
}

void GlitchEngine::process (juce::AudioBuffer<float>& buffer, const Timing& timing, const Settings& settings, float mix)
{
    const auto numSamples = buffer.getNumSamples();
    const auto numChannels = juce::jmin (buffer.getNumChannels(), (int) history.size());
    const auto historySize = (juce::int64) history[0].size();

    const auto baseLength = timing.stepBeats * 60.0 / timing.bpm * sampleRate;
    const auto ppqPerSample = timing.bpm / 60.0 / sampleRate;
    const auto fade = fadeSeconds * sampleRate;
    const auto split = 1.0 + juce::jlimit (0.0, 0.5, timing.swing); // where the second step of each pair starts
    const auto patternLength = juce::jlimit (1, numSteps, timing.length);

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

        // Steps come in pairs; swing makes the first of each pair longer and the second shorter.
        const auto position = (timing.ppq + i * ppqPerSample) / timing.stepBeats;
        const auto pair = std::floor (position / 2.0);
        const auto local = position - pair * 2.0;
        const auto second = local >= split;
        const auto stepNumber = (juce::int64) pair * 2 + (second ? 1 : 0);
        const auto length = (second ? 2.0 - split : split) * baseLength;
        const auto fraction = second ? (local - split) / (2.0 - split) : local / split;

        if (stepNumber != currentStepNumber)
            startStep (stepNumber, fraction * length, length, timing, settings);

        stepProgress.store ((float) fraction, std::memory_order_relaxed);
        const auto t = (double) (written - 1 - stepStart);

        if (activeLane != noEffect)
        {
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

        // Waveform display: output peaks across the 16 steps, tagged with the effect playing.
        const auto step = (int) (stepNumber % patternLength);
        const auto bin = juce::jlimit (0, waveformBins - 1, (int) (((double) step + fraction) * (waveformBins / numSteps)));

        if (bin != waveBin)
        {
            waveform[(size_t) waveBin].store (wavePeak, std::memory_order_relaxed);
            waveformLane[(size_t) waveBin].store (activeLane, std::memory_order_relaxed);
            wavePeak = 0.0f;
            waveBin = bin;
        }

        for (int ch = 0; ch < numChannels; ++ch)
            wavePeak = juce::jmax (wavePeak, std::abs (buffer.getSample (ch, i)));
    }

    if (! timing.running)
    {
        currentStep.store (-1);
        firingLane.store (noEffect);
    }
}
