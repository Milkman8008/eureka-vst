#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <vector>

#include "AmbikaImport.h"
#include "PluginParameters.h"

namespace ambika_plugin
{
    // A preset is the sound of one part: every patch and part parameter
    // (oscillators, mixer, filter, envelopes, LFOs, matrix, voicing, glide...).
    // The multi setup (channels, key ranges, voice assignment) is not part of a
    // preset; it is saved with the host project.
    //
    // Presets are JSON files (.eurekapreset) holding engine values by parameter
    // ID, so they stay readable and survive parameter additions: a missing
    // parameter loads as its default, an unknown one is ignored.
    struct PresetInfo
    {
        juce::File file;
        juce::String name;
        juce::StringArray tags;
        juce::String author;
        juce::String comment;
        juce::Time modified;
        bool factory = false;  // Built-in Ambika program: read only.
    };

    class PresetManager
    {
    public:
        static constexpr const char* fileExtension = ".eurekapreset";

        // Suggested tags offered in the save dialog and the browser filter.
        static const juce::StringArray& standardTags();

        explicit PresetManager (juce::AudioProcessorValueTreeState& state);

        // Defaults to Documents/eureka/presets, created on first use (never
        // while the host is merely loading or scanning the plug-in).
        // Tests point it elsewhere.
        void setDirectory (const juce::File& directory);
        juce::File getDirectory() const { return directory; }
        // Deleted presets go to the Trash; tests turn that off.
        void setUseTrash (bool shouldUseTrash) { useTrash = shouldUseTrash; }

        // All presets: the user's (directory and sub-folders) and the 128
        // factory programs of the Ambika, sorted by name.
        std::vector<PresetInfo> scan() const;
        static bool readInfo (const juce::File& file, PresetInfo& info);

        // The Ambika factory programs (compiled in). Their files are virtual
        // paths under /eureka-factory/ that only this class understands.
        static const std::vector<PresetInfo>& factoryPresets();
        static bool isFactory (const juce::File& file);
        static bool isEurekaPreset (const juce::File& file);

        // Message thread only: these change host parameters.
        // Loads a eureka! preset, a factory program, or an Ambika file
        // (.PAT/.PRO/.syx into the part; a .MUL multi into all six parts with
        // its key ranges and voice assignment).
        juce::Result load (int part, const juce::File& file);
        juce::Result loadAmbika (int part, const ambika_format::Bundle& bundle, const juce::File& source);

        // Converts Ambika files into eureka! presets in <directory>/Imported.
        // Multis become one preset per part. Returns how many were written.
        int importToLibrary (const juce::Array<juce::File>& files, juce::StringArray& errors);
        juce::Result save (int part, const juce::String& name, const juce::StringArray& tags,
                           const juce::String& author, const juce::String& comment, bool overwrite);
        void initialise (int part);
        // All six parts, plus the voice routing (card assignment, voice mode,
        // unison, keyboard splits) back to the defaults.
        void initialiseAll();

        juce::Result rename (const juce::File& file, const juce::String& newName);
        juce::Result remove (const juce::File& file);

        juce::File fileForName (const juce::String& name) const;

        // What each part currently holds, for the editor.
        juce::String presetName (int part) const { return current[(size_t) part].name; }
        juce::File presetFile (int part) const { return current[(size_t) part].file; }
        juce::StringArray presetTags (int part) const { return current[(size_t) part].tags; }
        juce::String presetAuthor (int part) const { return current[(size_t) part].author; }
        juce::String presetComment (int part) const { return current[(size_t) part].comment; }
        // True when the part's parameters differ from the loaded/saved preset.
        bool isModified (int part) const;

        // Neighbouring preset in scan order (direction -1 / +1), for the
        // previous/next buttons. Returns an empty file if there is none.
        juce::File neighbour (int part, int direction) const;

        // Persistence of the per-part preset names in the plug-in state.
        void writeState (juce::ValueTree& tree) const;
        void readState (const juce::ValueTree& tree);

    private:
        struct Current
        {
            juce::String name { "Init" };
            juce::File file;
            juce::StringArray tags;
            juce::String author;
            juce::String comment;
            std::vector<int> snapshot;  // Engine values when loaded/saved.
        };

        std::vector<int> captureValues (int part) const;
        juce::Result writePresetFile (const juce::File& file, const juce::String& name, const juce::StringArray& tags,
                                      const juce::String& author, const juce::String& comment,
                                      const std::vector<int>& values) const;
        void setParameter (const juce::String& id, float denormalisedValue);
        void applyValues (int part, const std::vector<int>& values);
        static juce::String sanitiseFileName (const juce::String& name);

        juce::AudioProcessorValueTreeState& state;
        juce::File directory;
        bool useTrash = true;
        std::array<Current, kNumParts> current;
    };
}
