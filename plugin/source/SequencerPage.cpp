#include "SequencerPage.h"

#include "controller_tables.h"

namespace ambika_plugin
{
    namespace
    {
        using ambika::plugin::kNumSteps;

        juce::String n (int i) { return juce::String (i); }

        juce::Colour accentOf (const juce::Component& c)
        {
            return c.findColour (juce::Slider::rotarySliderFillColourId);
        }

        // Step columns: a gap after every 4 steps, so beats read at a glance.
        juce::Rectangle<float> column (juce::Rectangle<float> area, int step)
        {
            const float groupGap = 6.0f;
            const float width = (area.getWidth() - 3.0f * groupGap) / (float) kNumSteps;
            const float x = area.getX() + (float) step * width + (float) (step / 4) * groupGap;
            return { x, area.getY(), width, area.getHeight() };
        }

        int columnAt (juce::Rectangle<float> area, float x)
        {
            for (int step = 0; step < kNumSteps; ++step)
                if (x < column (area, step).getRight() + 3.0f)
                    return step;
            return kNumSteps - 1;
        }

        void paintStepNumbers (juce::Graphics& g, juce::Rectangle<float> area, int length, int playing)
        {
            g.setFont (juce::Font (eurekaFont (10.5f)));
            for (int step = 0; step < kNumSteps; ++step)
            {
                const auto c = column (area, step);
                g.setColour (step == playing ? Palette::text : step < length ? Palette::dimText : Palette::faintText.withAlpha (0.5f));
                g.drawText (n (step + 1), c, juce::Justification::centred);
            }
        }
    }

    //==============================================================================
    juce::RangedAudioParameter* PartValues::parameter (const juce::String& engineId) const
    {
        return state.getParameter (partParameterId (currentPart, engineId));
    }

    int PartValues::get (const juce::String& engineId) const
    {
        const auto* info = findParameterInfo (engineId);
        auto* raw = state.getRawParameterValue (partParameterId (currentPart, engineId));
        return info != nullptr && raw != nullptr ? toEngineValue (*info, raw->load()) : 0;
    }

    void PartValues::set (const juce::String& engineId, int value)
    {
        const auto* info = findParameterInfo (engineId);
        auto* p = parameter (engineId);
        if (info == nullptr || p == nullptr)
            return;
        value = juce::jlimit<int> (info->min_value, info->max_value, value);
        if (value == get (engineId))
            return;
        if (! touched.contains (engineId))
        {
            touched.add (engineId);
            p->beginChangeGesture();
        }
        const float denormalised = (float) (info->choices != nullptr ? value - info->min_value : value);
        p->setValueNotifyingHost (p->convertTo0to1 (denormalised));
    }

    void PartValues::endGestures()
    {
        for (const auto& id : touched)
            if (auto* p = parameter (id))
                p->endChangeGesture();
        touched.clear();
    }

    //==============================================================================
    StepSequenceEditor::StepSequenceEditor (APVTS& s, AmbikaAudioProcessor& p, int index)
        : values (s), processor (p), sequence (index)
    {
        setRepaintsOnMouseActivity (false);
        setTooltip ("Drag to draw the steps. Seq. " + n (sequence + 1)
                    + " is a modulation source: route it in the Mod page.");
    }

    void StepSequenceEditor::bindPart (int part)
    {
        values.bindPart (part);
        shownLength = -1;
        poll();
    }

    void StepSequenceEditor::poll()
    {
        bool changed = false;
        const auto prefix = "seq" + n (sequence + 1);
        for (int step = 0; step < kNumSteps; ++step)
        {
            const int v = values.get (prefix + "_step" + n (step + 1));
            changed |= v != shown[(size_t) step];
            shown[(size_t) step] = v;
        }
        const int length = values.get (prefix + "_length");
        const int playing = processor.currentStep (values.part(), sequence);
        changed |= length != shownLength || playing != shownStep;
        shownLength = length;
        shownStep = playing;
        if (changed)
            repaint();
    }

    juce::Rectangle<float> StepSequenceEditor::barArea() const
    {
        return getLocalBounds().toFloat().reduced (2.0f, 0.0f).withTrimmedBottom (18.0f);
    }

