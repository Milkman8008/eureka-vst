#include "AmbikaImport.h"

#include "PluginParameters.h"

namespace ambika_plugin::ambika_format
{
    namespace
    {
        size_t expectedSize (int type)
        {
            switch (type)
            {
                case kPatch: return kPatchSize;
                case kSequence: return kSequenceSize;
                case kMulti: return kMultiSize;
                case kPart: return kPartSize;
                default: return 0;
            }
        }

        uint32_t readLE32 (const uint8_t* p)
        {
            return (uint32_t) p[0] | ((uint32_t) p[1] << 8) | ((uint32_t) p[2] << 16) | ((uint32_t) p[3] << 24);
        }

        juce::Result parseRiff (const uint8_t* d, size_t size, Bundle& out)
        {
            if (size < 12 || memcmp (d + 8, "MBKS", 4) != 0)
                return juce::Result::fail ("not an Ambika file (expected RIFF MBKS)");
            size_t i = 12;
            while (i + 8 <= size)
            {
                const auto chunkSize = (size_t) readLE32 (d + i + 4);
                if (i + 8 + chunkSize > size)
                    break;  // Truncated: keep what was complete.
                const auto* body = d + i + 8;
                if (memcmp (d + i, "name", 4) == 0)
                {
                    juce::String name;
                    for (size_t c = 0; c < chunkSize && body[c] != 0; ++c)
                        name += (body[c] >= 32 && body[c] < 127) ? (juce::juce_wchar) body[c] : (juce::juce_wchar) ' ';
                    out.name = name.trim();
                }
                else if (memcmp (d + i, "obj ", 4) == 0 && chunkSize >= 4)
                {
                    Object object;
                    object.type = body[0] - 1;
                    object.alias = body[1];
                    object.data.assign (body + 4, body + chunkSize);
                    if (object.data.size() == expectedSize (object.type))
                        out.objects.push_back (std::move (object));
                }
                i += 8 + chunkSize;
            }
            return juce::Result::ok();
        }

        juce::Result parseSysEx (const uint8_t* d, size_t size, Bundle& out)
        {
            static const uint8_t header[] = { 0xf0, 0x00, 0x21, 0x02, 0x00, 0x04 };
            int messages = 0, bad = 0;
            for (size_t i = 0; i < size; ++i)
            {
                if (d[i] != 0xf0)
                    continue;
                size_t end = i + 1;
                while (end < size && d[end] != 0xf7)
                    ++end;
                if (end >= size)
                    break;
                const size_t length = end - i + 1;
                if (length >= 11 && memcmp (d + i, header, sizeof (header)) == 0)
                {
                    const int command = d[i + 6];
                    const int argument = d[i + 7];
                    const int type = command - 1;
                    const size_t objectSize = expectedSize (type);
                    // Nibbles for the data and the checksum, then F7.
                    if (command >= 1 && command <= 5 && objectSize > 0 && length == 8 + (objectSize + 1) * 2 + 1)
                    {
                        Object object;
                        object.type = type;
                        object.alias = argument;
                        uint8_t checksum = 0;
                        for (size_t b = 0; b < objectSize; ++b)
                        {
                            const auto byte = (uint8_t) (((d[i + 8 + b * 2] & 0x0f) << 4) | (d[i + 9 + b * 2] & 0x0f));
                            object.data.push_back (byte);
                            checksum = (uint8_t) (checksum + byte);
                        }
                        const auto expected = (uint8_t) (((d[i + 8 + objectSize * 2] & 0x0f) << 4)
                                                         | (d[i + 9 + objectSize * 2] & 0x0f));
                        if (expected == checksum)
                        {
                            out.objects.push_back (std::move (object));
                            ++messages;
                        }
                        else
                            ++bad;
                    }
                }
                i = end;
            }
            if (messages == 0)
                return juce::Result::fail (bad > 0 ? "the SysEx data failed its checksum"
                                                   : "no Ambika patch, part or multi dump found");
            return juce::Result::ok();
        }
    }

    const Object* Bundle::find (int type, int alias) const
    {
        for (const auto& o : objects)
            if (o.type == type && o.alias == alias)
                return &o;
        return nullptr;
    }

    bool Bundle::isMulti() const
    {
        for (const auto& o : objects)
            if (o.type == kMulti || (o.type == kPatch && o.alias > 0))
                return true;
        return false;
    }

    bool Bundle::hasPatch() const
    {
        for (const auto& o : objects)
            if (o.type == kPatch)
                return true;
        return false;
    }

    juce::Result parse (const juce::MemoryBlock& block, const juce::String& fallbackName, Bundle& out)
    {
        out = {};
        const auto* d = static_cast<const uint8_t*> (block.getData());
        const auto size = block.getSize();
        juce::Result result = juce::Result::fail ("empty file");
        if (size >= 4 && memcmp (d, "RIFF", 4) == 0)
            result = parseRiff (d, size, out);
        else if (size >= 1 && d[0] == 0xf0)
            result = parseSysEx (d, size, out);
        else if (size > 0)
            result = juce::Result::fail ("not an Ambika file or SysEx dump");
        if (result.wasOk() && out.objects.empty())
            result = juce::Result::fail ("the file contains no Ambika data eureka! can read");
        if (out.name.isEmpty())
            out.name = fallbackName;
        return result;
    }

    juce::Result parseFile (const juce::File& file, Bundle& out)
    {
        juce::MemoryBlock data;
        if (! file.loadFileAsData (data))
            return juce::Result::fail ("could not read " + file.getFileName());
        return parse (data, file.getFileNameWithoutExtension(), out);
    }

    std::vector<int> toEngineValues (const uint8_t* patch, const uint8_t* part)
    {
        using namespace ambika::plugin;
        std::vector<int> values;
        for (int i = 0; i < NumParameters(); ++i)
        {
            const auto& info = GetParameter (i);
            int value = info.default_value;
            if (info.target == TARGET_PATCH && patch != nullptr && info.address < kPatchSize)
            {
                const uint8_t byte = patch[info.address];
                value = info.min_value < 0 ? (int) (int8_t) byte : (int) byte;
            }
            else if (info.target == TARGET_PART && part != nullptr)
            {
                // PartData (controller/part.h): volume, octave, tuning, spread,
                // raga, legato, portamento, ... , polyphony mode at 15.
                switch (info.address)
                {
                    case PART_VOLUME: value = part[0]; break;
                    case PART_OCTAVE: value = (int8_t) part[1]; break;
                    case PART_TUNING: value = (int8_t) part[2]; break;
                    case PART_SPREAD: value = part[3]; break;
                    case PART_LEGATO: value = part[5]; break;
                    case PART_PORTAMENTO: value = part[6]; break;
                    case PART_POLYPHONY_MODE:
                        // MONO, POLY, UNISON_2X, CYCLIC; CHAIN (4) has no
                        // meaning in a plug-in: poly.
                        value = part[15] <= 3 ? part[15] : 1;
                        break;
                    default: break;  // Pan: not in Ambika parts.
                }
            }
            values.push_back (juce::jlimit<int> (info.min_value, info.max_value, value));
        }
        return values;
    }
}
