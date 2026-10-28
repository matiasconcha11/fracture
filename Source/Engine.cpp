#include "Engine.h"

#include <cmath>

namespace
{
    constexpr double historySeconds = 10.0;
    constexpr double echoSeconds = 4.0;
    constexpr double fadeSeconds = 0.002;        // at block and slice edges, against clicks
    constexpr double grainSeconds = 0.06;        // Pitch and Stretch grain size

    double fractionalPart (double x) { return x - std::floor (x); }
}

GlitchEngine::GlitchEngine()
{
    for (auto& lane : waveformLane)
        lane.store (noEffect);
}

void GlitchEngine::applyValue (int lane, float value, LaneSettings& s)
{
    value = juce::jlimit (0.0f, 1.0f, value);
    static constexpr int repeatChoices[] { 2, 3, 4, 6, 8, 16 };
    static constexpr int pulseChoices[] { 2, 3, 4, 6, 8 };

    switch (lane)
    {
        case repeat: s.repeats = repeatChoices[juce::jmin (5, (int) (value * 6.0f))]; break;
        case tapeStop: s.tapeCurve = value; break;
        case stretch: s.stretchSpeed = 0.25f + value * 0.75f; break;
        case gate: s.pulses = pulseChoices[juce::jmin (4, (int) (value * 5.0f))]; break;
        case crush: s.crushAmount = value; break;
        case distort: s.driveAmount = value; break;
        case filter: s.resonance = value; break;
        case ringMod: s.ringFrequency = 30.0f * std::pow (66.0f, value); break;
        case echo: s.echoFeedback = value * 0.9f; break;
        case pitch: s.semitones = std::round (-24.0f + value * 48.0f); break;
        case shuffle: s.shuffleRange = 1 + juce::jmin (7, (int) (value * 8.0f)); break;
        default: break;
    }
}

void GlitchEngine::prepare (double newSampleRate, int numChannels)
{
    sampleRate = newSampleRate;
    history.assign ((size_t) juce::jmax (1, numChannels), std::vector<float> ((size_t) (historySeconds * sampleRate), 0.0f));
    written = 0;
    currentStepNumber = -1;
    activeLane = shownLane = playingLiveLane = noEffect;
    gateEnvelope = 1.0f;
    inputLevel = attackEnvelope = 0.0f;
    attackHold = attackCooldown = 0;
    wavePeak = waveProtectPeak = 0.0f;
    for (auto& marker : waveformProtect)
        marker.store (0.0f, std::memory_order_relaxed);

    for (auto& buffer : echoBuffers)
        buffer.assign ((size_t) (echoSeconds * sampleRate), 0.0f);

    for (auto& f : filters)
        f.reset();

    for (auto& c : crushers)
        c = {};
}

GlitchEngine::Step GlitchEngine::getStep (int patternIndex, int step) const
{
    const auto& s = patterns[(size_t) patternIndex][(size_t) step];
    return { s.lane.load(), s.tie.load(), s.chance.load(), s.value.load() };
}

void GlitchEngine::setStep (int patternIndex, int step, const Step& value)
{
    auto& s = patterns[(size_t) patternIndex][(size_t) step];
    s.lane.store (value.lane);
    s.tie.store (value.lane != noEffect && value.tie && step > 0);
    s.chance.store (value.chance);
    s.value.store (value.value);
}

bool GlitchEngine::isEmpty (int patternIndex) const
{
    for (const auto& step : patterns[(size_t) patternIndex])
        if (step.lane.load() != noEffect)
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
    if (const auto forced = patternOverride.load(); forced >= 0)
        return juce::jmin (forced, numPatterns - 1);

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
    // Change one or two steps: fill an empty one with a short block, or clear or swap a used one.
    const auto changes = 1 + random.nextInt (2);

    for (int i = 0; i < changes; ++i)
    {
        const auto step = random.nextInt (juce::jmax (1, length));
        const auto current = getStep (patternIndex, step);

        if (current.lane == noEffect)
            setStep (patternIndex, step, random.nextInt (numLanes));
        else if (random.nextBool())
            setStep (patternIndex, step, noEffect);
        else
            setStep (patternIndex, step, Step { random.nextInt (numLanes), current.tie });
    }
}

