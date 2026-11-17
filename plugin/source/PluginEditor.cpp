#include "PluginEditor.h"

#include "PresetBrowser.h"
#include "Skin.h"

namespace ambika_plugin
{
    namespace
    {
        constexpr int kHeaderHeight = 64;
        constexpr int kPresetBarHeight = 42;
        constexpr int kPagesTop = kHeaderHeight + kPresetBarHeight;
        constexpr int kMargin = 12;

        juce::String n (int i) { return juce::String (i); }

        void layoutGrid (const std::vector<ParameterControl*>& items, juce::Rectangle<int> area, int columns,
                         int cellHeight)
        {
            const int cellWidth = area.getWidth() / columns;
            for (size_t i = 0; i < items.size(); ++i)
            {
                const int column = (int) i % columns, row = (int) i / columns;
                items[i]->setBounds (area.getX() + column * cellWidth, area.getY() + row * cellHeight,
                                     cellWidth, cellHeight);
            }
        }

        // Signal-flow arrow between two panels.
        void drawArrow (juce::Graphics& g, juce::Point<float> from, juce::Point<float> to, juce::Colour colour)
        {
            g.setColour (colour);
            g.drawLine ({ from, to.translated (-5.0f, 0.0f) }, 1.5f);
            juce::Path head;
            head.addTriangle (to.x, to.y, to.x - 7.0f, to.y - 4.5f, to.x - 7.0f, to.y + 4.5f);
            g.fillPath (head);
        }

        juce::Colour accentOf (const juce::Component& c)
        {
            return c.findColour (juce::Slider::rotarySliderFillColourId);
        }
    }

    //==============================================================================
    void Page::bindPart (int part)
    {
        for (auto& c : controls)
            c->bindPart (part);
        for (auto& d : displays)
            d->bindPart (part);
    }

    void Page::poll()
    {
        for (auto& d : displays)
            d->poll();
    }

    ParameterControl& Page::control (juce::Component& parent, const juce::String& id, const juce::String& label,
                                     ControlStyle style, bool partScoped)
    {
        controls.push_back (std::make_unique<ParameterControl> (state, id, label, style, partScoped));
        parent.addAndMakeVisible (*controls.back());
        return *controls.back();
    }

    Panel& Page::panel (const juce::String& title)
    {
        panels.push_back (std::make_unique<Panel> (title));
        addAndMakeVisible (*panels.back());
        return *panels.back();
    }

    //==============================================================================
    SynthPage::SynthPage (APVTS& s) : Page (s)
    {
        using CS = ControlStyle;
        for (int i = 0; i < 2; ++i)
        {
            const auto o = "osc" + n (i + 1);
            auto& p = panel ("Oscillator " + n (i + 1));
            oscillators[i] = { &p, &display<OscillatorDisplay> (p, state, i + 1),
                               &control (p, o + "_shape", "Waveform", CS::Combo),
                               &control (p, o + "_parameter", "Parameter", CS::Knob),
                               &control (p, o + "_range", "Range", CS::Knob),
                               &control (p, o + "_tune", "Tune", CS::Knob) };
        }

        mixer = &panel ("Mixer");
        mixerControls = { &control (*mixer, "mix_balance", "Osc 1 / 2", CS::Knob),
                          &control (*mixer, "mix_operator", "Crossmod", CS::Combo),
                          &control (*mixer, "mix_amount", "Amount", CS::Knob),
                          &control (*mixer, "mix_sub_shape", "Sub / click", CS::Combo),
                          &control (*mixer, "mix_sub_level", "Sub level", CS::Knob),
                          &control (*mixer, "mix_noise", "Noise", CS::Knob),
                          &control (*mixer, "mix_fuzz", "Fuzz", CS::Knob),
                          &control (*mixer, "mix_crush", "Crush", CS::Knob) };

        filter = &panel ("Filter");
        filterDisplay = &display<FilterDisplay> (*filter, state);
        filterModel = &control (*filter, "filter_model", "Model", CS::Combo);
        filterVariant = &display<FilterVariantControl> (*filter, state);
        filterModelReader = std::make_unique<ParameterReader> (state, std::vector<juce::String> { "filter_model" });
        filterControls = { &control (*filter, "filter_cutoff", "Frequency", CS::Knob),
                           &control (*filter, "filter_resonance", "Resonance", CS::Knob),
                           &control (*filter, "filter_mode", "Mode", CS::Combo),
                           &control (*filter, "filter_env", "Env 2 amount", CS::Knob),
                           &control (*filter, "filter_lfo", "LFO 2 amount", CS::Knob) };

        voice = &panel ("Voice");
        voiceControls = { &control (*voice, "part_polyphony", "Voicing", CS::Combo),
                          &control (*voice, "part_legato", "Legato", CS::Combo),
                          &control (*voice, "part_volume", "Volume", CS::Knob),
                          &control (*voice, "part_pan", "Pan", CS::Knob),
                          &control (*voice, "part_portamento", "Glide", CS::Knob),
                          &control (*voice, "part_octave", "Octave", CS::Knob),
                          &control (*voice, "part_tuning", "Tune", CS::Knob),
                          &control (*voice, "part_spread", "Spread", CS::Knob) };

        for (int i = 0; i < 3; ++i)
        {
            const auto e = "env" + n (i + 1), l = "lfo" + n (i + 1);
            auto& p = panel ("Envelope " + n (i + 1) + "  /  LFO " + n (i + 1));
            envelopes[i] = { &p, &display<EnvelopeDisplay> (p, state, i + 1),
                             &display<LfoDisplay> (p, state, l + "_shape", l + "_rate", true),
                             &control (p, e + "_attack", "Attack", CS::Knob),
                             &control (p, e + "_decay", "Decay", CS::Knob),
                             &control (p, e + "_sustain", "Sustain", CS::Knob),
                             &control (p, e + "_release", "Release", CS::Knob),
                             &control (p, l + "_shape", "LFO wave", CS::Combo),
                             &control (p, l + "_rate", "Rate", CS::Knob),
                             &control (p, l + "_sync", "Trigger", CS::Combo) };
        }

        voiceLfo = &panel ("Voice LFO");
        voiceLfoDisplay = &display<LfoDisplay> (*voiceLfo, state, "voice_lfo_shape", "voice_lfo_rate", false);
        voiceLfoShape = &control (*voiceLfo, "voice_lfo_shape", "Wave", CS::Combo);
        voiceLfoRate = &control (*voiceLfo, "voice_lfo_rate", "Rate", CS::Knob);
        voiceLfo->onPaint = [this] (juce::Graphics& g)
        {
            g.setColour (Palette::faintText);
            g.setFont (juce::Font (juce::FontOptions (11.5f)));
            g.drawFittedText ("LFO 4 runs separately on every voice card, so each note gets its own "
                              "phase. Route it in the Mod page.",
                              voiceLfo->content().withHeight (120).reduced (2, 6), juce::Justification::topLeft, 6);
        };
    }

