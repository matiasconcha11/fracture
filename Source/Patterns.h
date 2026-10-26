#pragma once

#include "Engine.h"

namespace Patterns
{
    using Pattern = std::array<GlitchEngine::Step, GlitchEngine::numSteps>;

    // Pattern-only starting points, for the Patterns menu.
    juce::StringArray names();
    Pattern get (int index);

    // A new random pattern. Density is roughly the share of steps covered; blocks are mostly one or
    // two steps, start on beats and offbeats more often than not, and the last beat gets more fills.
    Pattern randomize (float density, juce::Random&);

    // Text form: one character per step. A letter starts a block on that lane ("RVTXGCDFMEPS?"),
    // '+' continues the block before it, '.' is empty. Per-step chance and value overrides are
    // stored separately so the text stays readable.
    Pattern fromString (const juce::String&);
    juce::String toString (const Pattern&);
    juce::String overridesToString (const Pattern&);
    void overridesFromString (const juce::String&, Pattern&);

    bool same (const Pattern&, const Pattern&);
}
