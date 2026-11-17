#include "Controls.h"

#include "EurekaLookAndFeel.h"
#include "Skin.h"
#include "preview.h"
#include "surge_filter.h"

#include <climits>
#include <cmath>

namespace ambika_plugin
{
    const ambika::plugin::ParameterInfo* findParameterInfo (const juce::String& engineId)
    {
        for (int i = 0; i < ambika::plugin::NumParameters(); ++i)
            if (engineId == ambika::plugin::GetParameter (i).id)
                return &ambika::plugin::GetParameter (i);
        return nullptr;
    }

    namespace
    {
        juce::String formatSeconds (double seconds)
        {
            if (seconds < 1.0)
                return juce::String (juce::roundToInt (seconds * 1000.0)) + " ms";
            return juce::String (seconds, seconds < 10.0 ? 2 : 1) + " s";
        }

        juce::String formatHz (double hz)
        {
            if (hz >= 1000.0)
                return juce::String (hz / 1000.0, 1) + " kHz";
            if (hz >= 100.0)
                return juce::String (juce::roundToInt (hz)) + " Hz";  // String(double, 0) does not round.
            return juce::String (hz, hz < 10.0 ? 2 : 1) + " Hz";
        }
    }

    //==============================================================================
    ParameterControl::ParameterControl (APVTS& s, juce::String id, juce::String text, ControlStyle st, bool scoped)
        : state (s), parameterId (std::move (id)), style (st), partScoped (scoped)
    {
        label.setText (text, juce::dontSendNotification);
        label.setJustificationType (juce::Justification::centred);
        label.setColour (juce::Label::textColourId, Palette::dimText);
        label.setFont (juce::Font (juce::FontOptions (11.5f)));
        label.setInterceptsMouseClicks (false, false);
        if (style != ControlStyle::Bar)
            addAndMakeVisible (label);

        if (style == ControlStyle::Combo)
        {
            combo = std::make_unique<juce::ComboBox>();
            addAndMakeVisible (*combo);
        }
        else
        {
            const bool bar = style == ControlStyle::Bar;
            slider = std::make_unique<juce::Slider> (
                bar ? juce::Slider::LinearHorizontal : juce::Slider::RotaryHorizontalVerticalDrag,
                bar ? juce::Slider::TextBoxRight : juce::Slider::TextBoxBelow);
            slider->setTextBoxStyle (bar ? juce::Slider::TextBoxRight : juce::Slider::TextBoxBelow, false,
                                     bar ? 48 : 64, 16);
            slider->setRotaryParameters (juce::degreesToRadians (225.0f), juce::degreesToRadians (495.0f), true);
            slider->setScrollWheelEnabled (true);
            addAndMakeVisible (*slider);
        }
        attach (partScoped ? partParameterId (0, parameterId) : parameterId);
    }