void GlitchEngine::startEvent (int lane, int sourceLane, double samplesIntoEvent, double length, const LaneSettings& settings)
{
    // Random picks one of the real effects each time it plays.
    const auto effect = lane == randomLane ? random.nextInt (randomLane) : lane;

    eventStart = written - 1 - (juce::int64) samplesIntoEvent;
    eventLength = juce::jmax (1.0, length);
    activeLane = effect;
    shownLane = sourceLane;
    eventSettings = settings;

    if (effect == shuffle)
        shuffleOffset = 1 + random.nextInt (juce::jmax (1, eventSettings.shuffleRange));

    if (effect == filter)
        for (auto& f : filters)
            f.reset();
}

void GlitchEngine::onStep (juce::int64 stepNumber, double samplesIntoStep, double length, const Timing& timing, const Settings& settings)
{
    currentStepNumber = stepNumber;
    stepLength = length;

    const auto patternLength = juce::jlimit (1, numSteps, timing.length);
    const auto cycle = stepNumber / patternLength;
    const auto step = (int) (stepNumber % patternLength);
    const auto patternIndex = patternForCycle (cycle, timing);

    if (step == 0 && timing.mutate > 0.0f && random.nextFloat() < timing.mutate)
        mutatePattern (patternIndex, patternLength);

    currentStep.store (step);
    currentPattern.store (patternIndex);

    // A held MIDI note overrides the pattern.
    if (playingLiveLane != noEffect)
        return;

    const auto s = getStep (patternIndex, step);

    // A tied step carries on whatever its block is doing (including not firing).
    if (s.tie && s.lane == shownLane && step > 0)
        return;

    shownLane = s.lane;
    activeLane = noEffect;

    if (s.lane == noEffect)
        return;

    const auto& laneSettings = settings[(size_t) s.lane];
    const auto chance = s.chance >= 0.0f ? s.chance : laneSettings.chance;

    if (random.nextFloat() >= chance)
        return;

    // The block runs over this step and every step tied to it.
    auto steps = 1;

    while (step + steps < patternLength)
    {
        const auto next = getStep (patternIndex, step + steps);

        if (! next.tie || next.lane != s.lane)
            break;

        ++steps;
    }

    auto blockSettings = laneSettings;
    startEvent (s.lane, s.lane, samplesIntoStep, steps * length, blockSettings);

    if (s.value >= 0.0f)
        applyValue (activeLane, s.value, eventSettings);
}

