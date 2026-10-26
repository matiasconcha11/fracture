#include "PresetManager.h"

namespace
{
    // Pattern text: a letter starts a block (R Repeat, V Reverse, T Tape Stop, X Stretch, G Gate,
    // C Crush, D Distort, F Filter, M Ring Mod, E Echo, P Pitch, S Shuffle, ? Random), '+' extends it.
    struct FactoryPreset
    {
        const char* category;
        const char* name;
        std::array<const char*, GlitchEngine::numPatterns> patterns;
        std::vector<std::pair<juce::String, float>> parameters; // plain values
        std::vector<std::pair<int, float>> stepValues = {};     // pattern A: step -> per-block value
    };

    const std::vector<FactoryPreset>& factoryPresets()
    {
        using namespace Params;
        const auto step = [] (const char* name) { return (float) stepLengthNames().indexOf (name); };

        static const std::vector<FactoryPreset> list {
            { "", "Init", { "", "", "", "" }, {} },

            // Stutter
            { "Stutter", "Beat Repeat", { "R+++....R+++....", "", "", "" }, { { repeatCount, 2 } } },
            { "Stutter", "Snare Roll Fill", { "............R+++", "", "", "" }, { { repeatCount, 5 }, { repeatPitch, 0.2f } } },
            { "Stutter", "Machine Gun", { "R+R+R+R+R+R+R+R+", "", "", "" }, { { repeatCount, 4 } } },
            { "Stutter", "Pitched Stutters", { "...R+...R+..R+++", "", "", "" }, { { repeatCount, 3 }, { repeatPitch, 0.6f } } },
            { "Stutter", "Bouncing Ball", { "........R+++++++", "", "", "" }, { { repeatCount, 5 }, { repeatPitch, 0.35f } } },
            { "Stutter", "Stutter Build (Chain)",
              { "............R+++", "........R+++R+++", "....R+++R+++R+++", "R+++R+++R+R+R+R+" },
              { { chain, 1 }, { repeatCount, 2 } } },

            // Time
            { "Time", "Tape Stop Bar End", { "............T+++", "", "", "" }, { { tapeCurve, 0.6f } } },
            { "Time", "Tape Wobble", { "T+..T+..T+..T+..", "", "", "" }, { { tapeCurve, 0.15f } } },
            { "Time", "Half-Time Stretch", { "X+++++++X+++++++", "", "", "" }, { { stretchSpeed, 0.5f } } },
            { "Time", "Slow Motion Breaks", { "X+++....X+++....", "", "", "" }, { { stretchSpeed, 0.4f } } },
            { "Time", "Reverse Tails", { ".V..V..V.V..V..V", "", "", "" }, {} },
            { "Time", "Reverse Swells", { "..V+......V+..V+", "", "", "" }, {} },
            { "Time", "Rewind", { "........V+++++++", "", "", "" }, {} },

            // Rhythm
            { "Rhythm", "Gated 16ths", { "G+++G+++G+++G+++", "", "", "" }, { { gatePulses, 2 }, { gateDuty, 0.5f } } },
            { "Rhythm", "Offbeat Chop", { "..G+..G+..G+..G+", "", "", "" }, { { gatePulses, 0 }, { gateDuty, 0.4f } } },
            { "Rhythm", "Stutter Gate", { "G.G.G+G.G.G+G+++", "", "", "" }, { { gatePulses, 4 }, { gateDuty, 0.35f } } },
            { "Rhythm", "Triplet Chop", { "G+G+G+G+G+G+G+G+", "", "", "" }, { { stepLength, step ("1/8T") }, { gatePulses, 1 } } },
            { "Rhythm", "Swing Shuffle", { "..S...S.S...S.S+", "", "", "" }, { { swing, 0.3f }, { shuffleRange, 2 } } },
            { "Rhythm", "Breakbeat Shuffle", { "S+..S.S.S+..S.S+", "", "", "" }, { { shuffleRange, 4 } } },
            { "Rhythm", "Polyrhythm 5", { "R.G..", "", "", "" }, { { length, 5 }, { gatePulses, 2 } } },
            { "Rhythm", "Polyrhythm 7", { "G..R.V.", "", "", "" }, { { length, 7 } } },

            // Lo-Fi
            { "Lo-Fi", "Bitcrushed Hits", { "C...C...C...C...", "", "", "" }, { { crushAmount, 0.75f } } },
            { "Lo-Fi", "Crunchy Bar", { "C+++C+++C+++C+++", "", "", "" }, { { crushAmount, 0.45f } } },
            { "Lo-Fi", "Dirt on the One", { "D+..D+..D+..D+..", "", "", "" }, { { distortAmount, 0.7f } } },
            { "Lo-Fi", "Overdriven Fill", { "............D+++", "", "", "" }, { { distortAmount, 0.95f } } },
            { "Lo-Fi", "Robot Voice", { "M+M+..M+M+..M+++", "", "", "" }, { { ringFrequency, 300.0f } } },
            { "Lo-Fi", "Metallic Ring", { "M+++++++M+++++++", "", "", "" }, { { ringFrequency, 1200.0f } } },

            // Filter & Space
            { "Filter & Space", "Filter Drops", { "...F...F...F.F++", "", "", "" }, { { filterResonance, 0.5f } } },
            { "Filter & Space", "Filter Opens", { "F+++F+++F+++F+++", "", "", "" }, { { filterDirection, 1 }, { filterResonance, 0.4f } } },
            { "Filter & Space", "Wah Pulse", { "F+F+F+F+F+F+F+F+", "", "", "" }, { { filterResonance, 0.8f } } },
            { "Filter & Space", "Dub Throws", { "...E.......E..E+", "", "", "" }, { { echoFeedback, 0.7f } } },
            { "Filter & Space", "Echo Tail", { "............E+++", "", "", "" }, { { echoFeedback, 0.85f }, { echoTime, 1 } } },
            { "Filter & Space", "Space Delays", { "E...E...E...E...", "", "", "" }, { { echoFeedback, 0.5f }, { echoTime, 3 } } },

            // Pitch
            { "Pitch", "Octave Drops", { "....P+......P+..", "", "", "" }, { { pitchSemitones, -12 } } },
            { "Pitch", "Chipmunk Fills", { "............P+++", "", "", "" }, { { pitchSemitones, 12 } } },
            { "Pitch", "Fifth Up Stabs", { "P...P...P...P...", "", "", "" }, { { pitchSemitones, 7 } } },
            { "Pitch", "Pitch Staircase", { "P+P+P+P+........", "", "", "" }, { { pitchSemitones, 0 } },
              { { 0, 0.75f }, { 2, 0.625f }, { 4, 0.5f }, { 6, 0.375f } } }, // +12, +6, 0, -6 semitones

            // Random & Evolving
            { "Random & Evolving", "Dice Rolls", { "?.?.?.?.?.?.?.?.", "", "", "" }, {} },
            { "Random & Evolving", "Constant Chaos", { "????????????????", "", "", "" }, {} },
            { "Random & Evolving", "Slow Evolve", { "R+..V+..G...S.T+", "", "", "" }, { { mutate, 0.3f } } },
            { "Random & Evolving", "Wild Mutation", { "R.V.G.C.S.T.F.E.", "", "", "" }, { { mutate, 0.9f } } },
            { "Random & Evolving", "Probability Groove", { "R+V+G+S+R+C+T+S+", "", "", "" },
              { { chanceId (GlitchEngine::repeat), 0.5f }, { chanceId (GlitchEngine::reverse), 0.4f }, { chanceId (GlitchEngine::gate), 0.6f },
                { chanceId (GlitchEngine::shuffle), 0.5f }, { chanceId (GlitchEngine::crush), 0.5f }, { chanceId (GlitchEngine::tapeStop), 0.4f } } },
            { "Random & Evolving", "Four Scenes (Chain)",
              { "R+++....V+......", "G+++G+++....S.S.", "C+..D+..C+..D+++", "X+++++++T+++++++" }, { { chain, 1 } } },

            // Combos
            { "Combos", "Glitch Mess", { "R.VS.CG.RT.F.S.R", "", "", "" }, {} },
            { "Combos", "Full Bar Mix", { "R+V+G+T+X+C+F+E+", "", "", "" }, {} },
            { "Combos", "IDM Breaks", { "R.V+S.G+R+..C+P.", "", "", "" }, { { stepLength, step ("1/32") }, { repeatCount, 4 } } },
            { "Combos", "Vocal Chops", { "V+..R+..X+..P+..", "", "", "" }, { { stretchSpeed, 0.6f }, { pitchSemitones, 5 } } },
            { "Combos", "Drum Mangler", { "R+S+G+C+R+S+D+T+", "", "", "" }, { { distortAmount, 0.5f } } },
            { "Combos", "Ambient Smear", { "X+++++++E+++++++", "", "", "" },
              { { stepLength, step ("1/8") }, { stretchSpeed, 0.35f }, { echoFeedback, 0.8f }, { mix, 0.7f } } },
            { "Combos", "Triplet Glitch", { "R+.V+.G+.S+.C+..", "", "", "" }, { { stepLength, step ("1/16T") } } },
            { "Combos", "Build-Up (Chain)",
              { "........F+++F+++", "....R+..F+++R+++", "R+R+F+++R+R+R+R+", "R+R+R+R+R+R+T+++" },
              { { chain, 1 }, { filterDirection, 1 }, { repeatCount, 3 } } },
        };

        return list;
    }

