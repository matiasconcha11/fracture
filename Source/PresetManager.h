#pragma once

#include "PluginProcessor.h"

// Factory presets are defined in code; user presets are XML files in the user preset folder.
// Message thread only.
class PresetManager
{
public:
    static constexpr const char* fileExtension = ".fracturepreset";

    struct Preset
    {
        juce::String name;
        juce::String category; // "User" for user presets
        bool factory = false;
        juce::File file;
    };

    explicit PresetManager (FractureProcessor&, juce::File userDirectory = defaultUserDirectory());

    // Factory presets first (grouped by category), then user presets sorted by name.
    const std::vector<Preset>& getPresets() const { return presets; }
    void refresh();

    bool load (int index);
    void step (int delta); // previous / next, wrapping around

    // Writes the current state as a user preset; an existing one with the same name is replaced.
    bool save (const juce::String& name);
    bool remove (int index);

    int getCurrentIndex() const;
    bool isCurrentUserPreset() const;

    juce::File getUserDirectory() const { return userDirectory; }
    static juce::File defaultUserDirectory();
    static int numFactoryPresets();

private:
    void applyFactory (int index);

    FractureProcessor& processor;
    juce::File userDirectory;
    std::vector<Preset> presets;
    int lastLoaded = -1; // names can repeat across banks, so remember which entry was loaded
};