float GlitchEngine::effectSample (int channel, float input, double t)
{
    const auto& s = eventSettings;
    const auto length = eventLength;
    const auto here = (double) eventStart;
    const auto ch = (size_t) juce::jmin (channel, 1);

    switch (activeLane)
    {
        case repeat:
        {
            // Slice the start of the block and play it again; each repeat can drop in pitch.
            const auto slice = length / juce::jmax (1, s.repeats);
            const auto index = std::floor (t / slice);
            const auto local = t - index * slice;
            const auto rate = std::pow (1.0 - s.pitch * 0.25, index);
            const auto fade = fadeSeconds * sampleRate;
            const auto edge = (float) juce::jlimit (0.0, 1.0, juce::jmin (local, slice - local) / fade);
            return read (channel, here + local * rate) * (index > 0 ? edge : 1.0f);
        }

        case reverse:
            // What came just before the block, backwards.
            return read (channel, here - 1.0 - t);

        case tapeStop:
        {
            // Speed falls from 1 to 0 across the block: speed(x) = 1 - x^e, position is its integral.
            const auto e = 0.5 + s.tapeCurve * 4.0;
            const auto x = t / length;
            const auto travelled = length * (x - std::pow (x, e + 1.0) / (e + 1.0));
            return read (channel, here + travelled);
        }

        case stretch:
        {
            // Granular slow-down: grains play at normal speed, but each starts further into the
            // audio by only `speed` of the time that has passed. Two overlapping grains, crossfaded.
            const auto window = grainSeconds * sampleRate;
            auto out = 0.0f;

            for (const auto offset : { 0.0, 0.5 })
            {
                const auto phase = fractionalPart (t / window + offset);
                const auto grainBegin = here + (t - phase * window) * s.stretchSpeed;
                const auto weight = (float) std::pow (std::sin (juce::MathConstants<double>::pi * phase), 2.0);
                out += weight * read (channel, grainBegin + phase * window);
            }

            return out;
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
            return crushers[ch].process (input, s.crushAmount);

        case distort:
            return Dsp::drive (input, s.driveAmount);

        case filter:
        {
            // Sweep between 20 kHz and 100 Hz across the block.
            const auto x = (float) (t / length);
            const auto position = s.filterOpens ? x : 1.0f - x;
            const auto cutoff = juce::jmin ((float) (sampleRate * 0.45), 100.0f * std::pow (200.0f, position));
            const auto g = std::tan (juce::MathConstants<float>::pi * cutoff / (float) sampleRate);
            const auto k = 1.0f / (0.5f + s.resonance * s.resonance * 11.5f);
            return filters[ch].process (input, g, k, 0);
        }

        case ringMod:
            return input * (float) std::sin (juce::MathConstants<double>::twoPi * s.ringFrequency * t / sampleRate);

        case echo:
        {
            // A feedback delay that starts fresh with the block, so the echoes belong to it alone.
            auto& buffer = echoBuffers[ch];
            const auto size = (juce::int64) buffer.size();
            const auto position = (juce::int64) t;
            const auto delay = juce::jmax ((juce::int64) 1, (juce::int64) (stepLength / juce::jmax (1, s.echoDivision)));
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
            const auto window = grainSeconds * sampleRate;
            const auto phase = t * (1.0 - speed) / window;
            auto out = 0.0f;

            for (const auto offset : { 0.0, 0.5 })
            {
                const auto f = fractionalPart (phase + offset);
                const auto weight = (float) std::pow (std::sin (juce::MathConstants<double>::pi * f), 2.0);
                out += weight * read (channel, here + t - f * window);
            }

            return out;
        }

        case shuffle:
            // An earlier step plays in place of this block.
            return read (channel, here - (double) shuffleOffset * stepLength + t);

        default:
            return input;
    }
}

void GlitchEngine::process (juce::AudioBuffer<float>& buffer, const Timing& timing, const Settings& settings, float mix, float protect)
{
    const auto numSamples = buffer.getNumSamples();
    const auto numChannels = juce::jmin (buffer.getNumChannels(), (int) history.size());
    const auto historySize = (juce::int64) history[0].size();

    const auto baseLength = timing.stepBeats * 60.0 / timing.bpm * sampleRate;
    const auto ppqPerSample = timing.bpm / 60.0 / sampleRate;
    const auto fade = fadeSeconds * sampleRate;
    const auto split = 1.0 + juce::jlimit (0.0, 0.5, timing.swing); // where the second step of each pair starts
    const auto patternLength = juce::jlimit (1, numSteps, timing.length);
    const auto levelRate = (float) (1.0 - std::exp (-1.0 / (0.035 * sampleRate)));
    const auto release = (float) std::exp (-1.0 / (0.024 * sampleRate));
    protect = juce::jlimit (0.0f, 1.0f, protect);
    const auto detectAttacks = protect > 0.0f;
    if (! detectAttacks)
    {
        inputLevel = attackEnvelope = 0.0f;
        attackHold = attackCooldown = 0;
    }

    // A live (MIDI) effect lasts a beat and loops for as long as the note is held.
    if (const auto live = liveLane.load(); live != playingLiveLane)
    {
        playingLiveLane = live;

        if (live != noEffect)
        {
            if (stepLength <= 0.0)
                stepLength = baseLength;

            startEvent (live, live, 0.0, 60.0 / timing.bpm * sampleRate, settings[(size_t) live]);
        }
        else
        {
            activeLane = shownLane = noEffect;
            currentStepNumber = -1; // pick the pattern back up at the next step
        }
    }

    for (int i = 0; i < numSamples; ++i)
    {
        if (detectAttacks)
        {
            float inputPeak = 0.0f;
            for (int ch = 0; ch < numChannels; ++ch)
                inputPeak = juce::jmax (inputPeak, std::abs (buffer.getSample (ch, i)));

            if (attackCooldown > 0)
                --attackCooldown;
            if (attackCooldown == 0 && inputPeak > juce::jmax (0.002f, inputLevel * 2.5f))
            {
                attackEnvelope = 1.0f;
                attackHold = (int) (0.012 * sampleRate);
                attackCooldown = (int) (0.055 * sampleRate);
            }
            inputLevel += levelRate * (inputPeak - inputLevel);
            if (attackHold > 0)
                --attackHold;
            else
                attackEnvelope *= release;
        }

        // Record first: effects may read the sample that just arrived.
        for (int ch = 0; ch < numChannels; ++ch)
            history[(size_t) ch][(size_t) (written % historySize)] = buffer.getSample (ch, i);

        ++written;

        auto step = -1;
        auto fraction = 0.0;

        if (timing.running)
        {
            // Steps come in pairs; swing makes the first of each pair longer and the second shorter.
            const auto position = (timing.ppq + i * ppqPerSample) / timing.stepBeats;
            const auto pair = std::floor (position / 2.0);
            const auto local = position - pair * 2.0;
            const auto second = local >= split;
            const auto stepNumber = (juce::int64) pair * 2 + (second ? 1 : 0);
            const auto length = (second ? 2.0 - split : split) * baseLength;
            fraction = second ? (local - split) / (2.0 - split) : local / split;

            if (stepNumber != currentStepNumber)
                onStep (stepNumber, fraction * length, length, timing, settings);

            step = (int) (stepNumber % patternLength);
            stepProgress.store ((float) fraction, std::memory_order_relaxed);
        }
        else if (playingLiveLane == noEffect)
        {
            currentStepNumber = -1;
            continue;
        }

        if (activeLane != noEffect)
        {
            auto t = (double) (written - 1 - eventStart);

            if (playingLiveLane != noEffect)
                t = std::fmod (t, eventLength);

            blockProgress.store ((float) (t / eventLength), std::memory_order_relaxed);

            // Fade the effect in and out at the block edges so switching never clicks.
            const auto edge = (float) juce::jlimit (0.0, 1.0, juce::jmin (t + 1.0, eventLength - t) / fade);
            const auto amount = edge * mix * (1.0f - protect * attackEnvelope);

            for (int ch = 0; ch < numChannels; ++ch)
            {
                const auto dry = buffer.getSample (ch, i);
                const auto wet = effectSample (ch, dry, t);
                buffer.setSample (ch, i, dry + (wet - dry) * amount);
            }
        }

        // Waveform display: output peaks across the 16 steps, tagged with the lane playing.
        if (step >= 0)
        {
            const auto bin = juce::jlimit (0, waveformBins - 1, (int) (((double) step + fraction) * (waveformBins / numSteps)));

            if (bin != waveBin)
            {
                waveform[(size_t) waveBin].store (wavePeak, std::memory_order_relaxed);
                waveformLane[(size_t) waveBin].store (activeLane == noEffect ? noEffect : shownLane, std::memory_order_relaxed);
                waveformProtect[(size_t) waveBin].store (waveProtectPeak, std::memory_order_relaxed);
                wavePeak = 0.0f;
                waveProtectPeak = 0.0f;
                waveBin = bin;
            }

            if (activeLane != noEffect)
                waveProtectPeak = juce::jmax (waveProtectPeak, protect * attackEnvelope);

            for (int ch = 0; ch < numChannels; ++ch)
                wavePeak = juce::jmax (wavePeak, std::abs (buffer.getSample (ch, i)));
        }
    }

    firingLane.store (activeLane == noEffect ? noEffect : shownLane);

    if (! timing.running)
        currentStep.store (-1);
}
