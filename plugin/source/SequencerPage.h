#pragma once

#include "PluginEditor.h"

namespace ambika_plugin
{
    // Reads and writes the edited part's engine parameters by engine ID
    // (e.g. "seq1_step3"), with host gestures around edits.
    class PartValues
    {
    public:
        explicit PartValues (APVTS& s) : state (s) {}
        void bindPart (int newPart) { endGestures(); currentPart = newPart; }
        int part() const { return currentPart; }

        int get (const juce::String& engineId) const;
        // Starts a gesture on the parameter the first time a drag touches it;
        // endGestures() (on mouse up) ends them.
        void set (const juce::String& engineId, int value);
        void endGestures();

    private:
        juce::RangedAudioParameter* parameter (const juce::String& engineId) const;

        APVTS& state;
        int currentPart = 0;
        juce::StringArray touched;
    };

    // One modulation sequence (Seq 1 or Seq 2): 16 bars, 0..255, drawn with
    // the mouse.
    class StepSequenceEditor final : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        StepSequenceEditor (APVTS&, AmbikaAudioProcessor&, int sequence);
        void bindPart (int part);
        void poll();
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;

    private:
        juce::Rectangle<float> barArea() const;
        void editAt (juce::Point<float> position);

        PartValues values;
        AmbikaAudioProcessor& processor;
        int sequence;
        std::array<int, ambika::plugin::kNumSteps> shown {};
        int shownLength = 0, shownStep = -1;
        juce::Point<float> lastDrag;
    };

    // The note pattern: per step a gate, a note (relative to the key held,
    // 60 = the key), a velocity and a tie into the next step.
    class NotePatternEditor final : public juce::Component, public juce::SettableTooltipClient
    {
    public:
        NotePatternEditor (APVTS&, AmbikaAudioProcessor&);
        void bindPart (int part);
        void poll();
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void mouseDoubleClick (const juce::MouseEvent&) override;
        void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    private:
        enum Row { kGateRow, kNoteRow, kVelocityRow, kTieRow, kNumRows };
        juce::Rectangle<float> cell (int row, int step) const;
        juce::Rectangle<float> rowArea (int row) const;
        int stepAt (float x) const;
        int rowAt (float y) const;
        juce::String id (Row row, int step) const;

        PartValues values;
        AmbikaAudioProcessor& processor;
        std::array<std::array<int, ambika::plugin::kNumSteps>, kNumRows> shown {};
        int shownLength = 0, shownStep = -1;
        // Drag state.
        int dragRow = -1, dragStep = -1, dragStartValue = 0, toggleTo = 0;
        float dragStartY = 0.0f;
    };

    // Arpeggiator settings, the two modulation sequences and the note pattern.
    class SequencerPage final : public Page
    {
    public:
        SequencerPage (APVTS&, AmbikaAudioProcessor&);
        void resized() override;
        void bindPart (int part) override;
        void poll() override;

    private:
        void paintArp (juce::Graphics&);
        void paintInfo (juce::Graphics&);

        AmbikaAudioProcessor& processor;
        Panel* arp = nullptr;
        Panel* info = nullptr;
        std::array<Panel*, 2> sequencePanels {};
        Panel* pattern = nullptr;
        ParameterControl *mode = nullptr, *direction = nullptr, *octaves = nullptr, *rhythm = nullptr,
                         *division = nullptr;
        std::array<ParameterControl*, 3> lengths {};
        std::array<std::unique_ptr<StepSequenceEditor>, 2> sequenceEditors;
        std::unique_ptr<NotePatternEditor> patternEditor;
        std::unique_ptr<ParameterReader> arpReader;
        juce::Rectangle<int> rhythmArea;
        int part = 0, shownArpStep = -1;
    };
}