    void SynthPage::resized()
    {
        auto area = getLocalBounds().reduced (kMargin);
        const int gap = 22;

        // Row 1: oscillators -> mixer -> filter -> voice.
        auto top = area.removeFromTop (322);
        auto oscColumn = top.removeFromLeft (360);
        for (auto& o : oscillators)
        {
            o.panel->setBounds (oscColumn.removeFromTop (155));
            oscColumn.removeFromTop (12);
            auto c = o.panel->content();
            o.display->setBounds (c.removeFromLeft (130).reduced (0, 4));
            c.removeFromLeft (8);
            o.shape->setBounds (c.removeFromTop (44));
            const int w = c.getWidth() / 3;
            o.parameter->setBounds (c.removeFromLeft (w));
            o.range->setBounds (c.removeFromLeft (w));
            o.tune->setBounds (c);
        }
        top.removeFromLeft (gap);
        mixer->setBounds (top.removeFromLeft (262));
        layoutGrid (mixerControls, mixer->content(), 3, 96);
        top.removeFromLeft (gap);
        filter->setBounds (top.removeFromLeft (300));
        {
            auto c = filter->content();
            filterDisplay->setBounds (c.removeFromTop (70));
            c.removeFromTop (4);
            auto models = c.removeFromTop (44);
            filterModel->setBounds (models.removeFromLeft (models.getWidth() * 3 / 5).reduced (3, 0));
            filterVariant->setBounds (models.reduced (3, 0));
            c.removeFromTop (4);
            layoutGrid (filterControls, c, 3, 84);
        }
        top.removeFromLeft (gap);
        voice->setBounds (top);
        {
            auto c = voice->content();
            const int w = c.getWidth() / 2;
            auto row = c.removeFromTop (46);
            voiceControls[0]->setBounds (row.removeFromLeft (w).reduced (3, 0));
            voiceControls[1]->setBounds (row.reduced (3, 0));
            c.removeFromTop (4);
            std::vector<ParameterControl*> knobs (voiceControls.begin() + 2, voiceControls.end());
            layoutGrid (knobs, c, 2, 78);
        }

        // Row 2: envelopes with their LFOs, and the voice LFO.
        area.removeFromTop (16);
        const int envWidth = 318;
        for (auto& e : envelopes)
        {
            e.panel->setBounds (area.removeFromLeft (envWidth));
            area.removeFromLeft (16);
            auto c = e.panel->content();
            e.envelope->setBounds (c.removeFromTop (78));
            c.removeFromTop (2);
            auto knobs = c.removeFromTop (84);
            const int w = knobs.getWidth() / 4;
            for (auto* k : { e.attack, e.decay, e.sustain, e.release })
                k->setBounds (knobs.removeFromLeft (w));
            c.removeFromTop (8);
            e.lfo->setBounds (c.removeFromTop (40));
            c.removeFromTop (4);
            const int lw = c.getWidth() / 3;
            e.shape->setBounds (c.removeFromLeft (lw).reduced (3, 10));
            e.rate->setBounds (c.removeFromLeft (lw));
            e.sync->setBounds (c.reduced (3, 10));
        }
        voiceLfo->setBounds (area);
        {
            auto c = voiceLfo->content();
            c.removeFromTop (172);  // Line up with the LFO row of the envelope panels.
            voiceLfoDisplay->setBounds (c.removeFromTop (40));
            c.removeFromTop (4);
            voiceLfoShape->setBounds (c.removeFromLeft (c.getWidth() / 2).reduced (3, 10));
            voiceLfoRate->setBounds (c);
        }
    }