    void StepSequenceEditor::paint (juce::Graphics& g)
    {
        const auto area = barArea();
        const auto accent = accentOf (*this);
        g.setColour (Palette::track.withAlpha (0.5f));
        g.fillRoundedRectangle (area.expanded (2.0f, 2.0f), 4.0f);
        for (int step = 0; step < kNumSteps; ++step)
        {
            auto c = column (area, step).reduced (1.5f, 0.0f);
            const bool active = step < shownLength;
            g.setColour (Palette::panelRaised.withAlpha (active ? 0.9f : 0.35f));
            g.fillRoundedRectangle (c, 3.0f);
            const float level = (float) shown[(size_t) step] / 255.0f;
            auto bar = c.withTop (c.getBottom() - juce::jmax (2.0f, level * c.getHeight()));
            g.setColour (active ? accent.withAlpha (step == shownStep ? 1.0f : 0.72f) : Palette::faintText.withAlpha (0.35f));
            g.fillRoundedRectangle (bar, 3.0f);
            if (step == shownStep)
            {
                g.setColour (Palette::text.withAlpha (0.85f));
                g.drawRoundedRectangle (c, 3.0f, 1.2f);
            }
        }
        paintStepNumbers (g, getLocalBounds().toFloat().removeFromBottom (16.0f).reduced (2.0f, 0.0f), shownLength, shownStep);
    }

    void StepSequenceEditor::editAt (juce::Point<float> position)
    {
        const auto area = barArea();
        const int step = columnAt (area, position.x);
        const float level = 1.0f - (position.y - area.getY()) / area.getHeight();
        values.set ("seq" + n (sequence + 1) + "_step" + n (step + 1), juce::roundToInt (juce::jlimit (0.0f, 1.0f, level) * 255.0f));
    }

    void StepSequenceEditor::mouseDown (const juce::MouseEvent& e)
    {
        lastDrag = e.position;
        editAt (e.position);
        poll();
    }

    void StepSequenceEditor::mouseDrag (const juce::MouseEvent& e)
    {
        // Fill every column crossed since the last event, so fast drags draw
        // a continuous line.
        const auto from = lastDrag, to = e.position;
        const int steps = juce::jmax (1, (int) (std::abs (to.x - from.x) / 4.0f));
        for (int i = 1; i <= steps; ++i)
            editAt (from + (to - from) * ((float) i / (float) steps));
        lastDrag = to;
        poll();
    }

    void StepSequenceEditor::mouseUp (const juce::MouseEvent&)
    {
        values.endGestures();
    }

    //==============================================================================
    NotePatternEditor::NotePatternEditor (APVTS& s, AmbikaAudioProcessor& p) : values (s), processor (p)
    {
        setTooltip ("Gate: click to play or rest. Note: drag up/down or use the wheel (60 = the key you hold; "
                    "double-click resets). Velocity: drag. Tie: slide into the next step.");
    }

    juce::String NotePatternEditor::id (Row row, int step) const
    {
        static const char* const kNames[] = { "pattern_gate", "pattern_note", "pattern_velocity", "pattern_legato" };
        return kNames[row] + n (step + 1);
    }

    void NotePatternEditor::bindPart (int part)
    {
        values.bindPart (part);
        shownLength = -1;
        poll();
    }

    void NotePatternEditor::poll()
    {
        bool changed = false;
        for (int row = 0; row < kNumRows; ++row)
            for (int step = 0; step < kNumSteps; ++step)
            {
                const int v = values.get (id ((Row) row, step));
                changed |= v != shown[(size_t) row][(size_t) step];
                shown[(size_t) row][(size_t) step] = v;
            }
        const int length = values.get ("pattern_length");
        const int playing = processor.currentStep (values.part(), 2);
        changed |= length != shownLength || playing != shownStep;
        shownLength = length;
        shownStep = playing;
        if (changed)
            repaint();
    }