    // Presets converted from the Glitch 2 factory banks (see scripts/convert_glitch_presets.py and CREDITS.md).
    struct LibraryPreset
    {
        const char* category;
        const char* name;
        std::array<const char*, GlitchEngine::numPatterns> patterns;
        int stepLength;
        int length;
        bool chain;
    };

    constexpr LibraryPreset glitchLibrary[] {
#include "GlitchLibrary.inc"
    };

    // Our own presets first, then the Glitch 2 library, as one list.
    const std::vector<FactoryPreset>& allFactoryPresets()
    {
        static const std::vector<FactoryPreset> all = []
        {
            auto list = factoryPresets();

            for (const auto& preset : glitchLibrary)
                list.push_back ({ preset.category, preset.name, preset.patterns,
                                  { { Params::stepLength, (float) preset.stepLength },
                                    { Params::length, (float) preset.length },
                                    { Params::chain, preset.chain ? 1.0f : 0.0f } } });

            return list;
        }();

        return all;
    }
}

PresetManager::PresetManager (FractureProcessor& p, juce::File directory)
    : processor (p), userDirectory (std::move (directory))
{
    refresh();
}

juce::File PresetManager::defaultUserDirectory()
{
    const auto presetsRoot = juce::File::getSpecialLocation (juce::File::userHomeDirectory).getChildFile ("Library/Audio/Presets");
    const auto standard = presetsRoot.getChildFile ("Rhizr/Fracture");

    // Presets saved before the Rhizr rename lived under the developer's own name: move them over once.
    if (const auto legacy = presetsRoot.getChildFile ("Matias Concha/Fracture"); legacy.isDirectory() && ! standard.exists())
        if (standard.getParentDirectory().createDirectory().wasOk())
            legacy.moveFileTo (standard);

    // ~/Library/Audio/Presets may point at an external drive that isn't mounted.
    if (standard.isDirectory() || standard.createDirectory().wasOk())
        return standard;

    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Fracture Presets");
}