    void SynthPage::bindPart (int part)
    {
        Page::bindPart (part);
        filterVariant->bindVariantPart (part);
        filterModelReader->bindPart (part);
        poll();
    }

    void SynthPage::poll()
    {
        Page::poll();
        filterVariant->refresh();
        filterModelReader->poll();
        // The LP/BP/HP mode belongs to the Ambika SVF; Surge models carry
        // their response type in the model itself.
        filterControls[2]->setEnabled (filterModelReader->value (0) == 0);
    }

    void SynthPage::paint (juce::Graphics& g)
    {
        const auto colour = accentOf (*this).withAlpha (0.75f);
        const auto m = mixer->getBounds().toFloat(), f = filter->getBounds().toFloat(),
                   v = voice->getBounds().toFloat();
        for (auto& o : oscillators)
        {
            const auto b = o.panel->getBounds().toFloat();
            drawArrow (g, { b.getRight() + 3.0f, b.getCentreY() }, { m.getX() - 3.0f, b.getCentreY() }, colour);
        }
        drawArrow (g, { m.getRight() + 3.0f, m.getCentreY() }, { f.getX() - 3.0f, m.getCentreY() }, colour);
        drawArrow (g, { f.getRight() + 3.0f, f.getCentreY() }, { v.getX() - 3.0f, f.getCentreY() }, colour);
    }

    //==============================================================================
    namespace
    {
        constexpr int kRowHeight = 32;
        constexpr int kTableTop = 62;  // Panel-relative y of the first row.
        constexpr int kTableLeft = 52;

        void paintTable (juce::Graphics& g, const Panel& panel, int rows,
                         std::initializer_list<std::pair<const char*, int>> columns)
        {
            g.setColour (Palette::dimText);
            g.setFont (juce::Font (juce::FontOptions (11.5f)));
            int x = kTableLeft;
            for (const auto& [text, width] : columns)
            {
                g.drawText (text, x, kTableTop - 22, width, 18, juce::Justification::centredLeft);
                x += width;
            }
            for (int r = 0; r < rows; ++r)
            {
                const int y = kTableTop + r * kRowHeight;
                if (r % 2 == 0)
                {
                    g.setColour (Palette::panelRaised.withAlpha (0.5f));
                    g.fillRoundedRectangle (8.0f, (float) y, (float) panel.getWidth() - 16.0f, (float) kRowHeight, 4.0f);
                }
                g.setColour (Palette::faintText);
                g.drawText (n (r + 1), 16, y, 30, kRowHeight, juce::Justification::centredLeft);
            }
        }
    }

    ModulationPage::ModulationPage (APVTS& s) : Page (s)
    {
        matrix = &panel ("Modulation matrix");
        for (int i = 1; i <= ambika::kNumModulations; ++i)
        {
            const auto m = "mod" + n (i);
            modulationRows.push_back ({ &control (*matrix, m + "_source", {}, ControlStyle::Combo),
                                        &control (*matrix, m + "_destination", {}, ControlStyle::Combo),
                                        &control (*matrix, m + "_amount", {}, ControlStyle::Bar) });
        }
        matrix->onPaint = [this] (juce::Graphics& g)
        {
            paintTable (g, *matrix, ambika::kNumModulations, { { "Source", 180 }, { "Destination", 200 }, { "Amount", 250 } });
            g.setColour (Palette::faintText);
            g.setFont (juce::Font (juce::FontOptions (11.0f)));
            g.drawText ("Slot 14 is scaled by the mod wheel, as on the hardware.", kTableLeft,
                        matrix->getHeight() - 26, 400, 16, juce::Justification::centredLeft);
        };

        modifiers = &panel ("Modifiers");
        for (int i = 1; i <= ambika::kNumModifiers; ++i)
        {
            const auto m = "modifier" + n (i);
            modifierRows.push_back ({ &control (*modifiers, m + "_in1", {}, ControlStyle::Combo),
                                      &control (*modifiers, m + "_operator", {}, ControlStyle::Combo),
                                      &control (*modifiers, m + "_in2", {}, ControlStyle::Combo) });
        }
        modifiers->onPaint = [this] (juce::Graphics& g)
        {
            paintTable (g, *modifiers, ambika::kNumModifiers, { { "Input 1", 135 }, { "Operator", 110 }, { "Input 2", 135 } });
            g.setColour (Palette::faintText);
            g.setFont (juce::Font (juce::FontOptions (11.0f)));
            g.drawFittedText ("Each modifier combines two sources into a new one (mod. 1 to mod. 4), "
                              "usable in the matrix.\n\nSeq. 1/2 and arp sources stay at zero: the sequencer "
                              "and arpeggiator are not part of this version.",
                              juce::Rectangle<int> (16, kTableTop + ambika::kNumModifiers * kRowHeight + 16,
                                                    modifiers->getWidth() - 32, 90),
                              juce::Justification::topLeft, 6);
        };
    }

