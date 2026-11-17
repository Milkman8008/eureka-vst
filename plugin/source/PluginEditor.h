#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <functional>
#include <memory>
#include <vector>

#include "Controls.h"
#include "EurekaLookAndFeel.h"
#include "PluginProcessor.h"

namespace ambika_plugin
{
    // Common behaviour of the pages: controls and displays that follow the
    // edited part.
    class Page : public juce::Component
    {
    public:
        explicit Page (APVTS& s) : state (s) {}
        virtual void bindPart (int part);
        virtual void poll();

    protected:
        ParameterControl& control (juce::Component& parent, const juce::String& id, const juce::String& label,
                                   ControlStyle style, bool partScoped = true);
        template <typename DisplayType, typename... Args>
        DisplayType& display (juce::Component& parent, Args&&... args)
        {
            auto owned = std::make_unique<DisplayType> (std::forward<Args> (args)...);
            auto& ref = *owned;
            parent.addAndMakeVisible (ref);
            displays.push_back (std::move (owned));
            return ref;
        }
        Panel& panel (const juce::String& title);

        APVTS& state;
        std::vector<std::unique_ptr<ParameterControl>> controls;
        std::vector<std::unique_ptr<Display>> displays;
        std::vector<std::unique_ptr<Panel>> panels;
    };

    // Oscillators -> mixer -> filter -> voice, then envelopes and LFOs.
    class SynthPage final : public Page
    {
    public:
        explicit SynthPage (APVTS&);
        void resized() override;
        void paint (juce::Graphics&) override;
        void bindPart (int part) override;
        void poll() override;

    private:
        struct OscillatorPanel
        {
            Panel* panel;
            Display* display;
            ParameterControl *shape, *parameter, *range, *tune;
        };
        struct EnvelopePanel
        {
            Panel* panel;
            Display *envelope, *lfo;
            ParameterControl *attack, *decay, *sustain, *release, *shape, *rate, *sync;
        };
        OscillatorPanel oscillators[2] {};
        Panel* mixer = nullptr;
        std::vector<ParameterControl*> mixerControls;
        Panel* filter = nullptr;
        Display* filterDisplay = nullptr;
        ParameterControl* filterModel = nullptr;
        FilterVariantControl* filterVariant = nullptr;
        std::vector<ParameterControl*> filterControls;
        std::unique_ptr<ParameterReader> filterModelReader;
        Panel* voice = nullptr;
        std::vector<ParameterControl*> voiceControls;
        EnvelopePanel envelopes[3] {};
        Panel* voiceLfo = nullptr;
        Display* voiceLfoDisplay = nullptr;
        ParameterControl *voiceLfoShape = nullptr, *voiceLfoRate = nullptr;
    };

    class ModulationPage final : public Page
    {
    public:
        explicit ModulationPage (APVTS&);
        void resized() override;

    private:
        Panel* matrix = nullptr;
        Panel* modifiers = nullptr;
        std::vector<std::array<ParameterControl*, 3>> modulationRows;
        std::vector<std::array<ParameterControl*, 3>> modifierRows;
    };

    // The six voice cards: which part (patch) each one plays, and how notes
    // are given to the cards.
    class MultiPage final : public Page
    {
    public:
        MultiPage (APVTS&, AmbikaAudioProcessor&, std::function<void (int)> editPart);
        void resized() override;
        void poll() override;
        void bindPart (int) override {}  // Shows all voices at once.

    private:
        void paintCards (juce::Graphics&);
        void paintAllocation (juce::Graphics&);
        void paintSplits (juce::Graphics&);
        juce::Rectangle<int> cardBounds (int voice) const;

        AmbikaAudioProcessor& processor;
        Panel* cards = nullptr;
        Panel* allocation = nullptr;
        Panel* splits = nullptr;
        struct SplitRow
        {
            ParameterControl *low = nullptr, *high = nullptr;
            Display* range = nullptr;
        };
        std::array<SplitRow, kNumParts> splitRows;
        std::array<ParameterControl*, kNumVoices> owners {};
        std::array<std::unique_ptr<juce::TextButton>, kNumVoices> editButtons;
        ParameterControl* voiceMode = nullptr;
        ParameterControl* unison = nullptr;
        std::unique_ptr<ParameterReader> allocationReader;
        std::array<uint8_t, kNumVoices> shownLevels {};
        std::array<uint8_t, kNumVoices> shownOwners {};
        std::array<juce::String, kNumParts> shownNames;
    };

    class AmbikaAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                             private juce::Timer
    {
    public:
        explicit AmbikaAudioProcessorEditor (AmbikaAudioProcessor&);
        ~AmbikaAudioProcessorEditor() override;

        void resized() override;

        // Design size; the window scales this uniformly.
        static constexpr int designWidth = 1200;
        static constexpr int designHeight = 802;

        // For tools and tests.
        void showPage (int index);  // 0 synth, 1 mod, 2 multi, 3 presets
        void editPart (int part);
        void showSaveDialog();
        void toggleTheme();

    private:
        class Content;
        void timerCallback() override;
        void buildContent (int page);

        AmbikaAudioProcessor& audioProcessor;
        EurekaLookAndFeel lookAndFeel;
        std::unique_ptr<Content> content;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AmbikaAudioProcessorEditor)
    };
}
