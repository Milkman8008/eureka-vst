#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <array>
#include <atomic>
#include <memory>
#include <vector>

#include "PluginParameters.h"
#include "PresetManager.h"
#include "engine.h"

namespace ambika_plugin
{
    class AmbikaAudioProcessor final : public juce::AudioProcessor
    {
    public:
        AmbikaAudioProcessor();
        ~AmbikaAudioProcessor() override = default;

        void prepareToPlay (double sampleRate, int samplesPerBlock) override;
        void releaseResources() override {}
        bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
        void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
        using AudioProcessor::processBlock;

        juce::AudioProcessorEditor* createEditor() override;
        bool hasEditor() const override { return true; }

        const juce::String getName() const override { return JucePlugin_Name; }
        bool acceptsMidi() const override { return true; }
        bool producesMidi() const override { return false; }
        bool isMidiEffect() const override { return false; }
        double getTailLengthSeconds() const override { return 0.0; }

        int getNumPrograms() override { return 1; }
        int getCurrentProgram() override { return 0; }
        void setCurrentProgram (int) override {}
        const juce::String getProgramName (int) override { return {}; }
        void changeProgramName (int, const juce::String&) override {}

        void getStateInformation (juce::MemoryBlock& destData) override;
        void setStateInformation (const void* data, int sizeInBytes) override;

        juce::AudioProcessorValueTreeState& parameters() { return state; }
        PresetManager& presets() { return presetManager; }

        // For the editor: activity (VCA level 0..255) and owner part of each voice.
        uint8_t voiceLevel (int voice) const { return voiceLevels[(size_t) voice].load (std::memory_order_relaxed); }
        uint8_t voiceOwner (int voice) const { return voiceOwners[(size_t) voice].load (std::memory_order_relaxed); }
        // Whether the host has enabled a voice's own output bus, and whether
        // the voice is playing on it (set to "own output" and enabled).
        bool voiceBusEnabled (int voice) const { return (voiceBusMask.load (std::memory_order_relaxed) >> voice) & 1; }
        bool voiceDirect (int voice) const { return (voiceDirectMask.load (std::memory_order_relaxed) >> voice) & 1; }

        // Where each part's sequencers are, for the editor: sequence 0..2
        // (Seq 1, Seq 2, pattern) or 3 (arpeggiator rhythm); -1 = not started.
        int currentStep (int part, int sequence) const
        {
            const auto step = currentSteps[(size_t) (part * 4 + sequence)].load (std::memory_order_relaxed);
            return step == 0xff ? -1 : (int) step;
        }

        // Output buses: 0 = main mix, 1..6 = voice 1..6.
        static constexpr int kVoiceBusOffset = 1;

        // Editor window scale (1 = design size), saved with the project; 0
        // until the editor has been opened in this project.
        float editorScale() const { return editorScaleValue.load(); }
        void setEditorScale (float scale) { editorScaleValue = scale; }

        // The part shown in the editor; saved with the project.
        int editedPart() const { return editedPartIndex.load(); }
        void setEditedPart (int part) { editedPartIndex = juce::jlimit (0, kNumParts - 1, part); }

    private:
        static BusesProperties makeBuses();
        void applyParameters();
        void handleMidi (const juce::MidiMessage& message);

        juce::AudioProcessorValueTreeState state;
        PresetManager presetManager { state };
        // Engine holds ~20 KB of voice state; keep it off the object.
        std::unique_ptr<ambika::plugin::Engine> engine;

        // Engine parameters per part, in engine table order.
        std::array<std::vector<std::atomic<float>*>, kNumParts> rawParameters;
        std::array<std::vector<int>, kNumParts> appliedValues;
        std::array<std::atomic<float>*, kNumVoices> rawVoiceParts {};
        std::array<std::atomic<float>*, kNumVoices> rawVoiceOutputs {};
        std::vector<float> voiceScratch;  // Direct outputs the host gave no room for.
        std::atomic<int> voiceBusMask { 0 }, voiceDirectMask { 0 };
        std::array<int, kNumVoices> appliedVoiceParts {};
        std::atomic<float>* outputGain = nullptr;
        std::array<std::array<std::atomic<float>*, 2>, kNumParts> rawKeyRange {};
        std::array<std::array<int, 2>, kNumParts> appliedKeyRange {};
        std::atomic<float>* voiceMode = nullptr;
        std::atomic<float>* unison = nullptr;
        int appliedVoiceMode = -1, appliedUnison = -1;
        juce::SmoothedValue<float> gainSmoothed;

        std::vector<float> left, right;
        std::array<std::atomic<uint8_t>, kNumVoices> voiceLevels {};
        std::array<std::atomic<uint8_t>, kNumVoices> voiceOwners {};
        std::atomic<int> editedPartIndex { 0 };
        std::atomic<float> editorScaleValue { 0.0f };
        std::array<std::atomic<uint8_t>, kNumParts * 4> currentSteps {};

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AmbikaAudioProcessor)
    };
}