    juce::Rectangle<float> NotePatternEditor::rowArea (int row) const
    {
        auto area = getLocalBounds().toFloat().withTrimmedLeft (78.0f).withTrimmedBottom (18.0f);
        const float heights[] = { 26.0f, 0.0f, 54.0f, 24.0f };
        const float gap = 6.0f;
        const float noteHeight = area.getHeight() - heights[0] - heights[2] - heights[3] - 3.0f * gap;
        float y = area.getY();
        for (int r = 0; r < row; ++r)
            y += (r == kNoteRow ? noteHeight : heights[r]) + gap;
        return { area.getX(), y, area.getWidth(), row == kNoteRow ? noteHeight : heights[row] };
    }

    juce::Rectangle<float> NotePatternEditor::cell (int row, int step) const
    {
        return column (rowArea (row), step).reduced (1.5f, 0.0f);
    }

    int NotePatternEditor::stepAt (float x) const { return columnAt (rowArea (0), x); }

    int NotePatternEditor::rowAt (float y) const
    {
        for (int row = 0; row < kNumRows; ++row)
            if (y < rowArea (row).getBottom() + 3.0f)
                return row;
        return -1;
    }

    void NotePatternEditor::paint (juce::Graphics& g)
    {
        const auto accent = accentOf (*this);
        const char* labels[] = { "Gate", "Note", "Velocity", "Tie" };
        g.setFont (juce::Font (eurekaFont (11.5f)));
        for (int row = 0; row < kNumRows; ++row)
        {
            g.setColour (Palette::dimText);
            const auto r = rowArea (row);
            g.drawText (labels[row], juce::Rectangle<float> (0.0f, r.getY(), 72.0f, r.getHeight()), juce::Justification::centredLeft);
        }

        for (int step = 0; step < kNumSteps; ++step)
        {
            const bool active = step < shownLength;
            const bool gate = shown[kGateRow][(size_t) step] != 0;
            const bool tie = shown[kTieRow][(size_t) step] != 0;
            const bool playing = step == shownStep;
            const float alpha = active ? 1.0f : 0.35f;

            // Gate.
            auto c = cell (kGateRow, step);
            g.setColour (gate ? accent.withAlpha (alpha) : Palette::panelRaised.withAlpha (0.8f * alpha));
            g.fillRoundedRectangle (c, 4.0f);
            if (playing)
            {
                g.setColour (Palette::text);
                g.drawRoundedRectangle (c.reduced (0.5f), 4.0f, 1.5f);
            }

            // Note: position in a two-octave window around the key, and the
            // interval from the key.
            c = cell (kNoteRow, step);
            g.setColour (Palette::panelRaised.withAlpha (0.6f * alpha));
            g.fillRoundedRectangle (c, 4.0f);
            const int note = shown[kNoteRow][(size_t) step];
            const int interval = note - 60;
            const float t = juce::jlimit (0.0f, 1.0f, (float) (interval + 12) / 24.0f);
            const float y = c.getBottom() - 8.0f - t * (c.getHeight() - 30.0f);
            g.setColour (Palette::outline.withAlpha (0.6f));
            const float zeroY = c.getBottom() - 8.0f - 0.5f * (c.getHeight() - 30.0f);
            g.drawHorizontalLine (juce::roundToInt (zeroY), c.getX() + 3.0f, c.getRight() - 3.0f);
            g.setColour ((gate ? accent : Palette::faintText).withAlpha (alpha));
            g.fillRoundedRectangle (juce::Rectangle<float> (c.getX() + 4.0f, y - 3.0f, c.getWidth() - 8.0f, 6.0f), 3.0f);
            g.setColour ((gate ? Palette::text : Palette::faintText).withAlpha (alpha));
            g.setFont (juce::Font (eurekaFont (11.0f, juce::Font::bold)));
            g.drawText (interval == 0 ? juce::String ("key") : (interval > 0 ? "+" : "") + n (interval),
                        c.withHeight (18.0f).translated (0.0f, 2.0f), juce::Justification::centred);

            // Velocity.
            c = cell (kVelocityRow, step);
            g.setColour (Palette::panelRaised.withAlpha (0.6f * alpha));
            g.fillRoundedRectangle (c, 3.0f);
            const float level = (float) shown[kVelocityRow][(size_t) step] / 127.0f;
            g.setColour ((gate ? accent : Palette::faintText).withAlpha (0.75f * alpha));
            g.fillRoundedRectangle (c.withTop (c.getBottom() - juce::jmax (2.0f, level * c.getHeight())), 3.0f);

            // Tie: a link into the next step.
            c = cell (kTieRow, step);
            g.setColour (Palette::panelRaised.withAlpha (0.6f * alpha));
            g.fillRoundedRectangle (c, 4.0f);
            if (tie)
            {
                g.setColour (accent.withAlpha (alpha));
                juce::Path arc;
                arc.startNewSubPath (c.getX() + 5.0f, c.getCentreY() + 3.0f);
                arc.quadraticTo (c.getCentreX(), c.getY() + 3.0f, c.getRight() + 4.0f, c.getCentreY() + 3.0f);
                g.strokePath (arc, juce::PathStrokeType (2.0f));
            }
        }
        auto numbers = getLocalBounds().toFloat().removeFromBottom (16.0f).withTrimmedLeft (78.0f);
        paintStepNumbers (g, numbers, shownLength, shownStep);
    }

