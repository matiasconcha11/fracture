#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "PluginEditor.h"
#include "PresetManager.h"

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

        // Runs `numSamples` of `source` (the ramp by default) from song start, returning the left channel.
        std::vector<float> run (int numSamples, float mix = 1.0f, std::function<float (int)> source = [] (int n) { return ramp (n); })
        {
            std::vector<float> out;
            juce::AudioBuffer<float> buffer (2, blockSize);

            for (int start = 0; start < numSamples; start += blockSize)
            {
                for (int i = 0; i < blockSize; ++i)
                    for (int ch = 0; ch < 2; ++ch)
                        buffer.setSample (ch, i, source (start + i));

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
    engine.setStep (0, 0, GlitchEngine::reverse);
    timing.running = false;
    const auto out = run (stepSamples);

    for (int n = 0; n < (int) out.size(); ++n)
        REQUIRE (out[(size_t) n] == ramp (n));
}

TEST_CASE_METHOD (EngineFixture, "Repeat plays the start of the step again", "[engine]")
{
    engine.setStep (0, 1, GlitchEngine::repeat); // step 1 starts at sample 6000
    settings[GlitchEngine::repeat].repeats = 4;  // slices of 1500 samples
    const auto out = run (stepSamples * 2);

    // Inside the second slice, we hear the first slice again.
    for (int k : { 200, 700, 1200 })
        CHECK_THAT (out[(size_t) (stepSamples + 1500 + k)], WithinAbs (ramp (stepSamples + k), 1.0e-6));
}

TEST_CASE_METHOD (EngineFixture, "Reverse plays the previous step backwards", "[engine]")
{
    engine.setStep (0, 1, GlitchEngine::reverse);
    const auto out = run (stepSamples * 2);

    for (int t : { 500, 3000, 5500 })
        CHECK_THAT (out[(size_t) (stepSamples + t)], WithinAbs (ramp (stepSamples - 1 - t), 1.0e-6));
}

TEST_CASE_METHOD (EngineFixture, "Tape stop starts at normal speed and falls behind", "[engine]")
{
    engine.setStep (0, 1, GlitchEngine::tapeStop);
    const auto out = run (stepSamples * 2);

    const auto lagAt = [&] (int t) { return ramp (stepSamples + t) - out[(size_t) (stepSamples + t)]; };

    CHECK (lagAt (200) < ramp (5));          // barely behind near the start
    CHECK (lagAt (3000) > lagAt (1000));     // falls further behind...
    CHECK (lagAt (5800) > ramp (1500));      // ...and is far behind by the end
}

TEST_CASE_METHOD (EngineFixture, "Gate silences the closed part of each pulse", "[engine]")
{
    engine.setStep (0, 0, GlitchEngine::gate);
    settings[GlitchEngine::gate].pulses = 4;  // pulses of 1500 samples
    settings[GlitchEngine::gate].duty = 0.5f; // open for the first 750
    const auto out = run (stepSamples);

    CHECK_THAT (out[400], WithinAbs (ramp (400), 1.0e-6));
    CHECK_THAT (out[1200], WithinAbs (0.0f, 1.0e-6));
    CHECK_THAT (out[1500 + 400], WithinAbs (ramp (1900), 2.0e-5)); // the 1 ms anti-click fade is still settling
}

TEST_CASE_METHOD (EngineFixture, "Shuffle plays an earlier step in place of this one", "[engine]")
{
    engine.setStep (0, 2, GlitchEngine::shuffle);
    settings[GlitchEngine::shuffle].shuffleRange = 1; // always exactly one step back
    const auto out = run (stepSamples * 3);

    for (int t : { 500, 3000, 5500 })
        CHECK_THAT (out[(size_t) (2 * stepSamples + t)], WithinAbs (ramp (stepSamples + t), 1.0e-6));
}

TEST_CASE_METHOD (EngineFixture, "Chance 0 never fires", "[engine]")
{
    for (int s = 0; s < GlitchEngine::numSteps; ++s)
        engine.setStep (0, s, GlitchEngine::reverse);

    settings[GlitchEngine::reverse].chance = 0.0f;
    const auto out = run (stepSamples * 4);

    for (int n = 0; n < (int) out.size(); ++n)
        REQUIRE (out[(size_t) n] == ramp (n));
}

TEST_CASE_METHOD (EngineFixture, "Step edges fade instead of jumping", "[engine]")
{
    engine.setStep (0, 1, GlitchEngine::reverse);
    const auto out = run (stepSamples * 3);

    // Around both edges of the reversed step, sample-to-sample changes stay small.
    for (int n = stepSamples - 50; n < stepSamples + 150; ++n)
        REQUIRE (std::abs (out[(size_t) n + 1] - out[(size_t) n]) < 0.002f);

    for (int n = 2 * stepSamples - 150; n < 2 * stepSamples + 50; ++n)
        REQUIRE (std::abs (out[(size_t) n + 1] - out[(size_t) n]) < 0.002f);
}

TEST_CASE_METHOD (EngineFixture, "Echo repeats the step's audio at the chosen division", "[engine]")
{
    engine.setStep (0, 1, GlitchEngine::echo);
    settings[GlitchEngine::echo].echoDivision = 4; // 1500 samples
    settings[GlitchEngine::echo].echoFeedback = 0.6f;
    const auto out = run (stepSamples * 2, 1.0f, [] (int n) { return n == stepSamples + 10 ? 1.0f : 0.0f; });

    CHECK_THAT (out[(size_t) stepSamples + 10], WithinAbs (1.0f, 1.0e-6));
    CHECK_THAT (out[(size_t) stepSamples + 1510], WithinAbs (0.6f, 1.0e-5));
    CHECK_THAT (out[(size_t) stepSamples + 3010], WithinAbs (0.36f, 1.0e-5));
}

TEST_CASE_METHOD (EngineFixture, "Pitch shifts an octave down", "[engine]")
{
    engine.setStep (0, 1, GlitchEngine::pitch);
    settings[GlitchEngine::pitch].semitones = -12.0f;
    const auto out = run (stepSamples * 2, 1.0f, [] (int n) { return (float) std::sin (juce::MathConstants<double>::twoPi * 880.0 * n / sampleRate); });

    // Count rising zero crossings over the middle of the step: 880 Hz in, about 440 Hz out.
    int crossings = 0;

    for (int n = stepSamples + 1000; n < stepSamples + 5800; ++n)
        crossings += out[(size_t) n - 1] < 0.0f && out[(size_t) n] >= 0.0f ? 1 : 0;

    const auto frequency = crossings * sampleRate / 4800.0;
    CHECK (frequency > 400.0);
    CHECK (frequency < 480.0);
}

TEST_CASE_METHOD (EngineFixture, "Swing delays every second step", "[engine]")
{
    engine.setStep (0, 1, GlitchEngine::reverse);
    timing.swing = 0.5; // step 1 starts at 1.5 x 6000 = 9000 instead of 6000

    const auto out = run (stepSamples * 2);

    CHECK (out[8000] == ramp (8000));                                 // still dry where step 1 used to be
    CHECK_THAT (out[9500], WithinAbs (ramp (9000 - 1 - 500), 1.0e-6)); // reversed once the swung step starts
}

TEST_CASE_METHOD (EngineFixture, "Pattern length loops early", "[engine]")
{
    engine.setStep (0, 1, GlitchEngine::reverse);
    timing.length = 4; // global step 5 is pattern step 1

    const auto out = run (stepSamples * 6);
    const auto start = 5 * stepSamples;

    CHECK_THAT (out[(size_t) start + 1000], WithinAbs (ramp (start - 1 - 1000), 1.0e-6));
    CHECK (out[(size_t) 4 * stepSamples + 1000] == ramp (4 * stepSamples + 1000));
}

TEST_CASE_METHOD (EngineFixture, "Chain plays the non-empty patterns in turn", "[engine]")
{
    engine.setStep (0, 0, GlitchEngine::reverse); // A: step 0
    engine.setStep (2, 1, GlitchEngine::reverse); // C: step 1 (B is empty and skipped)
    timing.chain = true;
    timing.length = 2;

    const auto out = run (stepSamples * 4);

    // Loop 2 (global steps 2-3) plays pattern C: step 2 dry, step 3 reversed.
    CHECK (out[(size_t) 2 * stepSamples + 1000] == ramp (2 * stepSamples + 1000));
    CHECK_THAT (out[(size_t) 3 * stepSamples + 1000], WithinAbs (ramp (3 * stepSamples - 1 - 1000), 1.0e-6));
    CHECK (engine.currentPattern.load() == 2);
}

TEST_CASE_METHOD (EngineFixture, "Mutate changes the pattern by itself", "[engine]")
{
    timing.mutate = 1.0f;
    run (stepSamples * 16 * 3);

    CHECK_FALSE (engine.isEmpty (0));
}

TEST_CASE ("Patterns survive text round trips; randomize follows density", "[patterns]")
{
    for (int i = 0; i < Patterns::names().size(); ++i)
    {
        const auto pattern = Patterns::get (i);
        CHECK (Patterns::same (Patterns::fromString (Patterns::toString (pattern)), pattern));
    }

    juce::Random random (42);
    const auto count = [] (const Patterns::Pattern& p) { return std::count_if (p.begin(), p.end(), [] (const auto& s) { return s.lane >= 0; }); };

    CHECK (count (Patterns::randomize (0.0f, random)) == 0);
    CHECK (count (Patterns::randomize (1.0f, random)) > GlitchEngine::numSteps * 3 / 4);
}

TEST_CASE ("Pattern and settings survive a save and reload", "[processor]")
{
    const juce::ScopedJuceInitialiser_GUI juce;
    FractureProcessor processor;
    processor.setPattern (0, Patterns::fromString ("R.V.T.G.C.F.S..."));
    processor.setPattern (3, Patterns::fromString ("..E.P..........."));
    auto* chance = processor.apvts.getParameter (Params::chanceId (GlitchEngine::gate));
    chance->setValueNotifyingHost (0.3f);

    juce::MemoryBlock state;
    processor.getStateInformation (state);

    FractureProcessor restored;
    restored.setStateInformation (state.getData(), (int) state.getSize());

    CHECK (Patterns::toString (restored.getPattern (0)) == "R.V.T.G.C.F.S...");
    CHECK (Patterns::toString (restored.getPattern (3)) == "..E.P...........");
    CHECK_THAT (restored.apvts.getRawParameterValue (Params::chanceId (GlitchEngine::gate))->load(), WithinAbs (0.3f, 1.0e-4));
}

TEST_CASE_METHOD (EngineFixture, "A tied block is one event across its steps", "[engine]")
{
    // Repeat over steps 1-2 (12000 samples) with 4 repeats: slices of 3000, not 1500.
    auto pattern = Patterns::fromString (".R+.............");
    for (int i = 0; i < GlitchEngine::numSteps; ++i)
        engine.setStep (0, i, pattern[(size_t) i]);

    settings[GlitchEngine::repeat].repeats = 4;
    const auto out = run (stepSamples * 3);

    // 500 samples into the second slice (at 3000 + 500), we hear the block's first slice again.
    CHECK_THAT (out[(size_t) stepSamples + 3500], WithinAbs (ramp (stepSamples + 500), 1.0e-6));
    // Crossing into the tied step doesn't restart the effect: still slice 3 of the same block.
    CHECK_THAT (out[(size_t) stepSamples + 6500], WithinAbs (ramp (stepSamples + 500), 1.0e-6));
}

TEST_CASE_METHOD (EngineFixture, "A block's own value overrides the lane setting", "[engine]")
{
    GlitchEngine::Step step { GlitchEngine::repeat };
    step.value = 0.0f; // 2 repeats: slices of 3000
    engine.setStep (0, 1, step);
    settings[GlitchEngine::repeat].repeats = 8;

    const auto out = run (stepSamples * 2);
    CHECK_THAT (out[(size_t) stepSamples + 3200], WithinAbs (ramp (stepSamples + 200), 1.0e-6));
}

TEST_CASE_METHOD (EngineFixture, "A block's own chance overrides the lane chance", "[engine]")
{
    GlitchEngine::Step step { GlitchEngine::reverse };
    step.chance = 0.0f;
    engine.setStep (0, 1, step);
    settings[GlitchEngine::reverse].chance = 1.0f;

    const auto out = run (stepSamples * 2);
    CHECK (out[(size_t) stepSamples + 3000] == ramp (stepSamples + 3000));
}

TEST_CASE_METHOD (EngineFixture, "Ring Mod multiplies by a sine", "[engine]")
{
    engine.setStep (0, 0, GlitchEngine::ringMod);
    settings[GlitchEngine::ringMod].ringFrequency = 100.0f;
    const auto out = run (stepSamples, 1.0f, [] (int) { return 1.0f; });

    // A quarter period of 100 Hz in: the carrier is at its peak.
    CHECK_THAT (out[120], WithinAbs (1.0f, 1.0e-3));
    CHECK_THAT (out[240], WithinAbs (0.0f, 1.0e-3));
}

TEST_CASE_METHOD (EngineFixture, "Stretch plays slower than real time", "[engine]")
{
    engine.setStep (0, 1, GlitchEngine::stretch);
    settings[GlitchEngine::stretch].stretchSpeed = 0.5f;
    const auto out = run (stepSamples * 2);

    // By the end of the block, playback has only got about half way through it.
    const auto heard = (out[(size_t) stepSamples + 5500] / 1.0e-5f) - stepSamples;
    CHECK (heard > 2000.0f);
    CHECK (heard < 3600.0f);
}

TEST_CASE_METHOD (EngineFixture, "Random plays one of the real effects", "[engine]")
{
    engine.setStep (0, 1, GlitchEngine::randomLane);
    const auto out = run (stepSamples * 2);

    auto changed = 0;

    for (int n = stepSamples + 200; n < 2 * stepSamples - 200; ++n)
        changed += out[(size_t) n] != ramp (n) ? 1 : 0;

    CHECK (changed > 1000);
}

TEST_CASE_METHOD (EngineFixture, "A held MIDI effect plays without the transport", "[engine]")
{
    timing.running = false;
    engine.setLiveLane (GlitchEngine::reverse);
    const auto out = run (stepSamples);

    CHECK_THAT (out[3000], WithinAbs (ramp (0), 1.0e-6)); // reading backwards from where the note started

    engine.setLiveLane (GlitchEngine::noEffect);
    const auto after = run (blockSize);
    CHECK (after[100] == ramp (100));
}

TEST_CASE ("MIDI notes switch patterns and hold effects", "[processor]")
{
    const juce::ScopedJuceInitialiser_GUI juce;
    FractureProcessor processor;
    processor.prepareToPlay (48000.0, 480);

    juce::AudioBuffer<float> buffer (2, 480);
    juce::MidiBuffer midi;
    midi.addEvent (juce::MidiMessage::noteOn (1, FractureProcessor::firstPatternNote + 2, 0.8f), 0);
    midi.addEvent (juce::MidiMessage::noteOn (1, FractureProcessor::firstLiveNote + GlitchEngine::gate, 0.8f), 0);
    buffer.clear();
    processor.processBlock (buffer, midi);

    CHECK (processor.engine.getPatternOverride() == 2);
    CHECK (processor.engine.firingLane.load() == GlitchEngine::gate);

    midi.clear();
    midi.addEvent (juce::MidiMessage::noteOff (1, FractureProcessor::firstLiveNote + GlitchEngine::gate), 0);
    processor.processBlock (buffer, midi);
    CHECK (processor.engine.firingLane.load() == GlitchEngine::noEffect);
}

TEST_CASE ("Blocks, ties and overrides survive text and state round trips", "[patterns]")
{
    auto pattern = Patterns::fromString ("R+++.V+..?..X++.");
    pattern[5].chance = 0.4f;
    pattern[5].value = 0.25f;

    auto restored = Patterns::fromString (Patterns::toString (pattern));
    Patterns::overridesFromString (Patterns::overridesToString (pattern), restored);

    CHECK (Patterns::same (pattern, restored));
    CHECK (Patterns::toString (restored) == "R+++.V+..?..X++.");
}

TEST_CASE ("Every factory preset loads and marks itself clean", "[presets]")
{
    const juce::ScopedJuceInitialiser_GUI juce;
    FractureProcessor processor;
    const auto directory = juce::File::getSpecialLocation (juce::File::tempDirectory).getNonexistentChildFile ("FracturePresets", {}, false);
    PresetManager presets (processor, directory);

    REQUIRE (PresetManager::numFactoryPresets() >= 40);

    for (int i = 0; i < PresetManager::numFactoryPresets(); ++i)
    {
        REQUIRE (presets.load (i));
        CHECK (processor.getPresetName() == presets.getPresets()[(size_t) i].name);
        CHECK_FALSE (processor.isPresetDirty());
        CHECK (presets.getCurrentIndex() == i);

        if (i > 0)
            CHECK_FALSE (processor.engine.isEmpty (0)); // every preset but Init has something in pattern A
    }

    directory.deleteRecursively();
}

TEST_CASE ("User presets save and load back", "[presets]")
{
    const juce::ScopedJuceInitialiser_GUI juce;
    FractureProcessor processor;
    const auto directory = juce::File::getSpecialLocation (juce::File::tempDirectory).getNonexistentChildFile ("FracturePresets", {}, false);
    PresetManager presets (processor, directory);

    processor.setPattern (1, Patterns::fromString ("..E+..P+........"));
    REQUIRE (presets.save ("My Glitch"));
    CHECK_FALSE (processor.isPresetDirty());

    presets.load (0);
    REQUIRE (processor.engine.isEmpty (1));

    REQUIRE (presets.load ((int) presets.getPresets().size() - 1));
    CHECK (processor.getPresetName() == "My Glitch");
    CHECK (Patterns::toString (processor.getPattern (1)) == "..E+..P+........");

    processor.setPattern (1, Patterns::get (0));
    CHECK (processor.isPresetDirty());

    directory.deleteRecursively();
}

TEST_CASE ("Sessions from 0.1 load their single pattern into A", "[processor]")
{
    const juce::ScopedJuceInitialiser_GUI juce;
    FractureProcessor processor;

    juce::ValueTree state ("PARAMS");
    state.setProperty ("pattern", "R...V...........", nullptr);
    juce::MemoryBlock data;
    juce::AudioProcessor::copyXmlToBinary (*state.createXml(), data);
    processor.setStateInformation (data.getData(), (int) data.getSize());

    CHECK (Patterns::toString (processor.getPattern (0)) == "R...V...........");
}

// Not part of the normal run: renders the editor to a PNG.  ./build/FractureTests "[.snapshot]"
TEST_CASE ("Editor snapshot", "[.snapshot]")
{
    const juce::ScopedJuceInitialiser_GUI juce;
    FractureProcessor processor;
    PresetManager presets (processor);
    presets.load (juce::SystemStats::getEnvironmentVariable ("FRACTURE_SNAPSHOT_PRESET", "48").getIntValue());
    processor.engine.setRandomSeed (3);

    // Play a few bars of a drum-like signal so the strip and the playhead have something to show.
    struct PlayHead : juce::AudioPlayHead
    {
        juce::Optional<PositionInfo> getPosition() const override { return info; }
        PositionInfo info;
    } host;

    host.info.setBpm (120.0);
    host.info.setIsPlaying (true);
    processor.setPlayHead (&host);
    processor.prepareToPlay (48000.0, 480);

    juce::AudioBuffer<float> buffer (2, 480);
    juce::MidiBuffer midi;

    for (int block = 0; block < 100 * 4 + 37; ++block)
    {
        for (int i = 0; i < 480; ++i)
        {
            const auto n = block * 480 + i;
            const auto beat = std::fmod (n / 12000.0, 1.0);
            const auto v = (float) (0.8 * std::exp (-beat * 6.0) * std::sin (n * 0.03) + 0.1 * std::sin (n * 0.31));
            buffer.setSample (0, i, v);
            buffer.setSample (1, i, v);
        }

        host.info.setPpqPosition (block * 480 / 24000.0);
        processor.processBlock (buffer, midi);
    }

    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
    const auto image = editor->createComponentSnapshot (editor->getLocalBounds(), true, 2.0f);
    const auto path = juce::SystemStats::getEnvironmentVariable ("FRACTURE_SNAPSHOT", "/tmp/fracture.png");
    juce::File (path).deleteFile();
    juce::FileOutputStream stream { juce::File (path) };
    REQUIRE (juce::PNGImageFormat().writeImageToStream (image, stream));
}
