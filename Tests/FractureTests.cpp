#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "PluginEditor.h"

using Catch::Matchers::WithinAbs;

namespace
{
    constexpr double sampleRate = 48000.0;
    constexpr int stepSamples = 6000; // a 1/16 step at 120 bpm
    constexpr int blockSize = 500;

    // Each sample's value encodes its own position, so a test can tell which past sample was played.
    float ramp (juce::int64 n) { return (float) n * 1.0e-5f; }

    struct EngineFixture
    {
        EngineFixture()
        {
            engine.prepare (sampleRate, 2);
            engine.setRandomSeed (1);
            timing.running = true;
            timing.bpm = 120.0;
            timing.stepBeats = 0.25;
        }

        // Runs `numSamples` of the ramp from song start, returning the left channel.
        std::vector<float> run (int numSamples, float mix = 1.0f)
        {
            std::vector<float> out;
            juce::AudioBuffer<float> buffer (2, blockSize);

            for (int start = 0; start < numSamples; start += blockSize)
            {
                for (int i = 0; i < blockSize; ++i)
                    for (int ch = 0; ch < 2; ++ch)
                        buffer.setSample (ch, i, ramp (start + i));

                timing.ppq = start * (timing.bpm / 60.0 / sampleRate);
                engine.process (buffer, timing, settings, mix);
                out.insert (out.end(), buffer.getReadPointer (0), buffer.getReadPointer (0) + blockSize);
            }

            return out;
        }

        GlitchEngine engine;
        GlitchEngine::Timing timing;
        GlitchEngine::Settings settings;
    };
}

TEST_CASE_METHOD (EngineFixture, "An empty pattern is exactly dry", "[engine]")
{
    const auto out = run (stepSamples * 4);

    for (int n = 0; n < (int) out.size(); ++n)
        REQUIRE (out[(size_t) n] == ramp (n));
}

TEST_CASE_METHOD (EngineFixture, "Nothing happens while the transport is stopped", "[engine]")
{
    engine.setStep (0, GlitchEngine::reverse);
    timing.running = false;
    const auto out = run (stepSamples);

    for (int n = 0; n < (int) out.size(); ++n)
        REQUIRE (out[(size_t) n] == ramp (n));
}

TEST_CASE_METHOD (EngineFixture, "Repeat plays the start of the step again", "[engine]")
{
    engine.setStep (1, GlitchEngine::repeat); // step 1 starts at sample 6000
    settings[GlitchEngine::repeat].repeats = 4;  // slices of 1500 samples
    const auto out = run (stepSamples * 2);

    // Inside the second slice, we hear the first slice again.
    for (int k : { 200, 700, 1200 })
        CHECK_THAT (out[(size_t) (stepSamples + 1500 + k)], WithinAbs (ramp (stepSamples + k), 1.0e-6));
}

TEST_CASE_METHOD (EngineFixture, "Reverse plays the previous step backwards", "[engine]")
{
    engine.setStep (1, GlitchEngine::reverse);
    const auto out = run (stepSamples * 2);

    for (int t : { 500, 3000, 5500 })
        CHECK_THAT (out[(size_t) (stepSamples + t)], WithinAbs (ramp (stepSamples - 1 - t), 1.0e-6));
}

TEST_CASE_METHOD (EngineFixture, "Tape stop starts at normal speed and falls behind", "[engine]")
{
    engine.setStep (1, GlitchEngine::tapeStop);
    const auto out = run (stepSamples * 2);

    const auto lagAt = [&] (int t) { return ramp (stepSamples + t) - out[(size_t) (stepSamples + t)]; };

    CHECK (lagAt (200) < ramp (5));          // barely behind near the start
    CHECK (lagAt (3000) > lagAt (1000));     // falls further behind...
    CHECK (lagAt (5800) > ramp (1500));      // ...and is far behind by the end
}

TEST_CASE_METHOD (EngineFixture, "Gate silences the closed part of each pulse", "[engine]")
{
    engine.setStep (0, GlitchEngine::gate);
    settings[GlitchEngine::gate].pulses = 4;  // pulses of 1500 samples
    settings[GlitchEngine::gate].duty = 0.5f; // open for the first 750
    const auto out = run (stepSamples);

    CHECK_THAT (out[400], WithinAbs (ramp (400), 1.0e-6));
    CHECK_THAT (out[1200], WithinAbs (0.0f, 1.0e-6));
    CHECK_THAT (out[1500 + 400], WithinAbs (ramp (1900), 2.0e-5)); // the 1 ms anti-click fade is still settling
}

