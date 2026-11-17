#include "PluginParameters.h"

namespace ambika_plugin
{
    juce::String partParameterId (int part, const juce::String& engineId)
    {
        return part == 0 ? engineId : "p" + juce::String (part + 1) + "_" + engineId;
    }

    juce::String partParameterId (int part, const ambika::plugin::ParameterInfo& info)
    {
        return partParameterId (part, juce::String (info.id));
    }

    juce::String partKeyLowId (int part) { return "multi_part" + juce::String (part + 1) + "_key_low"; }
    juce::String partKeyHighId (int part) { return "multi_part" + juce::String (part + 1) + "_key_high"; }
    juce::String voicePartId (int voice) { return "multi_voice" + juce::String (voice + 1) + "_part"; }

    juce::String noteName (int note)
    {
        return juce::MidiMessage::getMidiNoteName (note, true, true, 4);
    }

    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
    {
        juce::AudioProcessorValueTreeState::ParameterLayout layout;

        for (int part = 0; part < kNumParts; ++part)
        {
            const auto partName = "Part " + juce::String (part + 1);
            const auto prefix = "P" + juce::String (part + 1) + " ";
            auto partGroup = std::make_unique<juce::AudioProcessorParameterGroup> (
                "part" + juce::String (part + 1), partName, "|");

            // Sub-groups as in the engine table (Oscillator 1, Mixer, ...).
            std::vector<std::unique_ptr<juce::AudioProcessorParameterGroup>> groups;
            auto groupFor = [&] (const juce::String& name) -> juce::AudioProcessorParameterGroup&
            {
                for (auto& group : groups)
                    if (group->getName() == name)
                        return *group;
                const auto id = "part" + juce::String (part + 1) + "_"
                                + name.toLowerCase().replaceCharacters (" /", "__");
                groups.push_back (std::make_unique<juce::AudioProcessorParameterGroup> (id, name, "|"));
                return *groups.back();
            };

            for (int i = 0; i < ambika::plugin::NumParameters(); ++i)
            {
                const auto& info = ambika::plugin::GetParameter (i);
                const juce::ParameterID id { partParameterId (part, info), 1 };
                const auto name = prefix + info.name;

                if (info.choices != nullptr)
                {
                    juce::StringArray choices;
                    for (int v = info.min_value; v <= info.max_value; ++v)
                        choices.add (info.choices[v - info.min_value]);
                    groupFor (info.group).addChild (std::make_unique<juce::AudioParameterChoice> (
                        id, name, choices, info.default_value - info.min_value));
                }
                else
                {
                    groupFor (info.group).addChild (std::make_unique<juce::AudioParameterInt> (
                        id, name, info.min_value, info.max_value, info.default_value));
                }
            }
            for (auto& group : groups)
                partGroup->addChild (std::move (group));
            layout.add (std::move (partGroup));
        }

        // Multi: the part each voice card plays (default: all on part 1, the
        // single-part behaviour) and how notes are given to the cards.
        auto multi = std::make_unique<juce::AudioProcessorParameterGroup> ("multi", "Multi", "|");
        juce::StringArray owners { "off" };
        for (int part = 1; part <= kNumParts; ++part)
            owners.add ("part " + juce::String (part));
        for (int voice = 0; voice < kNumVoices; ++voice)
            multi->addChild (std::make_unique<juce::AudioParameterChoice> (
                juce::ParameterID { voicePartId (voice), 1 }, "Voice " + juce::String (voice + 1) + " Part",
                owners, 1));
        const auto noteAttributes = juce::AudioParameterIntAttributes()
                                        .withStringFromValueFunction ([] (int v, int) { return noteName (v); })
                                        .withValueFromStringFunction ([] (const juce::String& text)
                                        {
                                            for (int n = 0; n < 128; ++n)
                                                if (noteName (n).equalsIgnoreCase (text.trim()))
                                                    return n;
                                            return text.getIntValue();
                                        });
        for (int part = 0; part < kNumParts; ++part)
        {
            const auto name = "Part " + juce::String (part + 1) + " ";
            multi->addChild (std::make_unique<juce::AudioParameterInt> (
                juce::ParameterID { partKeyLowId (part), 1 }, name + "Key Low", 0, 127, 0, noteAttributes));
            multi->addChild (std::make_unique<juce::AudioParameterInt> (
                juce::ParameterID { partKeyHighId (part), 1 }, name + "Key High", 0, 127, 127, noteAttributes));
        }
        multi->addChild (std::make_unique<juce::AudioParameterChoice> (
            juce::ParameterID { kVoiceModeId, 1 }, "Voice Mode",
            juce::StringArray { "per part", "cyclic", "poly" }, 2));
        multi->addChild (std::make_unique<juce::AudioParameterInt> (
            juce::ParameterID { kUnisonId, 1 }, "Unison", 1, kNumVoices, 1));
        layout.add (std::move (multi));

        layout.add (std::make_unique<juce::AudioParameterFloat> (
            juce::ParameterID { kOutputGainId, 1 }, "Output",
            juce::NormalisableRange<float> (-24.0f, 12.0f, 0.1f), 0.0f,
            juce::AudioParameterFloatAttributes().withLabel ("dB")));

        return layout;
    }

    int toEngineValue (const ambika::plugin::ParameterInfo& info, float rawValue)
    {
        // Choice parameters hold the index, int parameters the value itself.
        const int value = juce::roundToInt (rawValue)
                          + (info.choices != nullptr ? info.min_value : 0);
        return juce::jlimit<int> (info.min_value, info.max_value, value);
    }
}