    void ModulationPage::resized()
    {
        auto area = getLocalBounds().reduced (kMargin);
        matrix->setBounds (area.removeFromLeft (700).withHeight (kTableTop + ambika::kNumModulations * kRowHeight + 40));
        area.removeFromLeft (16);
        modifiers->setBounds (area.withHeight (kTableTop + ambika::kNumModifiers * kRowHeight + 130));

        auto layoutRows = [] (std::vector<std::array<ParameterControl*, 3>>& rows, std::array<int, 3> widths)
        {
            int y = kTableTop;
            for (auto& row : rows)
            {
                int x = kTableLeft;
                for (size_t c = 0; c < 3; ++c)
                {
                    row[c]->setBounds (x, y + 3, widths[c] - 10, kRowHeight - 6);
                    x += widths[c];
                }
                y += kRowHeight;
            }
        };
        layoutRows (modulationRows, { 180, 200, 250 });
        layoutRows (modifierRows, { 135, 110, 135 });
    }

    //==============================================================================
    namespace
    {
        constexpr int kPartRowHeight = 44;
        constexpr int kPartTableTop = 52;
        constexpr int kColumnChannel = 130, kColumnLow = 250, kColumnHigh = 414, kColumnRange = 584,
                      kColumnPan = 846, kColumnVoices = 1000;
    }

    MultiPage::MultiPage (APVTS& s, AmbikaAudioProcessor& p, std::function<void (int)> editPart)
        : Page (s), processor (p)
    {
        parts = &panel ("Parts");
        for (int i = 0; i < kNumParts; ++i)
        {
            auto& row = rows[(size_t) i];
            row.channel = &control (*parts, partChannelId (i), {}, ControlStyle::Combo, false);
            row.low = &control (*parts, partKeyLowId (i), {}, ControlStyle::Bar, false);
            row.high = &control (*parts, partKeyHighId (i), {}, ControlStyle::Bar, false);
            row.range = &display<KeyRangeDisplay> (*parts, state, i);
            row.pan = &control (*parts, partParameterId (i, juce::String ("part_pan")), {}, ControlStyle::Bar, false);
            row.edit = std::make_unique<juce::TextButton> ("Edit");
            row.edit->setColour (juce::TextButton::textColourOffId, Palette::part (i));
            row.edit->onClick = [editPart, i] { editPart (i); };
            parts->addAndMakeVisible (*row.edit);
        }
        parts->onPaint = [this] (juce::Graphics& g) { paintParts (g); };

        voices = &panel ("Voice cards");
        for (int v = 0; v < kNumVoices; ++v)
            owners[(size_t) v] = &control (*voices, voicePartId (v), "Voice " + n (v + 1), ControlStyle::Combo, false);
        voices->onPaint = [this] (juce::Graphics& g) { paintVoices (g); };

        distribution = &panel ("Note distribution");
        distributionMode = &control (*distribution, kDistributionId, "Notes go to", ControlStyle::Combo, false);
        partsPerNote = &control (*distribution, kPartsPerNoteId, "Parts per note", ControlStyle::Knob, false);
        distributionReader = std::make_unique<ParameterReader> (
            state, std::vector<juce::String> { kDistributionId, kPartsPerNoteId }, false);
        distribution->onPaint = [this] (juce::Graphics& g) { paintDistribution (g); };
        shownOwners.fill (0xfe);
    }

