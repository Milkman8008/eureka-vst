#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <memory>
#include <vector>

#include "PluginParameters.h"

namespace ambika_plugin
{
    using APVTS = juce::AudioProcessorValueTreeState;

    enum class ControlStyle
    {
        Knob,   // rotary, label above, value below
        Combo,  // drop-down, label above
        Bar     // horizontal bar with value, no label
    };

    // A control bound to one host parameter. Part-scoped controls name an
    // engine parameter (e.g. "osc1_shape") and follow the edited part.
    class ParameterControl final : public juce::Component
    {
    public:
        ParameterControl (APVTS& state, juce::String parameterId, juce::String label, ControlStyle style,
                          bool partScoped = true);

        void bindPart (int part);
        void resized() override;

    private:
        void attach (const juce::String& fullId);

        APVTS& state;
        juce::String parameterId;
        ControlStyle style;
        bool partScoped;
        juce::Label label;
        std::unique_ptr<juce::Slider> slider;
        std::unique_ptr<juce::ComboBox> combo;
        std::unique_ptr<APVTS::SliderAttachment> sliderAttachment;
        std::unique_ptr<APVTS::ComboBoxAttachment> comboAttachment;
    };

    // Reads current engine values of a set of parameters, for displays.
    class ParameterReader
    {
    public:
        ParameterReader (APVTS& state, std::vector<juce::String> engineIds, bool partScoped = true);
        void bindPart (int part);
        // Returns true if any value changed since the last call.
        bool poll();
        int value (size_t index) const { return values[index]; }

    private:
        APVTS& state;
        std::vector<juce::String> ids;
        std::vector<const ambika::plugin::ParameterInfo*> infos;
        std::vector<std::atomic<float>*> raw;
        std::vector<int> values;
        bool partScoped;
    };

    // Base for live displays: polled by the editor, repaints on change.
    class Display : public juce::Component
    {
    public:
        Display (APVTS& state, std::vector<juce::String> ids, bool partScoped = true)
            : reader (state, std::move (ids), partScoped) {}
        void bindPart (int part) { reader.bindPart (part); repaint(); }
        void poll() { if (reader.poll()) repaint(); }

    protected:
        void paintFrame (juce::Graphics&) const;
        juce::Colour accent() const { return findColour (juce::Slider::rotarySliderFillColourId); }
        ParameterReader reader;
    };

    class OscillatorDisplay final : public Display
    {
    public:
        OscillatorDisplay (APVTS& state, int oscillator);
        void paint (juce::Graphics&) override;
    };

    class FilterDisplay final : public Display
    {
    public:
        explicit FilterDisplay (APVTS& state);
        void paint (juce::Graphics&) override;

    private:
        // Surge models: measured response, cached until the settings change.
        std::vector<double> surgeCurve;
        std::array<int, 5> surgeCurveKey { -1, -1, -1, -1, -1 };
    };

    // Variant menu for the filter: its entries are the subtypes of the
    // selected Surge model, so it is rebuilt when the model changes.
    class FilterVariantControl final : public Display
    {
    public:
        explicit FilterVariantControl (APVTS& state);
        void paint (juce::Graphics&) override {}
        void resized() override;
        void refresh();

    private:
        APVTS& state;
        int part = 0;
        int shownModel = -1;
        juce::Label label;
        juce::ComboBox combo;

    public:
        void bindVariantPart (int newPart) { part = newPart; shownModel = -1; bindPart (newPart); refresh(); }
    };

    class EnvelopeDisplay final : public Display
    {
    public:
        EnvelopeDisplay (APVTS& state, int envelope);
        void paint (juce::Graphics&) override;
    };

    class LfoDisplay final : public Display
    {
    public:
        LfoDisplay (APVTS& state, juce::String shapeId, juce::String rateId, bool synced);
        void paint (juce::Graphics&) override;

    private:
        bool canSync;
    };

    // A 128-key strip with the part's key range highlighted.
    class KeyRangeDisplay final : public Display
    {
    public:
        KeyRangeDisplay (APVTS& state, int part);
        void paint (juce::Graphics&) override;

    private:
        int part;
    };

    // A rounded panel with a title; children are laid out by the owner.
    class Panel : public juce::Component
    {
    public:
        explicit Panel (juce::String panelTitle) : title (std::move (panelTitle)) {}
        void paint (juce::Graphics&) override;
        juce::Rectangle<int> content() const { return getLocalBounds().reduced (10).withTrimmedTop (20); }

        // Extra drawing in panel coordinates, under the panel's controls.
        std::function<void (juce::Graphics&)> onPaint;

    private:
        juce::String title;
    };

    const ambika::plugin::ParameterInfo* findParameterInfo (const juce::String& engineId);
}