    void NotePatternEditor::mouseDown (const juce::MouseEvent& e)
    {
        dragRow = rowAt (e.position.y);
        dragStep = stepAt (e.position.x);
        if (dragRow < 0 || e.position.x < rowArea (0).getX())
        {
            dragRow = -1;
            return;
        }
        switch (dragRow)
        {
            case kGateRow:
            case kTieRow:
                // Click toggles; dragging across paints the same state.
                toggleTo = shown[(size_t) dragRow][(size_t) dragStep] ? 0 : 1;
                values.set (id ((Row) dragRow, dragStep), toggleTo);
                break;
            case kNoteRow:
                dragStartValue = shown[kNoteRow][(size_t) dragStep];
                dragStartY = e.position.y;
                break;
            case kVelocityRow:
                mouseDrag (e);
                break;
            default: break;
        }
        poll();
    }

    void NotePatternEditor::mouseDrag (const juce::MouseEvent& e)
    {
        if (dragRow < 0)
            return;
        const int step = stepAt (e.position.x);
        switch (dragRow)
        {
            case kGateRow:
            case kTieRow:
                values.set (id ((Row) dragRow, step), toggleTo);
                break;
            case kNoteRow:
                // One semitone per 6 pixels, on the step clicked.
                values.set (id (kNoteRow, dragStep),
                            dragStartValue + juce::roundToInt ((dragStartY - e.position.y) / 6.0f));
                break;
            case kVelocityRow:
            {
                const auto c = cell (kVelocityRow, step);
                const float level = 1.0f - (e.position.y - c.getY()) / c.getHeight();
                values.set (id (kVelocityRow, step), juce::roundToInt (juce::jlimit (0.0f, 1.0f, level) * 127.0f));
                break;
            }
            default: break;
        }
        poll();
    }

    void NotePatternEditor::mouseUp (const juce::MouseEvent&)
    {
        values.endGestures();
        dragRow = -1;
    }

    void NotePatternEditor::mouseDoubleClick (const juce::MouseEvent& e)
    {
        if (rowAt (e.position.y) == kNoteRow)
        {
            values.set (id (kNoteRow, stepAt (e.position.x)), 60);
            values.endGestures();
            poll();
        }
    }

    void NotePatternEditor::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
    {
        if (rowAt (e.position.y) != kNoteRow || wheel.deltaY == 0.0f)
            return;
        const int step = stepAt (e.position.x);
        values.set (id (kNoteRow, step), shown[kNoteRow][(size_t) step] + (wheel.deltaY > 0.0f ? 1 : -1));
        values.endGestures();
        poll();
    }

