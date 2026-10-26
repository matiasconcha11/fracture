#pragma once

#include <juce_core/juce_core.h>
#include <algorithm>
#include <cmath>
#include <vector>

namespace Dsp
{
    // Topology-preserving state variable filter (Zavalishin / Cytomic), stable under fast cutoff modulation.
    struct StateVariableFilter
    {
        float ic1 = 0.0f, ic2 = 0.0f;

        void reset() { ic1 = ic2 = 0.0f; }

        // type: 0 low-pass, 1 high-pass, 2 band-pass. g = tan(pi * fc / fs), k = 1 / Q.
        float process (float in, float g, float k, int type)
        {
            const auto a1 = 1.0f / (1.0f + g * (g + k));
            const auto a2 = g * a1;
            const auto a3 = g * a2;
            const auto v3 = in - ic2;
            const auto v1 = a1 * ic1 + a2 * v3;
            const auto v2 = ic2 + a2 * ic1 + a3 * v3;
            ic1 = 2.0f * v1 - ic1;
            ic2 = 2.0f * v2 - ic2;

            switch (type)
            {
                case 1: return in - k * v1 - v2;
                case 2: return v1 * k; // unity gain at the centre frequency
                default: return v2;
            }
        }
    };

    // Single-channel delay line read with linear interpolation.
    struct DelayLine
    {
        std::vector<float> buffer;
        int writeIndex = 0;

        void prepare (int maxDelaySamples)
        {
            buffer.assign ((size_t) maxDelaySamples + 2, 0.0f);
            writeIndex = 0;
        }

        int maxDelay() const { return (int) buffer.size() - 2; }

        // Silences the most recent `numSamples`, i.e. everything a read up to that delay could reach.
        void clearRecent (int numSamples)
        {
            const auto size = (int) buffer.size();

            for (int i = 0, index = writeIndex; i < juce::jmin (numSamples, size); ++i)
            {
                index = index == 0 ? size - 1 : index - 1;
                buffer[(size_t) index] = 0.0f;
            }
        }

        void push (float sample)
        {
            if (buffer.empty())
                return;

            buffer[(size_t) writeIndex] = sample;

            if (++writeIndex == (int) buffer.size())
                writeIndex = 0;
        }

        // Delay 0 returns the sample most recently pushed.
        float read (float delaySamples) const
        {
            if (buffer.empty())
                return 0.0f;

            const auto size = (int) buffer.size();
            const auto delay = juce::jlimit (0.0f, (float) maxDelay(), delaySamples);
            const auto whole = (int) delay;
            const auto frac = delay - (float) whole;

            auto index = writeIndex - 1 - whole;
            if (index < 0) index += size;
            auto previous = index - 1;
            if (previous < 0) previous += size;

            return buffer[(size_t) index] + frac * (buffer[(size_t) previous] - buffer[(size_t) index]);
        }
    };

    // Bit depth and sample-rate reduction; amount 0 passes audio through untouched.
    struct Crusher
    {
        float held = 0.0f;
        float counter = 1.0f;

        float process (float in, float amount)
        {
            if (amount <= 1.0e-4f)
            {
                held = in;
                counter = 1.0f;
                return in;
            }

            // Sample-and-hold: up to 1/40 of the sample rate at full amount.
            counter += 1.0f / (1.0f + amount * amount * 39.0f);

            if (counter >= 1.0f)
            {
                counter -= 1.0f;
                const auto bits = 16.0f - amount * 13.0f;
                const auto steps = std::exp2 (bits - 1.0f);
                held = std::round (in * steps) / steps;
            }

            return held;
        }
    };

    // tanh saturation, crossfaded in so amount 0 is exactly dry; roughly loudness-compensated.
    inline float drive (float in, float amount)
    {
        if (amount <= 1.0e-4f)
            return in;

        const auto gain = 1.0f + amount * 30.0f;
        const auto shaped = std::tanh (in * gain) / std::sqrt (gain);
        return in + (shaped - in) * juce::jmin (1.0f, amount * 4.0f);
    }
}