TEST_CASE_METHOD (EngineFixture, "Shuffle plays an earlier step in place of this one", "[engine]")
{
    engine.setStep (2, GlitchEngine::shuffle);
    settings[GlitchEngine::shuffle].shuffleRange = 1; // always exactly one step back
    const auto out = run (stepSamples * 3);

    for (int t : { 500, 3000, 5500 })
        CHECK_THAT (out[(size_t) (2 * stepSamples + t)], WithinAbs (ramp (stepSamples + t), 1.0e-6));
}

TEST_CASE_METHOD (EngineFixture, "Chance 0 never fires", "[engine]")
{
    for (int s = 0; s < GlitchEngine::numSteps; ++s)
        engine.setStep (s, GlitchEngine::reverse);

    settings[GlitchEngine::reverse].chance = 0.0f;
    const auto out = run (stepSamples * 4);

    for (int n = 0; n < (int) out.size(); ++n)
        REQUIRE (out[(size_t) n] == ramp (n));
}

TEST_CASE_METHOD (EngineFixture, "Step edges fade instead of jumping", "[engine]")
{
    engine.setStep (1, GlitchEngine::reverse);
    const auto out = run (stepSamples * 3);

    // Around both edges of the reversed step, sample-to-sample changes stay small.
    for (int n = stepSamples - 50; n < stepSamples + 150; ++n)
        REQUIRE (std::abs (out[(size_t) n + 1] - out[(size_t) n]) < 0.002f);

    for (int n = 2 * stepSamples - 150; n < 2 * stepSamples + 50; ++n)
        REQUIRE (std::abs (out[(size_t) n + 1] - out[(size_t) n]) < 0.002f);
}

TEST_CASE ("Patterns survive text round trips; randomize follows density", "[patterns]")
{
    for (int i = 0; i < Patterns::names().size(); ++i)
    {
        const auto pattern = Patterns::get (i);
        CHECK (Patterns::fromString (Patterns::toString (pattern)) == pattern);
    }

    juce::Random random (42);
    const auto count = [] (const Patterns::Pattern& p) { return std::count_if (p.begin(), p.end(), [] (int s) { return s >= 0; }); };

    CHECK (count (Patterns::randomize (0.0f, random)) == 0);
    CHECK (count (Patterns::randomize (1.0f, random)) == GlitchEngine::numSteps);
}

TEST_CASE ("Pattern and settings survive a save and reload", "[processor]")
{
    const juce::ScopedJuceInitialiser_GUI juce;
    FractureProcessor processor;
    processor.setPattern (Patterns::fromString ("R.V.T.G.C.F.S..."));
    auto* chance = processor.apvts.getParameter (Params::chanceId (GlitchEngine::gate));
    chance->setValueNotifyingHost (0.3f);

    juce::MemoryBlock state;
    processor.getStateInformation (state);

    FractureProcessor restored;
    restored.setStateInformation (state.getData(), (int) state.getSize());

    CHECK (Patterns::toString (restored.getPattern()) == "R.V.T.G.C.F.S...");
    CHECK_THAT (restored.apvts.getRawParameterValue (Params::chanceId (GlitchEngine::gate))->load(), WithinAbs (0.3f, 1.0e-4));
}

// Not part of the normal run: renders the editor to a PNG.  ./build/FractureTests "[.snapshot]"
TEST_CASE ("Editor snapshot", "[.snapshot]")
{
    const juce::ScopedJuceInitialiser_GUI juce;
    FractureProcessor processor;
    processor.setPattern (Patterns::get (10));

    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
    const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 2.0f);
    const auto path = juce::SystemStats::getEnvironmentVariable ("FRACTURE_SNAPSHOT", "/tmp/fracture.png");
    juce::File (path).deleteFile();
    juce::FileOutputStream stream { juce::File (path) };
    REQUIRE (juce::PNGImageFormat().writeImageToStream (image, stream));
}
