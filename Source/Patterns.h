#pragma once

#include "Engine.h"

namespace Patterns
{
    using Pattern = std::array<int, GlitchEngine::numSteps>;

    // Factory patterns to start from.
    juce::StringArray names();
    Pattern get (int index);

    // A new random pattern; density is the share of steps that get an effect.
    Pattern randomize (float density, juce::Random&);

    // "R..V" style text, one character per step, used for the factory list and saved state.
    Pattern fromString (const juce::String&);
    juce::String toString (const Pattern&);
}
