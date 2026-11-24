#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "engine.h"
#include "parameters.h"

namespace ambika_plugin
{
    constexpr int kNumParts = ambika::plugin::kNumParts;
    constexpr int kNumVoices = ambika::plugin::kNumVoices;

    // The plug-in's own output gain.
    inline constexpr const char* kOutputGainId = "output_gain";

    // Host parameter ID of an engine parameter for a part (0-based).
    // Part 1 keeps the plain IDs used before multi-part mode existed, so
    // projects saved with earlier builds load unchanged; parts 2-6 use p2_...
    juce::String partParameterId (int part, const ambika::plugin::ParameterInfo& info);
    juce::String partParameterId (int part, const juce::String& engineId);

    // Multi parameters: the part each voice card plays, and how notes are
    // given to the cards.
    juce::String voicePartId (int voice);
    // Where each voice card is heard: 0 = the main output, 1 = its own
    // output bus ("Voice 1".."Voice 6"), if the host has that bus enabled.
    juce::String voiceOutputId (int voice);
    // Keyboard split per part (same IDs as earlier builds, so their key
    // ranges load again).
    juce::String partKeyLowId (int part);
    juce::String partKeyHighId (int part);
    inline constexpr const char* kVoiceModeId = "multi_voice_mode";
    inline constexpr const char* kUnisonId = "multi_unison";

    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // Converts a host parameter's raw value to the engine's integer value.
    int toEngineValue (const ambika::plugin::ParameterInfo& info, float rawValue);

    juce::String noteName (int note);
}
