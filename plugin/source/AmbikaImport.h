#pragma once

#include <juce_core/juce_core.h>

#include <array>
#include <optional>
#include <vector>

namespace ambika_plugin
{
    // Reading the Ambika's own data formats (controller/storage.cc):
    //
    // - SD card files (.PAT patch, .PRO program, .MUL multi, .SEQ sequence):
    //   RIFF "MBKS" with a 16-character "name" chunk and "obj " chunks
    //   (object type + 1, part alias, 2 reserved bytes, then the raw data).
    // - SysEx dumps (.syx): F0 00 21 02 00 04, command (object type + 1),
    //   argument (part alias), data as high/low nibbles, checksum, F7.
    //
    // Part alias 0 = "the current part", 1..6 = part 1..6.
    namespace ambika_format
    {
        enum ObjectType
        {
            kPatch = 0,     // 112 bytes, struct Patch
            kSequence = 1,  // 72 bytes: arpeggiator settings and sequences
            kProgram = 2,   // never stored as such: patch + part
            kMulti = 3,     // 56 bytes, struct MultiData
            kPart = 4,      // 84 bytes, struct PartData
        };

        constexpr size_t kPatchSize = 112;
        constexpr size_t kPartSize = 84;
        constexpr size_t kMultiSize = 56;
        constexpr size_t kSequenceSize = 72;

        struct Object
        {
            int type = -1;
            int alias = 0;  // 0 = current part, 1..6
            std::vector<uint8_t> data;
        };

        struct Bundle
        {
            juce::String name;
            std::vector<Object> objects;

            const Object* find (int type, int alias) const;
            bool isMulti() const;
            bool hasPatch() const;
        };

        // Parses either format; returns an error message on failure.
        juce::Result parse (const juce::MemoryBlock& data, const juce::String& fallbackName, Bundle& out);
        juce::Result parseFile (const juce::File& file, Bundle& out);

        // File patterns for choosers.
        inline constexpr const char* kFilePatterns = "*.pat;*.pro;*.mul;*.seq;*.syx;*.PAT;*.PRO;*.MUL;*.SEQ;*.SYX";

        // Engine parameter values (engine table order) for a patch and,
        // optionally, part settings. Values not covered keep their defaults
        // (e.g. pan, the filter model: Ambika patches use the Ambika SVF).
        // Includes the arpeggiator / sequencer settings and steps.
        std::vector<int> toEngineValues (const uint8_t* patch, const uint8_t* part);

        // The arpeggiator / sequencer parameters in a sequence object (.SEQ),
        // as (engine parameter index, value) pairs. A sequence has no
        // arp/seq mode, so that is not among them.
        std::vector<std::pair<int, int>> sequenceValues (const uint8_t* sequence);
    }
}