    void MultiPage::resized()
    {
        auto area = getLocalBounds().reduced (kMargin);
        parts->setBounds (area.removeFromTop (kPartTableTop + kNumParts * kPartRowHeight + 14));
        for (int i = 0; i < kNumParts; ++i)
        {
            auto& row = rows[(size_t) i];
            const int y = kPartTableTop + i * kPartRowHeight;
            row.channel->setBounds (kColumnChannel, y + 9, 100, 26);
            row.low->setBounds (kColumnLow, y + 8, 150, 28);
            row.high->setBounds (kColumnHigh, y + 8, 150, 28);
            row.range->setBounds (kColumnRange, y + 8, 246, 28);
            row.pan->setBounds (kColumnPan, y + 8, 140, 28);
            row.edit->setBounds (parts->getWidth() - 96, y + 9, 80, 26);
        }

        area.removeFromTop (16);
        auto bottom = area.removeFromTop (178);
        voices->setBounds (bottom.removeFromLeft (760));
        {
            auto c = voices->content().withTrimmedTop (8);
            const int w = c.getWidth() / kNumVoices;
            for (auto* owner : owners)
                owner->setBounds (c.removeFromLeft (w).reduced (8, 0).withHeight (44));
        }
        bottom.removeFromLeft (16);
        distribution->setBounds (bottom);
        {
            auto c = distribution->content();
            auto controlsArea = c.removeFromLeft (130);
            distributionMode->setBounds (controlsArea.removeFromTop (46));
            partsPerNote->setBounds (controlsArea.removeFromTop (92));
        }
    }

    void MultiPage::paintParts (juce::Graphics& g)
    {
        g.setFont (juce::Font (juce::FontOptions (11.5f)));
        g.setColour (Palette::dimText);
        const std::pair<const char*, int> headings[] = { { "MIDI channel", kColumnChannel },
                                                         { "Lowest key", kColumnLow },
                                                         { "Highest key", kColumnHigh },
                                                         { "Key range", kColumnRange },
                                                         { "Pan", kColumnPan },
                                                         { "Voices", kColumnVoices } };
        for (const auto& [text, x] : headings)
            g.drawText (text, x, kPartTableTop - 22, 150, 18, juce::Justification::centredLeft);

        int voicesPerPart[kNumParts] = {};
        for (auto owner : shownOwners)
            if (owner < kNumParts)
                ++voicesPerPart[owner];

        for (int i = 0; i < kNumParts; ++i)
        {
            const int y = kPartTableTop + i * kPartRowHeight;
            const bool active = voicesPerPart[i] > 0;
            if (i % 2 == 0)
            {
                g.setColour (Palette::panelRaised.withAlpha (0.5f));
                g.fillRoundedRectangle (8.0f, (float) y, (float) parts->getWidth() - 16.0f, (float) kPartRowHeight, 4.0f);
            }
            g.setColour (Palette::part (i).withAlpha (active ? 1.0f : 0.3f));
            g.fillRoundedRectangle (18.0f, (float) y + 15.0f, 14.0f, 14.0f, 3.0f);
            g.setColour (active ? Palette::text : Palette::faintText);
            g.setFont (juce::Font (juce::FontOptions (14.0f, juce::Font::bold)));
            g.drawText ("Part " + n (i + 1), 42, y, 80, kPartRowHeight, juce::Justification::centredLeft);
            g.setFont (juce::Font (juce::FontOptions (12.5f)));
            g.setColour (active ? Palette::part (i) : Palette::faintText);
            g.drawText (active ? n (voicesPerPart[i]) + (voicesPerPart[i] == 1 ? " voice" : " voices") : "silent",
                        kColumnVoices, y, 90, kPartRowHeight, juce::Justification::centredLeft);
        }
    }

    void MultiPage::paintVoices (juce::Graphics& g)
    {
        const auto c = voices->content().withTrimmedTop (8);
        const int w = c.getWidth() / kNumVoices;
        for (int v = 0; v < kNumVoices; ++v)
        {
            const auto owner = shownOwners[(size_t) v];
            const auto colour = owner < kNumParts ? Palette::part (owner) : Palette::faintText;
            auto card = juce::Rectangle<float> ((float) c.getX() + (float) (w * v) + 4.0f, (float) c.getY() - 4.0f,
                                                      (float) w - 8.0f, 104.0f);
            g.setColour (Palette::panelRaised.withAlpha (0.6f));
            g.fillRoundedRectangle (card, 6.0f);
            g.setColour (colour.withAlpha (0.6f));
            g.fillRoundedRectangle (card.removeFromBottom (4.0f), 2.0f);

            const float level = shownLevels[(size_t) v] / 255.0f;
            const auto led = juce::Rectangle<float> (14.0f, 14.0f).withCentre ({ card.getCentreX(), card.getY() + 74.0f });
            g.setColour (Palette::track);
            g.fillEllipse (led);
            g.setColour (colour.withAlpha (juce::jlimit (0.0f, 1.0f, level * 1.5f)));
            g.fillEllipse (led);
            g.setColour (colour.withAlpha (0.5f));
            g.drawEllipse (led, 1.0f);
        }
        g.setColour (Palette::faintText);
        g.setFont (juce::Font (juce::FontOptions (11.0f)));
        g.drawText ("Each voice card belongs to one part. A part with no voices is silent.", c.getX(),
                    voices->getHeight() - 26, c.getWidth(), 16, juce::Justification::centred);
    }