    //==============================================================================
    SequencerPage::SequencerPage (APVTS& s, AmbikaAudioProcessor& p) : Page (s), processor (p)
    {
        using CS = ControlStyle;
        arp = &panel ("Arpeggiator");
        mode = &control (*arp, "arp_mode", "Keys play", CS::Combo);
        direction = &control (*arp, "arp_direction", "Direction", CS::Combo);
        octaves = &control (*arp, "arp_octave", "Octaves", CS::Knob);
        rhythm = &control (*arp, "arp_pattern", "Rhythm", CS::Combo);
        division = &control (*arp, "arp_divider", "Step", CS::Combo);
        arp->onPaint = [this] (juce::Graphics& g) { paintArp (g); };
        arpReader = std::make_unique<ParameterReader> (s, std::vector<juce::String> { "arp_mode", "arp_pattern", "arp_direction" });

        info = &panel ("How it plays");
        info->onPaint = [this] (juce::Graphics& g) { paintInfo (g); };

        for (int i = 0; i < 2; ++i)
        {
            sequencePanels[(size_t) i] = &panel ("Sequence " + n (i + 1) + "  /  mod source seq. " + n (i + 1));
            lengths[(size_t) i] = &control (*sequencePanels[(size_t) i], "seq" + n (i + 1) + "_length", {}, CS::Bar);
            sequenceEditors[(size_t) i] = std::make_unique<StepSequenceEditor> (s, p, i);
            sequencePanels[(size_t) i]->addAndMakeVisible (*sequenceEditors[(size_t) i]);
            sequencePanels[(size_t) i]->onPaint = [this, i] (juce::Graphics& g)
            {
                g.setColour (Palette::dimText);
                g.setFont (juce::Font (eurekaFont (11.5f)));
                const auto b = lengths[(size_t) i]->getBounds();
                g.drawText ("Length", b.getX() - 54, b.getY(), 50, b.getHeight(), juce::Justification::centredRight);
            };
        }

        pattern = &panel ("Note pattern");
        lengths[2] = &control (*pattern, "pattern_length", {}, CS::Bar);
        patternEditor = std::make_unique<NotePatternEditor> (s, p);
        pattern->addAndMakeVisible (*patternEditor);
        pattern->onPaint = [this] (juce::Graphics& g)
        {
            g.setColour (Palette::dimText);
            g.setFont (juce::Font (eurekaFont (11.5f)));
            const auto b = lengths[2]->getBounds();
            g.drawText ("Length", b.getX() - 54, b.getY(), 50, b.getHeight(), juce::Justification::centredRight);
            g.setColour (Palette::faintText);
            g.drawText ("Plays when Keys play is set to pattern: hold a key to transpose it (60 = that key).",
                        16, b.getY(), b.getX() - 90, b.getHeight(), juce::Justification::centredLeft, true);
        };
    }

    void SequencerPage::resized()
    {
        auto area = getLocalBounds().reduced (12);
        auto top = area.removeFromTop (176);
        arp->setBounds (top.removeFromLeft (720));
        {
            auto c = arp->content();
            auto row = c.removeFromTop (46);
            const int w = row.getWidth() / 5;
            mode->setBounds (row.removeFromLeft (w).reduced (4, 0));
            direction->setBounds (row.removeFromLeft (w).reduced (4, 0));
            rhythm->setBounds (row.removeFromLeft (w * 2).reduced (4, 0));
            division->setBounds (row.reduced (4, 0));
            c.removeFromTop (4);
            octaves->setBounds (c.removeFromLeft (w));
            c.removeFromLeft (w);  // Under Direction: nothing.
            rhythmArea = c.removeFromLeft (w * 2).reduced (4, 0).withSizeKeepingCentre (w * 2 - 8, 30).translated (0, 6);
        }
        top.removeFromLeft (16);
        info->setBounds (top);

        area.removeFromTop (12);
        auto middle = area.removeFromTop (190);
        const int half = (middle.getWidth() - 16) / 2;
        for (int i = 0; i < 2; ++i)
        {
            auto* panelForSequence = sequencePanels[(size_t) i];
            panelForSequence->setBounds (middle.removeFromLeft (half));
            middle.removeFromLeft (16);
            auto c = panelForSequence->content();
            auto header = c.removeFromTop (26);
            lengths[(size_t) i]->setBounds (header.removeFromRight (150).reduced (0, 1));
            c.removeFromTop (6);
            sequenceEditors[(size_t) i]->setBounds (c);
        }

        area.removeFromTop (12);
        pattern->setBounds (area);
        auto c = pattern->content();
        auto header = c.removeFromTop (26);
        lengths[2]->setBounds (header.removeFromRight (150).reduced (0, 1));
        c.removeFromTop (6);
        patternEditor->setBounds (c);
    }

