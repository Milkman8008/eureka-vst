// Preset management tests: runs the real processor and PresetManager against
// a temporary preset folder.

#include <juce_audio_processors/juce_audio_processors.h>

#include <cstdio>

#include "PluginProcessor.h"

using namespace ambika_plugin;

namespace
{
    int failures = 0;

    void check (bool condition, const juce::String& what)
    {
        std::printf ("%s: %s\n", condition ? "PASS" : "FAIL", what.toRawUTF8());
        if (! condition)
            ++failures;
    }

    void set (AmbikaAudioProcessor& p, const juce::String& id, float value)
    {
        auto* parameter = p.parameters().getParameter (id);
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
    }

    float get (AmbikaAudioProcessor& p, const juce::String& id)
    {
        return p.parameters().getRawParameterValue (id)->load();
    }
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;
    const auto dir = juce::File::createTempFile ("eureka-preset-test");
    dir.createDirectory();

    AmbikaAudioProcessor processor;
    auto& presets = processor.presets();
    presets.setDirectory (dir);
    presets.setUseTrash (false);

    check (presets.presetName (0) == "Init" && ! presets.isModified (0), "fresh parts are \"Init\" and unmodified");

    // Edit part 1 and save it.
    set (processor, "filter_cutoff", 20.0f);
    set (processor, "osc1_shape", 16.0f);  // fm
    set (processor, "mod3_amount", -40.0f);
    set (processor, "part_polyphony", 0.0f);  // mono
    check (presets.isModified (0), "editing a part marks it modified");

    auto result = presets.save (0, "Deep Bass", { "Bass", "Mono", "dark" }, "tester", "test comment", false);
    check (result.wasOk(), "save: " + result.getErrorMessage());
    const auto file = dir.getChildFile ("Deep Bass.eurekapreset");
    check (file.existsAsFile(), "save writes Deep Bass.eurekapreset");
    check (file.loadFileAsString().contains ("\"format\": \"eureka-preset\"")
               || file.loadFileAsString().contains ("\"format\":\"eureka-preset\""),
           "preset file is tagged as an eureka! preset");
    check (! presets.isModified (0) && presets.presetName (0) == "Deep Bass", "after save: named and unmodified");

    result = presets.save (0, "Deep Bass", {}, {}, {}, false);
    check (result.failed(), "saving over an existing name without overwrite is refused");

    // Init.
    presets.initialise (0);
    check (get (processor, "filter_cutoff") == 96.0f && get (processor, "mod3_amount") == 0.0f,
           "init restores the default patch");
    check (presets.presetName (0) == "Init" && ! presets.isModified (0), "init names the part \"Init\"");

    // Load back.
    result = presets.load (0, file);
    check (result.wasOk(), "load: " + result.getErrorMessage());
    check (get (processor, "filter_cutoff") == 20.0f && get (processor, "osc1_shape") == 16.0f
               && get (processor, "mod3_amount") == -40.0f && get (processor, "part_polyphony") == 0.0f,
           "load restores patch and part settings (incl. negative values)");
    check (presets.presetTags (0).contains ("dark") && presets.presetAuthor (0) == "tester",
           "load restores tags and author");

    // Load into another part leaves part 1 alone.
    set (processor, "filter_cutoff", 50.0f);
    result = presets.load (2, file);
    check (result.wasOk() && get (processor, "p3_filter_cutoff") == 20.0f && get (processor, "filter_cutoff") == 50.0f,
           "loading into part 3 changes part 3 only");
    check (presets.presetName (2) == "Deep Bass" && presets.isModified (0), "each part tracks its own preset");

    // A second preset; scan, neighbours, tags.
    presets.initialise (1);
    set (processor, "p2_osc1_shape", 22.0f);
    result = presets.save (1, "Airy Pad", { "Pad" }, {}, {}, false);
    const auto all = presets.scan();
    check (all.size() == 2 && all[0].name == "Airy Pad" && all[1].name == "Deep Bass", "scan lists presets sorted by name");
    check (presets.neighbour (1, 1) == file && presets.neighbour (1, -1) == file, "next/previous cycle through presets");

    // Rename.
    result = presets.rename (file, "Sub Bass");
    const auto renamed = dir.getChildFile ("Sub Bass.eurekapreset");
    check (result.wasOk() && renamed.existsAsFile() && ! file.existsAsFile(), "rename moves the file");
    check (presets.presetName (0) == "Sub Bass" && presets.presetFile (2) == renamed,
           "rename updates the parts using that preset");
    check (presets.rename (renamed, "Airy Pad").failed(), "rename refuses a name already taken");

    // Older or hand-written files: missing parameters load as defaults.
    const auto partial = dir.getChildFile ("Partial.eurekapreset");
    partial.replaceWithText (R"({"format":"eureka-preset","version":1,"name":"Partial",
                                 "parameters":{"filter_resonance":40,"bogus_parameter":3,"osc2_range":99}})");
    result = presets.load (3, partial);
    check (result.wasOk() && get (processor, "p4_filter_resonance") == 40.0f
               && get (processor, "p4_filter_cutoff") == 96.0f,
           "missing parameters load as defaults, unknown ones are ignored");
    check (get (processor, "p4_osc2_range") == 24.0f, "out-of-range values are clamped");
    check (presets.load (3, dir.getChildFile ("nope.eurekapreset")).failed(), "loading a non-preset fails cleanly");

    // Project state keeps preset names; restored parts are not "modified".
    set (processor, "p2_filter_cutoff", 10.0f);  // Part 2 modified at save time.
    juce::MemoryBlock saved;
    processor.getStateInformation (saved);
    AmbikaAudioProcessor restored;
    restored.presets().setDirectory (dir);
    restored.setStateInformation (saved.getData(), (int) saved.getSize());
    check (restored.presets().presetName (0) == "Sub Bass" && restored.presets().presetName (1) == "Airy Pad"
               && restored.presets().presetName (2) == "Sub Bass",
           "project state restores each part's preset name");
    check (! restored.presets().isModified (1) && get (restored, "p2_filter_cutoff") == 10.0f,
           "restored project keeps edited values");

    // Init everything also resets the voice routing.
    set (processor, ambika_plugin::voicePartId (3), 3.0f);
    set (processor, ambika_plugin::kVoiceModeId, 1.0f);
    set (processor, ambika_plugin::kUnisonId, 3.0f);
    set (processor, ambika_plugin::partKeyLowId (1), 60.0f);
    set (processor, "p5_filter_cutoff", 5.0f);
    presets.initialiseAll();
    check (get (processor, ambika_plugin::voicePartId (3)) == 1.0f && get (processor, ambika_plugin::kVoiceModeId) == 0.0f
               && get (processor, ambika_plugin::kUnisonId) == 1.0f && get (processor, ambika_plugin::partKeyLowId (1)) == 0.0f,
           "init everything resets voice cards, voice mode, unison and splits");
    check (get (processor, "p5_filter_cutoff") == 96.0f && presets.presetName (4) == "Init",
           "init everything resets every part's sound");
    set (processor, ambika_plugin::voicePartId (2), 2.0f);
    presets.initialise (0);
    check (get (processor, ambika_plugin::voicePartId (2)) == 2.0f, "init part leaves the voice routing alone");

    // Delete.
    result = presets.remove (renamed);
    check (result.wasOk() && ! renamed.existsAsFile() && presets.presetFile (0) == juce::File(),
           "delete removes the file and unlinks parts");

    dir.deleteRecursively();
    std::printf ("%s (%d failure%s)\n", failures ? "FAILED" : "OK", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