    void MultiPage::paintDistribution (juce::Graphics& g)
    {
        const bool roundRobin = distributionReader->value (0) == 1;
        const int stack = juce::jmax (1, distributionReader->value (1));
        auto text = distribution->content().withTrimmedLeft (146);
        g.setColour (Palette::text);
        g.setFont (juce::Font (juce::FontOptions (13.0f, juce::Font::bold)));
        g.drawText (roundRobin ? "Round robin" : "Layer / split", text.removeFromTop (20), juce::Justification::topLeft);
        g.setColour (Palette::dimText);
        g.setFont (juce::Font (juce::FontOptions (12.0f)));
        const juce::String description = roundRobin
            ? "Each note goes to the next part in turn"
                  + juce::String (stack > 1 ? " - " + juce::String (stack) + " parts per note" : juce::String())
                  + ". Give each part one voice and its own patch to cycle through six sounds; "
                    "parts per note stacks 2, 3... of them on every key."
            : "Every part whose MIDI channel and key range match a note plays it. Put several "
              "parts on one channel to layer them, or give them different key ranges to split "
              "the keyboard.";
        g.drawFittedText (description, text.removeFromTop (90), juce::Justification::topLeft, 6);
    }

    void MultiPage::poll()
    {
        Page::poll();
        if (distributionReader->poll())
        {
            partsPerNote->setEnabled (distributionReader->value (0) == 1);
            distribution->repaint();
        }
        bool levelsChanged = false, ownersChanged = false;
        for (int v = 0; v < kNumVoices; ++v)
        {
            const auto level = processor.voiceLevel (v);
            const auto owner = processor.voiceOwner (v);
            levelsChanged |= level != shownLevels[(size_t) v];
            ownersChanged |= owner != shownOwners[(size_t) v];
            shownLevels[(size_t) v] = level;
            shownOwners[(size_t) v] = owner;
        }
        if (levelsChanged || ownersChanged)
            voices->repaint();
        if (ownersChanged)
            parts->repaint();
    }

    //==============================================================================
    // Everything inside the window, at design size; the editor scales it.
    class AmbikaAudioProcessorEditor::Content final : public juce::Component
    {
    public:
        Content (AmbikaAudioProcessor& p, std::function<void (int)> showPageFn, std::function<void (int)> editPartFn)
            : processor (p),
              presetContext { p.presets(), [&p] { return p.editedPart(); },
                              [this] (const juce::String& text, bool isError) { showToast (text, isError); },
                              [this] { if (browser.isVisible()) browser.refresh(); } },
              dialog (presetContext),
              bar (presetContext, dialog, [this, showPageFn] { showPageFn (currentPage == 3 ? lastPage : 3); }),
              browser (presetContext, dialog),
              synth (p.parameters()),
              modulation (p.parameters()),
              multi (p.parameters(), p, [showPageFn, editPartFn] (int part) { editPartFn (part); showPageFn (0); }),
              output (p.parameters(), kOutputGainId, {}, ControlStyle::Bar, false)
        {
            const char* names[] = { "Synth", "Mod", "Multi" };
            for (int i = 0; i < 3; ++i)
            {
                auto& b = pageButtons[(size_t) i];
                b.setButtonText (names[i]);
                b.onClick = [showPageFn, i] { showPageFn (i); };
                addAndMakeVisible (b);
            }
            for (int i = 0; i < kNumParts; ++i)
            {
                auto& b = partButtons[(size_t) i];
                b.setButtonText (n (i + 1));
                b.setColour (juce::TextButton::buttonOnColourId, Palette::part (i));
                b.setColour (juce::TextButton::textColourOffId, Palette::part (i));
                b.setTooltip ("Edit part " + n (i + 1));
                b.onClick = [editPartFn, i] { editPartFn (i); };
                addAndMakeVisible (b);
            }
            addAndMakeVisible (synth);
            addChildComponent (modulation);
            addChildComponent (multi);
            addChildComponent (browser);
            addAndMakeVisible (output);
            addAndMakeVisible (bar);
            addChildComponent (dialog);
        }

        void showSaveDialog() { dialog.openForSave(); }

        void showToast (const juce::String& text, bool isError)
        {
            toastText = text;
            toastIsError = isError;
            const int id = ++toastId;
            repaint (toastArea());
            juce::Component::SafePointer<Content> safe (this);
            juce::Timer::callAfterDelay (isError ? 5000 : 2500, [safe, id]
            {
                if (safe != nullptr && safe->toastId == id)
                {
                    safe->toastText.clear();
                    safe->repaint (safe->toastArea());
                }
            });
        }