int PresetManager::numFactoryPresets()
{
    return (int) allFactoryPresets().size();
}

void PresetManager::refresh()
{
    presets.clear();

    for (const auto& factory : allFactoryPresets())
        presets.push_back ({ factory.name, factory.category, true, {} });

    auto files = userDirectory.findChildFiles (juce::File::findFiles, false, juce::String ("*") + fileExtension);
    std::sort (files.begin(), files.end(), [] (const auto& a, const auto& b)
               { return a.getFileNameWithoutExtension().compareNatural (b.getFileNameWithoutExtension()) < 0; });

    for (const auto& file : files)
        presets.push_back ({ file.getFileNameWithoutExtension(), "User", false, file });
}

void PresetManager::applyFactory (int index)
{
    const auto& preset = allFactoryPresets()[(size_t) index];
    const FractureProcessor::ScopedPresetLoad loading (processor);

    // Start from defaults (keeping bypass and the selected slot), then apply the preset's changes.
    for (auto* parameter : processor.getParameters())
        if (parameter != processor.getBypassParameter())
            parameter->setValueNotifyingHost (parameter->getDefaultValue());

    for (const auto& [id, value] : preset.parameters)
        if (auto* parameter = processor.apvts.getParameter (id))
            parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));

    for (int p = 0; p < GlitchEngine::numPatterns; ++p)
    {
        auto pattern = Patterns::fromString (preset.patterns[(size_t) p]);

        if (p == 0)
            for (const auto& [stepIndex, value] : preset.stepValues)
                pattern[(size_t) stepIndex].value = value;

        processor.setPattern (p, pattern);
    }

    processor.setPresetName (preset.name);
}

