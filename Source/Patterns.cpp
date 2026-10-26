#include "Patterns.h"

namespace Patterns
{
    namespace
    {
        // One letter per lane, in GlitchEngine::Lane order.
        constexpr const char* laneLetters = "RVTXGCDFMEPS?";

        struct Factory
        {
            const char* name;
            const char* steps;
        };

        constexpr Factory factory[] {
            { "Empty", "................" },
            { "Stutter Fill", "............R+++" },
            { "Stutter Builds", "...R.......R+R++" },
            { "Snare Rolls", "....R+......R+++" },
            { "Reverse Swells", "..V+......V+..V+" },
            { "Reverse Tails", ".V..V..V.V..V..V" },
            { "Tape Stop Ending", "............T+++" },
            { "Half-Bar Stops", "......T+......T+" },
            { "Slow Motion", "X+++....X+++...." },
            { "Gated Groove", "G..G..G.G..G..G+" },
            { "Offbeat Gates", "..G+..G+..G+..G+" },
            { "Lo-Fi Bars", "C+..C+..C+..C+.." },
            { "Dirty Beats", "D...D...D...D+++" },
            { "Filter Drops", "...F...F...F.F++" },
            { "Robot Voice", "M+M+..M+M+..M+++" },
            { "Dub Throws", "...E.......E..E+" },
            { "Octave Drops", "....P+......P+.." },
            { "Breakbeat Shuffle", "..S...S.S...S.S+" },
            { "Dice Rolls", "?.?.?.?.?.?.?.?." },
            { "Constant Chaos", "????????????????" },
            { "Glitch Mess", "R.VS.CG.RT.F.S.R" },
            { "Full Bar Mix", "R+V+G+T+X+C+F+E+" },
        };

        int laneForLetter (juce::juce_wchar c) { return juce::String (laneLetters).indexOfChar (c); }
    }

    juce::StringArray names()
    {
        juce::StringArray result;

        for (const auto& f : factory)
            result.add (f.name);

        return result;
    }

    Pattern get (int index)
    {
        return fromString (factory[juce::jlimit (0, (int) std::size (factory) - 1, index)].steps);
    }

    Pattern fromString (const juce::String& text)
    {
        Pattern pattern {};

        for (int i = 0; i < GlitchEngine::numSteps; ++i)
        {
            // Short strings are empty past their end (reading beyond a juce::String is undefined).
            const auto c = i < text.length() ? text[i] : (juce::juce_wchar) '.';

            if (c == '+' && i > 0 && pattern[(size_t) i - 1].lane != GlitchEngine::noEffect)
                pattern[(size_t) i] = { pattern[(size_t) i - 1].lane, true };
            else
                pattern[(size_t) i] = { laneForLetter (c) }; // -1 (noEffect) for '.' or anything unknown
        }

        return pattern;
    }

    juce::String toString (const Pattern& pattern)
    {
        juce::String text;

        for (const auto& step : pattern)
        {
            if (! juce::isPositiveAndBelow (step.lane, (int) GlitchEngine::numLanes))
                text << '.';
            else if (step.tie)
                text << '+';
            else
                text << laneLetters[step.lane];
        }

        return text;
    }

    juce::String overridesToString (const Pattern& pattern)
    {
        // "step:chance:value" for each step that has an override, separated by spaces.
        juce::StringArray parts;

        for (int i = 0; i < GlitchEngine::numSteps; ++i)
        {
            const auto& s = pattern[(size_t) i];

            if (s.chance >= 0.0f || s.value >= 0.0f)
                parts.add (juce::String (i) + ":" + juce::String (s.chance, 3) + ":" + juce::String (s.value, 3));
        }

        return parts.joinIntoString (" ");
    }

    void overridesFromString (const juce::String& text, Pattern& pattern)
    {
        for (const auto& part : juce::StringArray::fromTokens (text, " ", ""))
        {
            const auto fields = juce::StringArray::fromTokens (part, ":", "");

            if (fields.size() != 3)
                continue;

            const auto step = fields[0].getIntValue();

            if (juce::isPositiveAndBelow (step, GlitchEngine::numSteps))
            {
                pattern[(size_t) step].chance = fields[1].getFloatValue();
                pattern[(size_t) step].value = fields[2].getFloatValue();
            }
        }
    }

    bool same (const Pattern& a, const Pattern& b)
    {
        for (int i = 0; i < GlitchEngine::numSteps; ++i)
        {
            const auto& x = a[(size_t) i];
            const auto& y = b[(size_t) i];

            if (x.lane != y.lane || x.tie != y.tie || x.chance != y.chance || x.value != y.value)
                return false;
        }

        return true;
    }

    Pattern randomize (float density, juce::Random& random)
    {
        Pattern pattern {};

        // Block lengths in steps, weighted like the presets we studied: mostly 1 or 2, sometimes a beat.
        constexpr int lengths[] { 1, 1, 1, 2, 2, 2, 3, 4, 4 };

        for (int i = 0; i < GlitchEngine::numSteps;)
        {
            // More blocks on beats and offbeats, and more in the last beat (fills).
            auto likelihood = density * (i % 4 == 0 ? 1.3f : (i % 2 == 0 ? 1.0f : 0.7f));

            if (i >= 12)
                likelihood *= 1.3f;

            if (random.nextFloat() >= likelihood)
            {
                ++i;
                continue;
            }

            const auto length = juce::jmin (lengths[random.nextInt ((int) std::size (lengths))], GlitchEngine::numSteps - i);

            // Random gets picked less often than the real effects.
            const auto lane = random.nextFloat() < 0.05f ? (int) GlitchEngine::randomLane : random.nextInt (GlitchEngine::randomLane);

            for (int k = 0; k < length; ++k)
                pattern[(size_t) (i + k)] = { lane, k > 0 };

            i += length;
        }

        return pattern;
    }
}