        juce::Rectangle<int> toastArea() const
        {
            return getLocalBounds().removeFromBottom (54).withSizeKeepingCentre (560, 34);
        }

        void paintOverChildren (juce::Graphics& g) override
        {
            if (toastText.isEmpty() || dialog.isVisible())
                return;
            const auto area = toastArea().toFloat();
            g.setColour (Palette::panelRaised.withAlpha (0.97f));
            g.fillRoundedRectangle (area, 8.0f);
            g.setColour (toastIsError ? juce::Colour (0xfff0647a) : accentOf (*this));
            g.drawRoundedRectangle (area.reduced (0.5f), 8.0f, 1.0f);
            g.setColour (Palette::text);
            g.setFont (juce::Font (juce::FontOptions (13.0f)));
            g.drawFittedText (toastText, area.toNearestInt().reduced (14, 0), juce::Justification::centred, 1);
        }

        void paint (juce::Graphics& g) override
        {
            const auto& skin = currentSkin();
            g.fillAll (Palette::background);
            if (const auto& background = skin.backgroundFor (currentPage); background.isValid())
                g.drawImage (background, getLocalBounds().toFloat(), juce::RectanglePlacement::stretchToFit);

            const auto header = getLocalBounds().removeFromTop (kHeaderHeight);
            g.setColour (Palette::panel.withMultipliedAlpha (skin.settings.headerOpacity));
            g.fillRect (header);
            g.setColour (Palette::outline.withMultipliedAlpha (skin.settings.headerOpacity));
            g.drawHorizontalLine (kHeaderHeight - 1, 0.0f, (float) getWidth());

            if (skin.logo.isValid())
            {
                g.drawImage (skin.logo, juce::Rectangle<float> (16.0f, 6.0f, 230.0f, 52.0f),
                             juce::RectanglePlacement::xLeft | juce::RectanglePlacement::yMid
                                 | juce::RectanglePlacement::onlyReduceInSize);
            }
            else
            {
                g.setColour (Palette::text);
                g.setFont (juce::Font (juce::FontOptions (28.0f, juce::Font::bold)));
                const auto wordmark = juce::String ("eureka");
                const int wordWidth = juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), wordmark);
                g.drawText (wordmark, 20, 6, wordWidth + 4, 34, juce::Justification::centredLeft);
                g.setColour (accentOf (*this));
                g.drawText ("!", 20 + wordWidth + 1, 6, 20, 34, juce::Justification::centredLeft);
                g.setColour (Palette::faintText);
                g.setFont (juce::Font (juce::FontOptions (10.5f)));
                g.drawText ("after the Mutable Instruments Ambika", 21, 38, 220, 16, juce::Justification::centredLeft);
            }

            g.setColour (Palette::dimText);
            g.setFont (juce::Font (juce::FontOptions (11.0f)));
            g.drawText ("EDIT PART", partButtons[0].getX() - 76, 0, 68, kHeaderHeight, juce::Justification::centredRight);
            g.drawText ("VOICES", voiceLedsArea.getX() - 54, 0, 46, kHeaderHeight, juce::Justification::centredRight);
            g.drawText ("OUT", output.getX() - 36, 0, 28, kHeaderHeight, juce::Justification::centredRight);