    void ParameterControl::attach (const juce::String& fullId)
    {
        sliderAttachment.reset();
        comboAttachment.reset();
        auto* parameter = state.getParameter (fullId);
        jassert (parameter != nullptr);
        if (combo != nullptr)
        {
            combo->clear (juce::dontSendNotification);
            if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (parameter))
                combo->addItemList (choice->choices, 1);
            comboAttachment = std::make_unique<APVTS::ComboBoxAttachment> (state, fullId, *combo);
        }
        else
        {
            sliderAttachment = std::make_unique<APVTS::SliderAttachment> (state, fullId, *slider);
            // Double-click returns to the default value.
            slider->setDoubleClickReturnValue (true, parameter->convertFrom0to1 (parameter->getDefaultValue()));
        }
    }

    void ParameterControl::bindPart (int part)
    {
        if (partScoped)
            attach (partParameterId (part, parameterId));
    }

    void ParameterControl::resized()
    {
        auto area = getLocalBounds();
        switch (style)
        {
            case ControlStyle::Bar:
                slider->setBounds (area);
                break;
            case ControlStyle::Combo:
                if (label.getText().isNotEmpty())
                    label.setBounds (area.removeFromTop (16));
                combo->setBounds (area.withSizeKeepingCentre (area.getWidth(), juce::jmin (24, area.getHeight())));
                break;
            case ControlStyle::Knob:
                label.setBounds (area.removeFromTop (15));
                slider->setBounds (area);
                break;
        }
    }

    //==============================================================================
    ParameterReader::ParameterReader (APVTS& s, std::vector<juce::String> engineIds, bool scoped)
        : state (s), ids (std::move (engineIds)), partScoped (scoped)
    {
        for (const auto& id : ids)
            infos.push_back (partScoped ? findParameterInfo (id) : nullptr);
        values.assign (ids.size(), INT_MIN);
        bindPart (0);
    }

    void ParameterReader::bindPart (int part)
    {
        raw.clear();
        for (const auto& id : ids)
        {
            raw.push_back (state.getRawParameterValue (partScoped ? partParameterId (part, id) : id));
            jassert (raw.back() != nullptr);
        }
        std::fill (values.begin(), values.end(), INT_MIN);
        poll();
    }

    bool ParameterReader::poll()
    {
        bool changed = false;
        for (size_t i = 0; i < raw.size(); ++i)
        {
            const float r = raw[i]->load (std::memory_order_relaxed);
            const int v = infos[i] != nullptr ? toEngineValue (*infos[i], r) : juce::roundToInt (r);
            if (v != values[i])
            {
                values[i] = v;
                changed = true;
            }
        }
        return changed;
    }

    //==============================================================================
    void Display::paintFrame (juce::Graphics& g) const
    {
        g.setColour (Palette::background.withMultipliedAlpha (currentSkin().settings.displayOpacity));
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 5.0f);
    }

    OscillatorDisplay::OscillatorDisplay (APVTS& state, int oscillator)
        : Display (state, { "osc" + juce::String (oscillator) + "_shape",
                            "osc" + juce::String (oscillator) + "_parameter",
                            "osc" + juce::String (oscillator) + "_range" })
    {
    }

    void OscillatorDisplay::paint (juce::Graphics& g)
    {
        paintFrame (g);
        const auto area = getLocalBounds().toFloat().reduced (6.0f, 8.0f);
        g.setColour (Palette::outline);
        g.drawHorizontalLine (juce::roundToInt (area.getCentreY()), area.getX(), area.getRight());

        constexpr int numSamples = 160;
        uint8_t samples[numSamples];
        ambika::plugin::RenderOscillatorPreview ((uint8_t) reader.value (0), (uint8_t) reader.value (1),
                                                 (int8_t) reader.value (2), samples, numSamples);
        juce::Path wave;
        for (int i = 0; i < numSamples; ++i)
        {
            const float x = area.getX() + area.getWidth() * (float) i / (float) (numSamples - 1);
            const float y = area.getBottom() - area.getHeight() * (float) samples[i] / 255.0f;
            if (i == 0)
                wave.startNewSubPath (x, y);
            else
                wave.lineTo (x, y);
        }
        g.setColour (accent());
        g.strokePath (wave, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved));
    }

    FilterDisplay::FilterDisplay (APVTS& state)
        : Display (state, { "filter_cutoff", "filter_resonance", "filter_mode", "filter_model", "filter_variant" })
    {
    }

    FilterVariantControl::FilterVariantControl (APVTS& s)
        : Display (s, { "filter_model", "filter_variant" }), state (s)
    {
        label.setText ("Variant", juce::dontSendNotification);
        label.setJustificationType (juce::Justification::centred);
        label.setColour (juce::Label::textColourId, Palette::dimText);
        label.setFont (juce::Font (juce::FontOptions (11.5f)));
        addAndMakeVisible (label);
        addAndMakeVisible (combo);
        combo.onChange = [this]
        {
            const int index = combo.getSelectedId() - 1;
            if (index < 0 || index == reader.value (1))
                return;
            if (auto* parameter = state.getParameter (partParameterId (part, juce::String ("filter_variant"))))
            {
                parameter->beginChangeGesture();
                parameter->setValueNotifyingHost (parameter->convertTo0to1 ((float) index));
                parameter->endChangeGesture();
            }
        };
        refresh();
    }

    void FilterVariantControl::refresh()
    {
        reader.poll();
        const int model = reader.value (0);
        const int variants = model > 0 ? ambika::plugin::SurgeFilter::NumSubtypes (model) : 0;
        if (model != shownModel)
        {
            shownModel = model;
            combo.clear (juce::dontSendNotification);
            for (int i = 0; i < variants; ++i)
                combo.addItem (ambika::plugin::SurgeFilter::SubtypeName (model, i), i + 1);
            if (variants == 0)
                combo.addItem ("-", 1);
            combo.setEnabled (variants > 1);
        }
        const int selected = juce::jlimit (0, juce::jmax (0, variants - 1), reader.value (1));
        combo.setSelectedId (selected + 1, juce::dontSendNotification);
    }

    void FilterVariantControl::resized()
    {
        auto area = getLocalBounds();
        label.setBounds (area.removeFromTop (16));
        combo.setBounds (area.withSizeKeepingCentre (area.getWidth(), juce::jmin (24, area.getHeight())));
    }

    void FilterDisplay::paint (juce::Graphics& g)
    {
        paintFrame (g);
        const auto area = getLocalBounds().toFloat().reduced (6.0f, 6.0f);
        const double minHz = 20.0, maxHz = 20000.0;
        const float topDb = 18.0f, bottomDb = -36.0f;
        auto xForHz = [&] (double hz)
        {
            return area.getX() + area.getWidth() * (float) (std::log (hz / minHz) / std::log (maxHz / minHz));
        };
        auto yForDb = [&] (float db)
        {
            return juce::jmap (juce::jlimit (bottomDb, topDb, db), topDb, bottomDb, area.getY(), area.getBottom());
        };

        g.setColour (Palette::outline.withAlpha (0.7f));
        for (double hz : { 100.0, 1000.0, 10000.0 })
            g.drawVerticalLine (juce::roundToInt (xForHz (hz)), area.getY(), area.getBottom());
        g.drawHorizontalLine (juce::roundToInt (yForDb (0.0f)), area.getX(), area.getRight());

        const auto cutoff = (uint8_t) reader.value (0);
        const auto resonance = (uint8_t) reader.value (1);
        const auto mode = (uint8_t) reader.value (2);
        const int model = reader.value (3), variant = reader.value (4);
        const double cv = juce::jlimit (0.0, 255.0, cutoff * 2.0 - 8.0);

        // Surge models: measure the real filter's response at a set of
        // frequencies (cached), then interpolate.
        constexpr int kSurgePoints = 120;
        if (model > 0)
        {
            const std::array<int, 5> key { cutoff, resonance, mode, model, variant };
            if (key != surgeCurveKey)
            {
                surgeCurveKey = key;
                std::vector<double> frequencies (kSurgePoints);
                for (int i = 0; i < kSurgePoints; ++i)
                    frequencies[(size_t) i] = minHz * std::pow (maxHz / minHz, (double) i / (kSurgePoints - 1));
                surgeCurve.assign (kSurgePoints, 0.0);
                ambika::plugin::SurgeFilter::MeasureResponse (
                    model, variant, ambika::plugin::AnalogVoice::CutoffToHz (cv), (float) (resonance * 4.0 / 255.0),
                    ambika::plugin::Engine::kNativeSampleRate * ambika::plugin::Engine::kOversampling,
                    frequencies.data(), surgeCurve.data(), kSurgePoints);
            }
        }

        juce::Path curve, fill;
        const int steps = juce::jmax (2, (int) area.getWidth());
        for (int i = 0; i <= steps; ++i)
        {
            const double t = (double) i / steps;
            const double hz = minHz * std::pow (maxHz / minHz, t);
            double magnitude;
            if (model > 0)
            {
                const double position = t * (kSurgePoints - 1);
                const int index = juce::jlimit (0, kSurgePoints - 2, (int) position);
                const double frac = position - index;
                magnitude = surgeCurve[(size_t) index] * (1.0 - frac) + surgeCurve[(size_t) index + 1] * frac;
            }
            else
                magnitude = ambika::plugin::FilterResponse (cutoff, resonance, mode, hz);
            const float db = (float) juce::Decibels::gainToDecibels (magnitude, -100.0);
            const float x = xForHz (hz), y = yForDb (db);
            if (i == 0)
            {
                curve.startNewSubPath (x, y);
                fill.startNewSubPath (x, area.getBottom());
            }
            else
                curve.lineTo (x, y);
            fill.lineTo (x, y);
        }
        fill.lineTo (area.getRight(), area.getBottom());
        fill.closeSubPath();
        g.setGradientFill (juce::ColourGradient (accent().withAlpha (0.28f), 0.0f, area.getY(),
                                                 accent().withAlpha (0.02f), 0.0f, area.getBottom(), false));
        g.fillPath (fill);
        g.setColour (accent());
        g.strokePath (curve, juce::PathStrokeType (1.8f));

        g.setColour (Palette::dimText);
        g.setFont (juce::Font (juce::FontOptions (11.0f)));
        g.drawText (formatHz (juce::jmin (20000.0, ambika::plugin::AnalogVoice::CutoffToHz (cv))) + " at C4",
                    area.reduced (4.0f, 2.0f), juce::Justification::topRight);
    }

    EnvelopeDisplay::EnvelopeDisplay (APVTS& state, int envelope)
        : Display (state, { "env" + juce::String (envelope) + "_attack", "env" + juce::String (envelope) + "_decay",
                            "env" + juce::String (envelope) + "_sustain",
                            "env" + juce::String (envelope) + "_release" })
    {
    }

    void EnvelopeDisplay::paint (juce::Graphics& g)
    {
        paintFrame (g);
        auto area = getLocalBounds().toFloat().reduced (8.0f, 8.0f);
        const auto textArea = area.removeFromBottom (13.0f);

        const double attack = ambika::plugin::EnvelopeSegmentSeconds ((uint8_t) reader.value (0));
        const double decay = ambika::plugin::EnvelopeSegmentSeconds ((uint8_t) reader.value (1));
        const double release = ambika::plugin::EnvelopeSegmentSeconds ((uint8_t) reader.value (3));
        const float sustain = (float) reader.value (2) / 127.0f;

        // Square-root time axis: short and long envelopes both stay readable.
        const double wa = std::sqrt (attack), wd = std::sqrt (decay), wr = std::sqrt (release);
        const float holdWidth = area.getWidth() * 0.18f;
        const float scale = (area.getWidth() - holdWidth) / (float) juce::jmax (wa + wd + wr, 2.4);
        const float x0 = area.getX();
        const float xa = x0 + (float) wa * scale;
        const float xd = xa + (float) wd * scale;
        const float xs = xd + holdWidth;
        const float xr = xs + (float) wr * scale;
        auto y = [&] (float level) { return area.getBottom() - level * area.getHeight(); };

        // Segments follow an exponential-looking curve, like the firmware's
        // wav_res_env_expo interpolation.
        juce::Path path;
        path.startNewSubPath (x0, y (0.0f));
        auto segment = [&] (float fromX, float toX, float fromLevel, float toLevel)
        {
            constexpr int steps = 24;
            for (int i = 1; i <= steps; ++i)
            {
                const float t = (float) i / steps;
                const float shaped = (1.0f - std::exp (-4.0f * t)) / (1.0f - std::exp (-4.0f));
                path.lineTo (fromX + (toX - fromX) * t, y (fromLevel + (toLevel - fromLevel) * shaped));
            }
        };
        segment (x0, xa, 0.0f, 1.0f);
        segment (xa, xd, 1.0f, sustain);
        path.lineTo (xs, y (sustain));
        segment (xs, xr, sustain, 0.0f);

        juce::Path fill (path);
        fill.lineTo (xr, area.getBottom());
        fill.closeSubPath();
        g.setColour (accent().withAlpha (0.15f));
        g.fillPath (fill);
        g.setColour (accent());
        g.strokePath (path, juce::PathStrokeType (1.8f, juce::PathStrokeType::curved));

        g.setColour (Palette::dimText);
        g.setFont (juce::Font (juce::FontOptions (10.5f)));
        g.drawText ("A " + formatSeconds (attack) + "   D " + formatSeconds (decay) + "   R " + formatSeconds (release),
                    textArea, juce::Justification::centredLeft);
    }

    LfoDisplay::LfoDisplay (APVTS& state, juce::String shapeId, juce::String rateId, bool synced)
        : Display (state, { std::move (shapeId), std::move (rateId) }), canSync (synced)
    {
    }

    void LfoDisplay::paint (juce::Graphics& g)
    {
        paintFrame (g);
        auto area = getLocalBounds().toFloat().reduced (6.0f, 6.0f);
        const auto labelArea = area.removeFromRight (64.0f);

        constexpr int numSamples = 128;
        uint8_t samples[numSamples];
        ambika::plugin::RenderLfoPreview ((uint8_t) reader.value (0), samples, numSamples);
        juce::Path wave;
        for (int i = 0; i < numSamples; ++i)
        {
            const float x = area.getX() + area.getWidth() * (float) i / (float) (numSamples - 1);
            const float yy = area.getBottom() - area.getHeight() * (float) samples[i] / 255.0f;
            if (i == 0)
                wave.startNewSubPath (x, yy);
            else
                wave.lineTo (x, yy);
        }
        g.setColour (accent());
        g.strokePath (wave, juce::PathStrokeType (1.5f));

        static const char* const kSynced[] = { "1/1", "3/4", "2/3", "1/2", "3/8", "1/3", "1/4", "1/6",
                                               "1/8", "1/12", "1/16", "1/24", "1/32", "1/48", "1/96" };
        const int rate = reader.value (1);
        juce::String text;
        if (canSync && rate < ambika::kNumSyncedLfoRates)
            text = juce::String (kSynced[rate]) + " sync";
        else
            text = formatHz (ambika::plugin::LfoRateHz ((uint8_t) (canSync ? rate - ambika::kNumSyncedLfoRates : rate)));
        g.setColour (Palette::dimText);
        g.setFont (juce::Font (juce::FontOptions (11.0f)));
        g.drawText (text, labelArea, juce::Justification::centredRight);
    }

    KeyRangeDisplay::KeyRangeDisplay (APVTS& state, int p)
        : Display (state, { partKeyLowId (p), partKeyHighId (p) }, false), part (p)
    {
    }

    void KeyRangeDisplay::paint (juce::Graphics& g)
    {
        paintFrame (g);
        const auto area = getLocalBounds().toFloat().reduced (4.0f, 6.0f);
        const float keyWidth = area.getWidth() / 128.0f;
        // Octave bands, alternating, to give an idea of position.
        for (int octave = 0; octave < 11; ++octave)
        {
            g.setColour (octave % 2 == 0 ? Palette::panelRaised : Palette::panel);
            g.fillRect (area.getX() + (float) (octave * 12) * keyWidth, area.getY(),
                        (float) juce::jmin (12, 128 - octave * 12) * keyWidth, area.getHeight());
        }
        const int low = reader.value (0), high = reader.value (1);
        auto highlight = [&] (int from, int to)
        {
            g.setColour (Palette::part (part).withAlpha (0.85f));
            g.fillRect (area.getX() + (float) from * keyWidth, area.getY() + area.getHeight() * 0.35f,
                        (float) (to - from + 1) * keyWidth, area.getHeight() * 0.3f);
        };
        if (low <= high)
            highlight (low, high);
        else
        {
            // The firmware treats an inverted range as "outside low..high".
            highlight (0, high);
            highlight (low, 127);
        }
        // C markers.
        g.setColour (Palette::faintText);
        for (int c = 0; c < 128; c += 12)
            g.fillRect (area.getX() + (float) c * keyWidth, area.getBottom() - 3.0f, 1.0f, 3.0f);
    }

    //==============================================================================
    void Panel::paint (juce::Graphics& g)
    {
        const auto area = getLocalBounds().toFloat().reduced (0.5f);
        const float opacity = currentSkin().settings.panelOpacity;
        g.setColour (Palette::panel.withMultipliedAlpha (opacity));
        g.fillRoundedRectangle (area, 8.0f);
        g.setColour (Palette::outline.withMultipliedAlpha (opacity));
        g.drawRoundedRectangle (area, 8.0f, 1.0f);
        g.setColour (findColour (juce::Slider::rotarySliderFillColourId));
        g.fillEllipse (12.0f, 11.0f, 6.0f, 6.0f);
        g.setColour (Palette::text);
        g.setFont (juce::Font (juce::FontOptions (12.0f, juce::Font::bold)).withExtraKerningFactor (0.08f));
        g.drawText (title.toUpperCase(), 24, 5, getWidth() - 34, 18, juce::Justification::centredLeft);
        if (onPaint)
            onPaint (g);
    }
}