    void SequencerPage::bindPart (int newPart)
    {
        part = newPart;
        Page::bindPart (newPart);
        arpReader->bindPart (newPart);
        for (auto& editor : sequenceEditors)
            editor->bindPart (newPart);
        patternEditor->bindPart (newPart);
        poll();
        repaint();
    }

    void SequencerPage::poll()
    {
        Page::poll();
        for (auto& editor : sequenceEditors)
            editor->poll();
        patternEditor->poll();
        const int arpStep = processor.currentStep (part, 3);
        const bool changed = arpReader->poll();
        if (changed)
        {
            const bool arpeggio = arpReader->value (0) == ambika::plugin::ARP_SEQUENCER_MODE_ARPEGGIATOR;
            direction->setEnabled (arpeggio);
            octaves->setEnabled (arpeggio);
            rhythm->setEnabled (arpeggio);
            info->repaint();
        }
        if (changed || arpStep != shownArpStep)
        {
            shownArpStep = arpStep;
            arp->repaint (rhythmArea.expanded (4));
        }
    }

    void SequencerPage::paintArp (juce::Graphics& g)
    {
        // The rhythm: 16 steps, filled where the arpeggiator plays.
        const auto bits = ambika::plugin::arpeggiator_patterns[juce::jlimit (0, (int) ambika::plugin::kNumArpeggiatorPatterns - 1,
                                                                              arpReader->value (1))];
        const bool arpeggio = arpReader->value (0) == ambika::plugin::ARP_SEQUENCER_MODE_ARPEGGIATOR;
        const auto area = rhythmArea.toFloat();
        g.setColour (Palette::faintText);
        g.setFont (juce::Font (eurekaFont (11.0f)));
        g.drawText ("plays on the lit steps", area.withY (area.getY() - 17.0f).withHeight (14.0f), juce::Justification::centredLeft);
        const auto accent = accentOf (*this);
        for (int step = 0; step < kNumSteps; ++step)
        {
            const auto c = column (area, step).reduced (2.0f, 4.0f);
            const bool on = bits & (1 << step);
            g.setColour (on ? accent.withAlpha (arpeggio ? 0.9f : 0.35f) : Palette::panelRaised.withAlpha (0.7f));
            g.fillRoundedRectangle (c, 3.0f);
            if (arpeggio && step == shownArpStep)
            {
                g.setColour (Palette::text);
                g.drawRoundedRectangle (c.expanded (1.5f), 4.0f, 1.5f);
            }
        }
    }

    void SequencerPage::paintInfo (juce::Graphics& g)
    {
        juce::String title, text;
        switch (arpReader->value (0))
        {
            case ambika::plugin::ARP_SEQUENCER_MODE_ARPEGGIATOR:
                title = "Arpeggio";
                text = "Held keys play one at a time, in the chosen direction and rhythm, over 1-4 octaves. "
                       "Chord plays all held keys on every step.";
                break;
            case ambika::plugin::ARP_SEQUENCER_MODE_NOTE:
                title = "Note pattern";
                text = "Hold a key: the pattern below plays, transposed so that 60 is the key held.";
                break;
            default:
                title = "Keys";
                text = "Keys play notes directly. Sequences 1 and 2 still run as modulation sources (seq. 1, "
                       "seq. 2 in the Mod page), as does the arpeggiator rhythm (arp).";
                break;
        }
        text += "\n\nThe steps follow the host's tempo and song position while it plays, "
                "and run free (from the first key) while it is stopped.";
        auto area = info->content().reduced (4, 2);
        g.setColour (Palette::text);
        g.setFont (juce::Font (eurekaFont (14.0f, juce::Font::bold)));
        g.drawText (title, area.removeFromTop (20), juce::Justification::topLeft);
        g.setColour (Palette::dimText);
        g.setFont (juce::Font (eurekaFont (12.0f)));
        g.drawFittedText (text, area, juce::Justification::topLeft, 8);
    }
}
