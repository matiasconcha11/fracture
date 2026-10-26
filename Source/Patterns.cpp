#include "Patterns.h"

namespace Patterns
{
    namespace
    {
        // One letter per lane, in GlitchEngine::Lane order; '.' is an empty step.
        constexpr const char* laneLetters = "RVTGCFS";

        struct Factory
        {
            const char* name;
            const char* steps;
        };

        constexpr Factory factory[] {
            { "Empty", "................" },
            { "Stutter Fill", "............RRRR" },
            { "Stutter Builds", "...R.......R.RRR" },
            { "Reverse Swells", "...V.......V...V" },
            { "Tape Stop Ending", "..............TT" },
            { "Half-Bar Stops", "......TT......TT" },
            { "Gated Groove", "G..G..G.G..G..GG" },
            { "Breakbeat Shuffle", "..S...S.S...S.SS" },
            { "Lo-Fi Bars", "CC..CC..CC..CC.." },
            { "Filter Drops", "...F...F...F.FFF" },
            { "Glitch Mess", "R.VS.CG.RT.F.S.R" },
            { "Chaos", "RVSGRCSVTRGFSRVT" },
        };
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
        Pattern pattern;
        const juce::String letters (laneLetters);

        for (int i = 0; i < GlitchEngine::numSteps; ++i)
            pattern[(size_t) i] = letters.indexOfChar (text[i]); // -1 (noEffect) for '.' or anything unknown

        return pattern;
    }

    juce::String toString (const Pattern& pattern)
    {
        juce::String text;

        for (auto lane : pattern)
            text << (juce::isPositiveAndBelow (lane, (int) GlitchEngine::numLanes) ? laneLetters[lane] : '.');

        return text;
    }

    Pattern randomize (float density, juce::Random& random)
    {
        Pattern pattern;
        pattern.fill (GlitchEngine::noEffect);

        for (int i = 0; i < GlitchEngine::numSteps; ++i)
        {
            if (random.nextFloat() >= density)
                continue;

            // Effects tend to come in runs, which sounds more musical than isolated hits.
            const auto continueRun = i > 0 && pattern[(size_t) i - 1] != GlitchEngine::noEffect && random.nextFloat() < 0.5f;
            pattern[(size_t) i] = continueRun ? pattern[(size_t) i - 1] : random.nextInt (GlitchEngine::numLanes);
        }

        return pattern;
    }
}
