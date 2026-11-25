// Preset management tests: runs the real processor and PresetManager against
// a temporary preset folder.

#include <juce_audio_processors/juce_audio_processors.h>

#include <cstdio>

#include "PluginProcessor.h"
#include "filter_models.h"

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

    // ---- Filter model: Ripples for new sounds, the SVF for older content ---
    {
        const float ripples = (float) ambika::plugin::RipplesFilterModel();
        check (get (processor, "filter_model") == ripples && get (processor, "p6_filter_model") == ripples,
               "new parts use the Ripples filter");
        set (processor, "p6_filter_model", 2.0f);
        presets.initialise (5);
        check (get (processor, "p6_filter_model") == ripples, "Init sets the Ripples filter");

        // A preset written before the filter model existed.
        const auto old = dir.getChildFile ("Old Preset.eurekapreset");
        old.replaceWithText (R"({"format": "eureka-preset", "version": 1, "name": "Old Preset",
                                "parameters": {"filter_cutoff": 50}})");
        check (presets.load (5, old).wasOk() && get (processor, "p6_filter_model") == 0.0f
                   && get (processor, "p6_filter_cutoff") == 50.0f,
               "a preset without a filter model loads with the Ambika SVF");
        old.deleteFile();

        // A project saved before the filter model existed.
        juce::MemoryBlock saved;
        processor.getStateInformation (saved);
        auto xml = juce::AudioProcessor::getXmlFromBinary (saved.getData(), (int) saved.getSize());
        auto tree = juce::ValueTree::fromXml (*xml);
        for (int part = 0; part < kNumParts; ++part)
            tree.removeChild (tree.getChildWithProperty ("id", partParameterId (part, juce::String ("filter_model"))), nullptr);
        set (processor, "p3_filter_model", ripples);
        juce::MemoryBlock oldProject;
        juce::AudioProcessor::copyXmlToBinary (*tree.createXml(), oldProject);
        processor.setStateInformation (oldProject.getData(), (int) oldProject.getSize());
        check (get (processor, "p3_filter_model") == 0.0f, "a project without filter models loads with the Ambika SVF");
        processor.setStateInformation (saved.getData(), (int) saved.getSize());
        check (get (processor, "filter_model") == ripples, "a project with filter models keeps them");
        presets.initialise (2);
        presets.initialise (5);
    }

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
    std::vector<PresetInfo> mine;
    const auto all = presets.scan();
    for (const auto& info : all)
        if (! info.factory)
            mine.push_back (info);
    check (mine.size() == 2 && mine[0].name == "Airy Pad" && mine[1].name == "Deep Bass",
           "scan lists the user's presets sorted by name");
    size_t index = 0;
    while (index < all.size() && all[index].file != presets.presetFile (1))
        ++index;
    check (index < all.size() && presets.neighbour (1, 1) == all[(index + 1) % all.size()].file
               && presets.neighbour (1, -1) == all[(index + all.size() - 1) % all.size()].file,
           "next/previous step through the list (user and factory presets)");

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
    set (processor, ambika_plugin::kVoiceModeId, 0.0f);
    set (processor, ambika_plugin::kUnisonId, 3.0f);
    set (processor, ambika_plugin::partKeyLowId (1), 60.0f);
    set (processor, "p5_filter_cutoff", 5.0f);
    presets.initialiseAll();
    check (get (processor, ambika_plugin::voicePartId (3)) == 1.0f && get (processor, ambika_plugin::kVoiceModeId) == 2.0f
               && get (processor, ambika_plugin::kUnisonId) == 1.0f && get (processor, ambika_plugin::partKeyLowId (1)) == 0.0f,
           "init everything resets voice cards, voice mode (poly), unison and splits");
    check (get (processor, "p5_filter_cutoff") == 96.0f && presets.presetName (4) == "Init",
           "init everything resets every part's sound");
    set (processor, ambika_plugin::voicePartId (2), 2.0f);
    presets.initialise (0);
    check (get (processor, ambika_plugin::voicePartId (2)) == 2.0f, "init part leaves the voice routing alone");

    // Delete.
    result = presets.remove (renamed);
    check (result.wasOk() && ! renamed.existsAsFile() && presets.presetFile (0) == juce::File(),
           "delete removes the file and unlinks parts");

    // ---- Ambika factory programs and file import ---------------------------
    using namespace ambika_plugin::ambika_format;
    const auto& factory = PresetManager::factoryPresets();
    check (factory.size() == 128, "128 Ambika factory programs are built in (" + juce::String ((int) factory.size()) + ")");
    check (factory[0].name == "junon" && factory[0].factory && factory[0].tags.contains ("Factory"),
           "factory program 000 is \"junon\", tagged Factory");
    int listed = 0;
    for (const auto& info : presets.scan())
        listed += info.factory ? 1 : 0;
    check (listed == 128, "the browser lists the factory programs");

    // Load a factory program into part 2 and compare with the raw file.
    const juce::File programsDir (AMBIKA_PROGRAMS_DIR);
    Bundle raw;
    check (parseFile (programsDir.getChildFile ("005.PRO"), raw).wasOk(), "parse 005.PRO from the Ambika repository");
    const auto* rawPatch = raw.find (kPatch, 0);
    const auto* rawPart = raw.find (kPart, 0);
    check (rawPatch != nullptr && rawPart != nullptr, ".PRO holds a patch and part settings");
    result = presets.load (1, factory[5].file);
    check (result.wasOk() && presets.presetName (1) == raw.name && ! presets.isModified (1),
           "load factory program into part 2: " + presets.presetName (1));
    bool matches = rawPatch != nullptr;
    for (int i = 0; matches && i < ambika::plugin::NumParameters(); ++i)
    {
        const auto& info = ambika::plugin::GetParameter (i);
        if (info.target != ambika::plugin::TARGET_PATCH || info.address >= 104)
            continue;
        int expected = info.min_value < 0 ? (int) (int8_t) rawPatch->data[info.address] : rawPatch->data[info.address];
        expected = juce::jlimit<int> (info.min_value, info.max_value, expected);
        matches = juce::roundToInt (get (processor, partParameterId (1, info)))
                  == (info.choices ? expected - info.min_value : expected);
        if (! matches)
            std::printf ("      mismatch %s: param %.1f, byte %d -> expected %d\n", info.id,
                         get (processor, partParameterId (1, info)), rawPatch->data[info.address], expected);
    }
    check (matches, "every patch parameter matches the program's bytes");
    check (get (processor, "p2_filter_model") == 0.0f, "Ambika programs use the Ambika SVF");
    if (rawPart != nullptr)
    {
        // The program's arpeggiator settings and sequences come along.
        const uint8_t* p = rawPart->data.data();
        auto value = [&] (const char* id) { return juce::roundToInt (get (processor, juce::String ("p2_") + id)); };
        const bool sequences = value ("seq1_step3") == p[16 + 2] && value ("seq2_step5") == p[16 + 16 + 4]
                               && value ("pattern_note2") == (p[16 + 32 + 2] & 0x7f)
                               && value ("pattern_gate2") == (p[16 + 32 + 2] >> 7)
                               && value ("pattern_velocity2") == (p[16 + 33 + 2] & 0x7f)
                               && value ("arp_divider") == juce::jmin (14, (int) p[11])
                               && value ("arp_mode") == p[7]
                               && value ("seq1_length") == juce::jlimit (1, 16, (int) p[12]);
        check (sequences, "the program's arpeggiator settings and sequences load too");
    }
    check (presets.rename (factory[5].file, "x").failed() && presets.remove (factory[5].file).failed(),
           "factory presets cannot be renamed or deleted");

    // A .PRO file from disk.
    result = presets.load (2, programsDir.getChildFile ("000.PRO"));
    check (result.wasOk() && presets.presetName (2) == "junon" && presets.presetFile (2) == juce::File(),
           "load a .PRO file into part 3 (not saved over)");

    // The same patch as a SysEx dump.
    auto sysex = [] (int command, int argument, const std::vector<uint8_t>& data)
    {
        juce::MemoryOutputStream out;
        for (uint8_t b : { 0xf0, 0x00, 0x21, 0x02, 0x00, 0x04 }) out.writeByte ((char) b);
        out.writeByte ((char) command);
        out.writeByte ((char) argument);
        uint8_t checksum = 0;
        for (auto b : data)
        {
            checksum = (uint8_t) (checksum + b);
            out.writeByte ((char) (b >> 4));
            out.writeByte ((char) (b & 0x0f));
        }
        out.writeByte ((char) (checksum >> 4));
        out.writeByte ((char) (checksum & 0x0f));
        out.writeByte ((char) 0xf7);
        return out.getMemoryBlock();
    };
    const auto syxFile = dir.getChildFile ("dump.syx");
    syxFile.replaceWithData (sysex (1, 0, rawPatch->data).getData(), rawPatch->data.size() * 2 + 11);
    result = presets.load (3, syxFile);
    check (result.wasOk() && get (processor, "p4_osc1_shape") == get (processor, "p2_osc1_shape")
               && get (processor, "p4_filter_cutoff") == get (processor, "p2_filter_cutoff"),
           "load an Ambika SysEx patch dump");
    auto corrupt = sysex (1, 0, rawPatch->data);
    static_cast<uint8_t*> (corrupt.getData())[20] ^= 0x01;
    syxFile.replaceWithData (corrupt.getData(), corrupt.getSize());
    check (presets.load (3, syxFile).failed(), "a SysEx dump with a bad checksum is rejected");

    // A sequence dump (.SEQ contents): PartData bytes 8..79. It sets part
    // 4's arpeggiator and sequences and leaves its sound and mode alone.
    {
        set (processor, "p4_arp_mode", 1.0f);
        std::vector<uint8_t> sequence (kSequenceSize, 0);
        sequence[0] = 2;   // direction: up&down
        sequence[1] = 3;   // octaves
        sequence[3] = 8;   // step 1/8
        sequence[4] = 7;   // seq 1 length
        sequence[6] = 24;  // pattern length (clamped to 16)
        sequence[8 + 0] = 200;               // seq 1 step 1
        sequence[8 + 32 + 2] = 0x80 | 64;    // pattern step 2: gate, note 64
        sequence[8 + 33 + 2] = 0x80 | 90;    // tie, velocity 90
        const float cutoff = get (processor, "p4_filter_cutoff");
        syxFile.replaceWithData (sysex (kSequence + 1, 0, sequence).getData(), sequence.size() * 2 + 11);
        result = presets.load (3, syxFile);
        check (result.wasOk() && get (processor, "p4_arp_direction") == 2.0f && get (processor, "p4_arp_octave") == 3.0f
                   && get (processor, "p4_arp_divider") == 8.0f && get (processor, "p4_seq1_length") == 7.0f
                   && get (processor, "p4_pattern_length") == 16.0f && get (processor, "p4_seq1_step1") == 200.0f
                   && get (processor, "p4_pattern_note2") == 64.0f && get (processor, "p4_pattern_gate2") == 1.0f
                   && get (processor, "p4_pattern_velocity2") == 90.0f && get (processor, "p4_pattern_legato2") == 1.0f,
               "load an Ambika sequence dump into a part (" + result.getErrorMessage() + ")");
        check (get (processor, "p4_arp_mode") == 1.0f && get (processor, "p4_filter_cutoff") == cutoff,
               "a sequence leaves the part's sound and arp mode alone");
    }

    // A multi (.MUL): part 1 = 000.PRO on voices 1,3,5 below C4; part 2 =
    // 005.PRO on voices 2,4,6 from C4.
    Bundle junon;
    parseFile (programsDir.getChildFile ("000.PRO"), junon);
    auto riffObject = [] (juce::MemoryOutputStream& out, int type, int alias, const std::vector<uint8_t>& data)
    {
        out.write ("obj ", 4);
        out.writeInt ((int) data.size() + 4);
        out.writeByte ((char) (type + 1));
        out.writeByte ((char) alias);
        out.writeShort (0);
        out.write (data.data(), data.size());
    };
    juce::MemoryOutputStream body;
    body.write ("MBKS", 4);
    body.write ("name", 4);
    body.writeInt (16);
    body.write ("split test      ", 16);
    std::vector<uint8_t> multi (kMultiSize, 0);
    const uint8_t mapping[] = { 1, 0, 59, 0x15, 1, 60, 127, 0x2a };
    std::copy (std::begin (mapping), std::end (mapping), multi.begin());
    riffObject (body, kMulti, 0, multi);
    riffObject (body, kPatch, 1, junon.find (kPatch, 0)->data);
    riffObject (body, kPart, 1, junon.find (kPart, 0)->data);
    riffObject (body, kPatch, 2, rawPatch->data);
    riffObject (body, kPart, 2, rawPart->data);
    juce::MemoryOutputStream mul;
    mul.write ("RIFF", 4);
    mul.writeInt ((int) body.getDataSize());
    mul.write (body.getData(), body.getDataSize());
    const auto mulFile = dir.getChildFile ("SPLIT.MUL");
    mulFile.replaceWithData (mul.getData(), mul.getDataSize());
    presets.initialiseAll();
    result = presets.load (0, mulFile);
    check (result.wasOk() && get (processor, voicePartId (0)) == 1.0f && get (processor, voicePartId (1)) == 2.0f
               && get (processor, voicePartId (4)) == 1.0f && get (processor, voicePartId (5)) == 2.0f,
           "a .MUL multi sets the voice cards (1,3,5 -> part 1; 2,4,6 -> part 2)");
    check (get (processor, partKeyHighId (0)) == 59.0f && get (processor, partKeyLowId (1)) == 60.0f,
           "a .MUL multi sets the keyboard splits");
    check (get (processor, "p2_osc1_shape") == get (processor, "p4_osc1_shape") && presets.presetName (0) == "split test 1",
           "a .MUL multi loads each part's patch");

    // Library import: files and folders become eureka! presets.
    juce::StringArray errors;
    const int added = presets.importToLibrary ({ programsDir.getChildFile ("001.PRO"), mulFile }, errors);
    check (added == 3 && errors.isEmpty(), "importing a .PRO and a 2-part .MUL adds 3 presets ("
                                                + juce::String (added) + ")");
    const auto imported = dir.getChildFile ("Imported").getChildFile ("moof!.eurekapreset");
    const auto importedAlt = dir.getChildFile ("Imported").findChildFiles (juce::File::findFiles, false, "moof*");
    check (! importedAlt.isEmpty() && presets.load (5, importedAlt[0]).wasOk(),
           "an imported preset loads like any other");
    juce::ignoreUnused (imported);

    dir.deleteRecursively();
    std::printf ("%s (%d failure%s)\n", failures ? "FAILED" : "OK", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