            // Voice LEDs, in the colour of the part that owns each voice.
            const int step = voiceLedsArea.getWidth() / kNumVoices;
            for (int v = 0; v < kNumVoices; ++v)
            {
                const auto owner = processor.voiceOwner (v);
                const auto colour = owner < kNumParts ? Palette::part (owner) : Palette::faintText;
                const auto led = juce::Rectangle<float> (11.0f, 11.0f)
                                     .withCentre ({ (float) voiceLedsArea.getX() + (float) step * ((float) v + 0.5f),
                                                    (float) voiceLedsArea.getCentreY() });
                g.setColour (Palette::track);
                g.fillEllipse (led);
                g.setColour (colour.withAlpha (juce::jlimit (0.0f, 1.0f, shownLevels[(size_t) v] / 255.0f * 1.5f)));
                g.fillEllipse (led);
                g.setColour (colour.withAlpha (0.5f));
                g.drawEllipse (led, 1.0f);
            }
        }

        void resized() override
        {
            auto header = getLocalBounds().removeFromTop (kHeaderHeight).reduced (16, 16);
            header.removeFromLeft (230);
            for (auto& b : pageButtons)
            {
                b.setBounds (header.removeFromLeft (76));
                header.removeFromLeft (6);
            }
            auto right = header;
            output.setBounds (right.removeFromRight (150));
            right.removeFromRight (44);
            voiceLedsArea = right.removeFromRight (108);
            right.removeFromRight (62);
            for (int i = kNumParts - 1; i >= 0; --i)
            {
                partButtons[(size_t) i].setBounds (right.removeFromRight (34));
                right.removeFromRight (5);
            }

            bar.setBounds (0, kHeaderHeight, getWidth(), kPresetBarHeight);
            const auto pageArea = getLocalBounds().withTrimmedTop (kPagesTop);
            synth.setBounds (pageArea);
            modulation.setBounds (pageArea);
            multi.setBounds (pageArea);
            browser.setBounds (pageArea);
            dialog.setBounds (getLocalBounds());
        }

        void showPage (int index)
        {
            if (currentPage != 3)
                lastPage = currentPage;
            currentPage = index;
            synth.setVisible (index == 0);
            modulation.setVisible (index == 1);
            multi.setVisible (index == 2);
            browser.setVisible (index == 3);
            bar.setBrowserOpen (index == 3);
            if (currentSkin().pageBackgrounds[(size_t) juce::jlimit (0, 3, index)].isValid())
                repaint();  // Page-specific background.
            for (int i = 0; i < 3; ++i)
                pageButtons[(size_t) i].setToggleState (i == index, juce::dontSendNotification);
        }

        void setEditedPart (int part)
        {
            for (int i = 0; i < kNumParts; ++i)
                partButtons[(size_t) i].setToggleState (i == part, juce::dontSendNotification);
            synth.bindPart (part);
            modulation.bindPart (part);
            if (browser.isVisible())
                browser.repaint();
        }

        void poll()
        {
            Page* pages[] = { &synth, &modulation, &multi };
            for (auto* page : pages)
                if (page->isVisible())
                    page->poll();
            bool changed = false;
            for (int v = 0; v < kNumVoices; ++v)
            {
                const auto level = processor.voiceLevel (v);
                changed |= level != shownLevels[(size_t) v];
                shownLevels[(size_t) v] = level;
            }
            if (changed)
                repaint (voiceLedsArea.expanded (8));
            bar.poll();
        }

    private:
        AmbikaAudioProcessor& processor;
        PresetContext presetContext;
        PresetDialog dialog;
        PresetBar bar;
        PresetBrowserPage browser;
        int currentPage = 0, lastPage = 0;
        juce::String toastText;
        bool toastIsError = false;
        int toastId = 0;
        std::array<juce::TextButton, 3> pageButtons;
        std::array<juce::TextButton, kNumParts> partButtons;
        SynthPage synth;
        ModulationPage modulation;
        MultiPage multi;
        ParameterControl output;
        juce::Rectangle<int> voiceLedsArea;
        std::array<uint8_t, kNumVoices> shownLevels {};
    };

    //==============================================================================
    AmbikaAudioProcessorEditor::AmbikaAudioProcessorEditor (AmbikaAudioProcessor& p)
        : AudioProcessorEditor (&p), audioProcessor (p)
    {
        // Skin images are read when the window opens (never during a host's
        // plug-in scan), so edits show up the next time the window opens.
        setCurrentSkin (Skin::load());
        setLookAndFeel (&lookAndFeel);
        content = std::make_unique<Content> (
            p, [this] (int page) { showPage (page); }, [this] (int part) { editPart (part); });
        content->setSize (designWidth, designHeight);
        addAndMakeVisible (*content);

        editPart (audioProcessor.editedPart());
        showPage (0);

        setResizable (true, true);
        setResizeLimits (designWidth * 6 / 10, designHeight * 6 / 10, designWidth * 2, designHeight * 2);
        getConstrainer()->setFixedAspectRatio ((double) designWidth / designHeight);
        setSize (designWidth, designHeight);
        startTimerHz (30);
    }

    AmbikaAudioProcessorEditor::~AmbikaAudioProcessorEditor()
    {
        stopTimer();
        content.reset();
        setLookAndFeel (nullptr);
    }

    void AmbikaAudioProcessorEditor::resized()
    {
        const float scale = (float) getWidth() / (float) designWidth;
        content->setTransform (juce::AffineTransform::scale (scale));
    }

    void AmbikaAudioProcessorEditor::showPage (int index)
    {
        content->showPage (index);
    }

    void AmbikaAudioProcessorEditor::editPart (int part)
    {
        audioProcessor.setEditedPart (part);
        lookAndFeel.setAccent (Palette::part (part));
        content->setEditedPart (part);
        content->sendLookAndFeelChange();
        content->repaint();
    }

    void AmbikaAudioProcessorEditor::showSaveDialog()
    {
        content->showSaveDialog();
    }

    void AmbikaAudioProcessorEditor::timerCallback()
    {
        content->poll();
    }
}