bool PresetManager::load (int index)
{
    if (! juce::isPositiveAndBelow (index, (int) presets.size()))
        return false;

    const auto& preset = presets[(size_t) index];

    if (preset.factory)
    {
        applyFactory (index);
    }
    else
    {
        const auto xml = juce::XmlDocument::parse (preset.file);

        if (xml == nullptr)
            return false;

        auto state = juce::ValueTree::fromXml (*xml);

        if (! state.hasType (processor.apvts.state.getType()))
            return false;

        // Presets never change whether the plugin is bypassed.
        for (auto child : state)
            if (child.getProperty ("id").toString() == Params::bypass)
                child.setProperty ("value", processor.apvts.getRawParameterValue (Params::bypass)->load(), nullptr);

        state.setProperty ("presetName", preset.name, nullptr);
        state.setProperty ("presetDirty", false, nullptr);
        processor.setStateTree (state);
    }

    lastLoaded = index;
    processor.markPresetClean();
    processor.updateHostDisplay (juce::AudioProcessor::ChangeDetails().withNonParameterStateChanged (true));
    return true;
}

void PresetManager::step (int delta)
{
    const auto count = (int) presets.size();
    const auto current = getCurrentIndex();
    load (current < 0 ? 0 : ((current + delta) % count + count) % count);
}

bool PresetManager::save (const juce::String& requestedName)
{
    const auto name = juce::File::createLegalFileName (requestedName.trim()).substring (0, 64);

    if (name.isEmpty() || ! (userDirectory.isDirectory() || userDirectory.createDirectory().wasOk()))
        return false;

    processor.setPresetName (name);
    auto state = processor.getStateTree();
    state.setProperty ("presetDirty", false, nullptr);

    const auto xml = state.createXml();

    if (xml == nullptr || ! xml->writeTo (userDirectory.getChildFile (name + fileExtension)))
        return false;

    processor.markPresetClean();
    refresh();
    lastLoaded = -1;
    return true;
}

bool PresetManager::remove (int index)
{
    if (! juce::isPositiveAndBelow (index, (int) presets.size()) || presets[(size_t) index].factory)
        return false;

    const auto wasCurrent = index == getCurrentIndex();

    if (! presets[(size_t) index].file.deleteFile())
        return false;

    refresh();

    // The deleted preset's sound stays loaded, but now as unsaved changes.
    if (wasCurrent)
        processor.markPresetDirty();

    return true;
}

int PresetManager::getCurrentIndex() const
{
    const auto name = processor.getPresetName();

    if (juce::isPositiveAndBelow (lastLoaded, (int) presets.size()) && presets[(size_t) lastLoaded].name == name)
        return lastLoaded;

    for (size_t i = presets.size(); i-- > 0;) // user presets win over a factory one with the same name
        if (presets[i].name == name)
            return (int) i;

    return -1;
}

bool PresetManager::isCurrentUserPreset() const
{
    const auto index = getCurrentIndex();
    return index >= 0 && ! presets[(size_t) index].factory;
}
