#include "PresetBrowser.h"

#include "EurekaLookAndFeel.h"
#include "Skin.h"

namespace ambika_plugin
{
    namespace
    {
        juce::Colour accentOf (const juce::Component& c)
        {
            return c.findColour (juce::Slider::rotarySliderFillColourId);
        }

        void styleLabel (juce::Label& label, const juce::String& text, float size = 12.0f,
                         juce::Colour colour = Palette::dimText)
        {
            label.setText (text, juce::dontSendNotification);
            label.setFont (juce::Font (juce::FontOptions (size)));
            label.setColour (juce::Label::textColourId, colour);
        }

        void styleEditor (juce::TextEditor& editor, const juce::String& placeholder)
        {
            editor.setFont (juce::Font (juce::FontOptions (14.0f)));
            editor.setTextToShowWhenEmpty (placeholder, Palette::faintText);
            editor.setColour (juce::TextEditor::backgroundColourId, Palette::background);
            editor.setColour (juce::TextEditor::textColourId, Palette::text);
            editor.setColour (juce::TextEditor::outlineColourId, Palette::outline);
            editor.setIndents (8, 6);
            editor.setColour (juce::TextEditor::highlightColourId, Palette::outline.brighter (0.3f));
        }

        std::unique_ptr<juce::TextButton> makeChip (const juce::String& text)
        {
            auto chip = std::make_unique<juce::TextButton> (text);
            chip->setClickingTogglesState (true);
            return chip;
        }

        // Lays chips out left to right, wrapping; returns the height used.
        int layoutChips (std::vector<std::unique_ptr<juce::TextButton>>& chips, juce::Rectangle<int> area,
                         int height = 26)
        {
            int x = area.getX(), y = area.getY();
            juce::Font font (juce::FontOptions (13.0f, juce::Font::bold));
            for (auto& chip : chips)
            {
                const int w = juce::GlyphArrangement::getStringWidthInt (font, chip->getButtonText()) + 22;
                if (x + w > area.getRight() && x > area.getX())
                {
                    x = area.getX();
                    y += height + 6;
                }
                chip->setBounds (x, y, w, height);
                x += w + 6;
            }
            return chips.empty() ? 0 : y + height - area.getY();
        }

        juce::StringArray parseTags (const juce::String& text)
        {
            auto tags = juce::StringArray::fromTokens (text, ",", "\"");
            tags.trim();
            tags.removeEmptyStrings();
            tags.removeDuplicates (true);
            return tags;
        }
    }

    //==============================================================================
    PresetDialog::PresetDialog (PresetContext& c) : context (c)
    {
        setWantsKeyboardFocus (true);
        styleLabel (title, "Save preset", 18.0f, Palette::text);
        title.setFont (juce::Font (juce::FontOptions (18.0f, juce::Font::bold)));
        styleLabel (nameLabel, "Name");
        styleLabel (tagsLabel, "Tags");
        styleLabel (extraTagsLabel, "Other tags");
        styleLabel (authorLabel, "Author");
        styleLabel (commentLabel, "Comment");
        styleLabel (warning, {}, 12.0f, juce::Colour (0xfff0a33a));
        for (auto* l : { &title, &nameLabel, &tagsLabel, &extraTagsLabel, &authorLabel, &commentLabel, &warning })
            addAndMakeVisible (*l);

        styleEditor (name, "Preset name");
        styleEditor (extraTags, "comma separated, e.g. acid, dark");
        styleEditor (author, "optional");
        styleEditor (comment, "optional");
        name.onTextChange = [this] { updateWarning(); };
        name.onReturnKey = [this] { confirm(); };
        for (auto* e : { &name, &extraTags, &author, &comment })
            addAndMakeVisible (*e);

        for (const auto& tag : PresetManager::standardTags())
        {
            tagButtons.push_back (makeChip (tag));
            addAndMakeVisible (*tagButtons.back());
        }

        cancelButton.onClick = [this] { close(); };
        confirmButton.onClick = [this] { confirm(); };
        confirmButton.setToggleState (true, juce::dontSendNotification);  // Accent fill.
        addAndMakeVisible (cancelButton);
        addAndMakeVisible (confirmButton);
    }

    void PresetDialog::openForSave()
    {
        mode = Mode::Save;
        const int part = context.editedPart();
        auto& presets = context.presets;
        title.setText ("Save part " + juce::String (part + 1) + " as preset", juce::dontSendNotification);
        const auto current = presets.presetName (part);
        name.setText (current == "Init" ? juce::String() : current, juce::dontSendNotification);
        juce::StringArray extra;
        const auto tags = presets.presetTags (part);
        for (auto& chip : tagButtons)
            chip->setToggleState (tags.contains (chip->getButtonText(), true), juce::dontSendNotification);
        for (const auto& tag : tags)
            if (! PresetManager::standardTags().contains (tag, true))
                extra.add (tag);
        extraTags.setText (extra.joinIntoString (", "), juce::dontSendNotification);
        author.setText (presets.presetAuthor (part), juce::dontSendNotification);
        comment.setText (presets.presetComment (part), juce::dontSendNotification);
        for (juce::Component* c : { (juce::Component*) &tagsLabel, (juce::Component*) &extraTagsLabel,
                                    (juce::Component*) &authorLabel, (juce::Component*) &commentLabel,
                                    (juce::Component*) &extraTags, (juce::Component*) &author,
                                    (juce::Component*) &comment })
            c->setVisible (true);
        for (auto& chip : tagButtons)
            chip->setVisible (true);
        setVisible (true);
        toFront (true);
        resized();
        updateWarning();
        name.grabKeyboardFocus();
        name.selectAll();
    }

    void PresetDialog::openForRename (const PresetInfo& preset)
    {
        mode = Mode::Rename;
        renaming = preset;
        title.setText ("Rename preset", juce::dontSendNotification);
        name.setText (preset.name, juce::dontSendNotification);
        for (juce::Component* c : { (juce::Component*) &tagsLabel, (juce::Component*) &extraTagsLabel,
                                    (juce::Component*) &authorLabel, (juce::Component*) &commentLabel,
                                    (juce::Component*) &extraTags, (juce::Component*) &author,
                                    (juce::Component*) &comment })
            c->setVisible (false);
        for (auto& chip : tagButtons)
            chip->setVisible (false);
        setVisible (true);
        toFront (true);
        resized();
        updateWarning();
        name.grabKeyboardFocus();
        name.selectAll();
    }

    juce::StringArray PresetDialog::selectedTags() const
    {
        juce::StringArray tags;
        for (auto& chip : tagButtons)
            if (chip->getToggleState())
                tags.add (chip->getButtonText());
        for (const auto& tag : parseTags (extraTags.getText()))
            tags.addIfNotAlreadyThere (tag, true);
        return tags;
    }

    void PresetDialog::updateWarning()
    {
        const auto file = context.presets.fileForName (name.getText());
        const bool clash = name.getText().trim().isNotEmpty() && file.existsAsFile()
                           && ! (mode == Mode::Rename && file == renaming.file);
        if (mode == Mode::Save)
        {
            warning.setText (clash ? "A preset with this name exists. Saving will replace it." : juce::String(),
                             juce::dontSendNotification);
            confirmButton.setButtonText (clash ? "Replace" : "Save");
        }
        else
        {
            warning.setText (clash ? "Another preset already has this name." : juce::String(),
                             juce::dontSendNotification);
            confirmButton.setButtonText ("Rename");
        }
        confirmButton.setEnabled (name.getText().trim().isNotEmpty() && ! (mode == Mode::Rename && clash));
    }

    void PresetDialog::confirm()
    {
        if (! confirmButton.isEnabled())
            return;
        auto& presets = context.presets;
        juce::Result result = juce::Result::ok();
        if (mode == Mode::Save)
        {
            result = presets.save (context.editedPart(), name.getText(), selectedTags(), author.getText(),
                                   comment.getText(), true);
            if (result.wasOk())
                context.notify ("Saved \"" + name.getText().trim() + "\"", false);
        }
        else
        {
            result = presets.rename (renaming.file, name.getText());
            if (result.wasOk())
                context.notify ("Renamed to \"" + name.getText().trim() + "\"", false);
        }
        if (result.failed())
        {
            warning.setText (result.getErrorMessage(), juce::dontSendNotification);
            return;
        }
        context.presetsChanged();
        close();
    }

    void PresetDialog::close()
    {
        setVisible (false);
    }

    bool PresetDialog::keyPressed (const juce::KeyPress& key)
    {
        if (key == juce::KeyPress::escapeKey)
        {
            close();
            return true;
        }
        return false;
    }

    juce::Rectangle<int> PresetDialog::card() const
    {
        const int height = mode == Mode::Save ? 410 : 200;
        return getLocalBounds().withSizeKeepingCentre (580, height);
    }

    void PresetDialog::paint (juce::Graphics& g)
    {
        g.fillAll (juce::Colours::black.withAlpha (0.6f));
        const auto c = card().toFloat();
        g.setColour (Palette::panel);
        g.fillRoundedRectangle (c, 10.0f);
        g.setColour (accentOf (*this).withAlpha (0.6f));
        g.drawRoundedRectangle (c.reduced (0.5f), 10.0f, 1.0f);
    }

    void PresetDialog::resized()
    {
        auto c = card().reduced (24, 18);
        title.setBounds (c.removeFromTop (30));
        c.removeFromTop (8);
        auto row = [&c] (juce::Label& label, juce::Component& field, int height = 30)
        {
            auto r = c.removeFromTop (height);
            label.setBounds (r.removeFromLeft (96));
            field.setBounds (r);
            c.removeFromTop (10);
        };
        row (nameLabel, name);
        if (mode == Mode::Save)
        {
            auto tagsArea = c.removeFromTop (64);
            tagsLabel.setBounds (tagsArea.removeFromLeft (96).withHeight (26));
            layoutChips (tagButtons, tagsArea);
            c.removeFromTop (8);
            row (extraTagsLabel, extraTags);
            row (authorLabel, author);
            row (commentLabel, comment);
        }
        warning.setBounds (c.removeFromTop (22).withTrimmedLeft (96));
        auto buttons = c.removeFromBottom (32);
        confirmButton.setBounds (buttons.removeFromRight (110));
        buttons.removeFromRight (10);
        cancelButton.setBounds (buttons.removeFromRight (100));
    }

    //==============================================================================
    PresetBar::PresetBar (PresetContext& c, PresetDialog& d, std::function<void()> toggleBrowser)
        : context (c), dialog (d)
    {
        previous.setTooltip ("Previous preset");
        next.setTooltip ("Next preset");
        browse.setTooltip ("Browse, load, rename and delete presets");
        save.setTooltip ("Save over the current preset (or name a new one)");
        saveAs.setTooltip ("Save as a new preset with a name and tags");
        init.setTooltip ("Reset this part to the initial patch");

        previous.onClick = [this] { step (-1); };
        next.onClick = [this] { step (1); };
        browse.onClick = std::move (toggleBrowser);
        save.onClick = [this] { quickSave(); };
        saveAs.onClick = [this] { dialog.openForSave(); };
        init.onClick = [this]
        {
            // Two clicks: Init discards the part's current sound.
            if (! initArmed)
            {
                initArmed = true;
                init.setButtonText ("Sure?");
                juce::Component::SafePointer<PresetBar> safe (this);
                juce::Timer::callAfterDelay (2500, [safe]
                {
                    if (safe != nullptr && safe->initArmed)
                    {
                        safe->initArmed = false;
                        safe->init.setButtonText ("Init");
                    }
                });
                return;
            }
            initArmed = false;
            init.setButtonText ("Init");
            context.presets.initialise (context.editedPart());
            context.notify ("Part " + juce::String (context.editedPart() + 1) + " initialised", false);
        };
        for (auto* b : { &previous, &next, &browse, &save, &saveAs, &init })
            addAndMakeVisible (*b);
    }

    void PresetBar::step (int direction)
    {
        const auto file = context.presets.neighbour (context.editedPart(), direction);
        if (file == juce::File())
        {
            context.notify ("No presets saved yet. Use Save as... to create one.", false);
            return;
        }
        const auto result = context.presets.load (context.editedPart(), file);
        if (result.failed())
            context.notify (result.getErrorMessage(), true);
    }

    void PresetBar::quickSave()
    {
        auto& presets = context.presets;
        const int part = context.editedPart();
        if (! presets.presetFile (part).existsAsFile())
        {
            dialog.openForSave();
            return;
        }
        const auto result = presets.save (part, presets.presetName (part), presets.presetTags (part),
                                          presets.presetAuthor (part), presets.presetComment (part), true);
        if (result.failed())
            context.notify (result.getErrorMessage(), true);
        else
        {
            context.notify ("Saved \"" + presets.presetName (part) + "\"", false);
            context.presetsChanged();
        }
    }

    void PresetBar::poll()
    {
        const int part = context.editedPart();
        const auto name = context.presets.presetName (part);
        const auto tags = context.presets.presetTags (part);
        const bool modified = context.presets.isModified (part);
        if (part != shownPart || name != shownName || tags != shownTags || modified != shownModified)
        {
            shownPart = part;
            shownName = name;
            shownTags = tags;
            shownModified = modified;
            repaint();
        }
    }

    void PresetBar::paint (juce::Graphics& g)
    {
        const auto barColour = Palette::theme() == Palette::Theme::Dark ? Palette::panel.darker (0.25f)
                                                                         : Palette::panelRaised.darker (0.02f);
        g.setColour (barColour.withMultipliedAlpha (currentSkin().settings.headerOpacity));
        g.fillRect (getLocalBounds());
        g.setColour (Palette::outline);
        g.drawHorizontalLine (getHeight() - 1, 0.0f, (float) getWidth());

        const auto colour = Palette::part (juce::jmax (0, shownPart));
        auto area = getLocalBounds().reduced (20, 0);
        auto chip = area.removeFromLeft (64).withSizeKeepingCentre (64, 22).toFloat();
        g.setColour (colour.withAlpha (0.18f));
        g.fillRoundedRectangle (chip, 4.0f);
        g.setColour (colour);
        g.setFont (juce::Font (juce::FontOptions (11.5f, juce::Font::bold)));
        g.drawText ("PART " + juce::String (shownPart + 1), chip, juce::Justification::centred);

        area.removeFromLeft (12);
        auto text = area.withRight (previous.getX() - 12);
        g.setFont (juce::Font (juce::FontOptions (17.0f, juce::Font::bold)));
        g.setColour (Palette::text);
        const auto label = shownName + (shownModified ? " *" : "");
        const int nameWidth = juce::jmin (text.getWidth(),
                                          juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), label) + 6);
        g.drawText (label, text.removeFromLeft (nameWidth), juce::Justification::centredLeft, true);
        if (shownModified)
        {
            g.setFont (juce::Font (juce::FontOptions (11.0f)));
            g.setColour (Palette::dimText);
            g.drawText ("modified", text.removeFromLeft (64), juce::Justification::centredLeft);
        }
        if (! shownTags.isEmpty())
        {
            text.removeFromLeft (8);
            g.setFont (juce::Font (juce::FontOptions (11.5f)));
            g.setColour (colour.withAlpha (0.8f));
            g.drawText (shownTags.joinIntoString ("  ·  "), text, juce::Justification::centredLeft, true);
        }
    }

    void PresetBar::resized()
    {
        auto area = getLocalBounds().reduced (16, 6);
        init.setBounds (area.removeFromRight (64));
        area.removeFromRight (6);
        saveAs.setBounds (area.removeFromRight (92));
        area.removeFromRight (6);
        save.setBounds (area.removeFromRight (64));
        area.removeFromRight (14);
        browse.setBounds (area.removeFromRight (84));
        area.removeFromRight (14);
        next.setBounds (area.removeFromRight (30));
        area.removeFromRight (4);
        previous.setBounds (area.removeFromRight (30));
    }

    //==============================================================================
    PresetBrowserPage::PresetBrowserPage (PresetContext& c, PresetDialog& d) : context (c), dialog (d)
    {
        styleEditor (search, "Search names, tags, authors");
        search.onTextChange = [this] { applyFilter(); };
        addAndMakeVisible (search);

        list.setRowHeight (40);
        list.setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
        list.setColour (juce::ListBox::outlineColourId, juce::Colours::transparentBlack);
        addAndMakeVisible (list);

        load.onClick = [this] { loadSelected(); };
        rename.onClick = [this]
        {
            if (auto* p = selected())
                dialog.openForRename (*p);
        };
        remove.onClick = [this]
        {
            auto* p = selected();
            if (p == nullptr)
                return;
            if (! deleteArmed)
            {
                deleteArmed = true;
                remove.setButtonText ("Sure?");
                return;
            }
            const auto preset = *p;
            const auto result = context.presets.remove (preset.file);
            context.notify (result.wasOk() ? "Moved \"" + preset.name + "\" to the Trash" : result.getErrorMessage(),
                            result.failed());
            refresh();
        };
        reveal.onClick = [this]
        {
            auto dir = context.presets.getDirectory();
            dir.createDirectory();
            dir.revealToUser();
        };
        rescan.onClick = [this] { refresh(); };
        for (auto* b : { &load, &rename, &remove, &reveal, &rescan })
            addAndMakeVisible (*b);
        load.setToggleState (true, juce::dontSendNotification);  // Accent fill.
    }

    void PresetBrowserPage::visibilityChanged()
    {
        if (isVisible())
            refresh();
    }

    void PresetBrowserPage::refresh()
    {
        const auto previouslySelected = selected() != nullptr ? selected()->file : juce::File();
        all = context.presets.scan();
        rebuildTagButtons();
        applyFilter();
        for (size_t i = 0; i < filtered.size(); ++i)
            if (filtered[i].file == previouslySelected
                || (previouslySelected == juce::File()
                    && filtered[i].file == context.presets.presetFile (context.editedPart())))
                list.selectRow ((int) i);
    }

    void PresetBrowserPage::rebuildTagButtons()
    {
        juce::StringArray tags (PresetManager::standardTags());
        for (const auto& preset : all)
            for (const auto& tag : preset.tags)
                if (! tags.contains (tag, true))
                    tags.add (tag);
        for (int i = activeTags.size(); --i >= 0;)
            if (! tags.contains (activeTags[i], true))
                activeTags.remove (i);

        tagButtons.clear();
        for (const auto& tag : tags)
        {
            auto chip = makeChip (tag);
            chip->setToggleState (activeTags.contains (tag, true), juce::dontSendNotification);
            auto* raw = chip.get();
            chip->onClick = [this, raw]
            {
                if (raw->getToggleState())
                    activeTags.addIfNotAlreadyThere (raw->getButtonText());
                else
                    activeTags.removeString (raw->getButtonText(), true);
                applyFilter();
            };
            addAndMakeVisible (*chip);
            tagButtons.push_back (std::move (chip));
        }
        resized();
    }

    void PresetBrowserPage::applyFilter()
    {
        const auto query = search.getText().trim();
        filtered.clear();
        for (const auto& preset : all)
        {
            bool matches = true;
            for (const auto& tag : activeTags)
                matches = matches && preset.tags.contains (tag, true);
            if (matches && query.isNotEmpty())
                matches = preset.name.containsIgnoreCase (query) || preset.author.containsIgnoreCase (query)
                          || preset.comment.containsIgnoreCase (query)
                          || preset.tags.joinIntoString (" ").containsIgnoreCase (query);
            if (matches)
                filtered.push_back (preset);
        }
        list.updateContent();
        list.deselectAllRows();
        deleteArmed = false;
        remove.setButtonText ("Delete");
        repaint();
    }

    const PresetInfo* PresetBrowserPage::selected() const
    {
        const int row = list.getSelectedRow();
        return row >= 0 && row < (int) filtered.size() ? &filtered[(size_t) row] : nullptr;
    }

    void PresetBrowserPage::loadSelected()
    {
        if (auto* p = selected())
        {
            const auto result = context.presets.load (context.editedPart(), p->file);
            context.notify (result.wasOk() ? "Loaded \"" + p->name + "\" into part " + juce::String (context.editedPart() + 1)
                                           : result.getErrorMessage(),
                            result.failed());
            list.repaint();
        }
    }

    int PresetBrowserPage::getNumRows() { return (int) filtered.size(); }

    void PresetBrowserPage::paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool isSelected)
    {
        if (row < 0 || row >= (int) filtered.size())
            return;
        const auto& preset = filtered[(size_t) row];
        auto area = juce::Rectangle<int> (width, height).reduced (2, 2);
        if (isSelected)
        {
            g.setColour (accentOf (*this).withAlpha (0.18f));
            g.fillRoundedRectangle (area.toFloat(), 5.0f);
        }
        else if (row % 2 == 0)
        {
            g.setColour (Palette::panelRaised.withAlpha (0.5f));
            g.fillRoundedRectangle (area.toFloat(), 5.0f);
        }
        const bool loaded = preset.file == context.presets.presetFile (context.editedPart());
        if (loaded)
        {
            g.setColour (accentOf (*this));
            g.fillRoundedRectangle ((float) area.getX() + 4.0f, (float) area.getCentreY() - 8.0f, 3.0f, 16.0f, 1.5f);
        }
        area.removeFromLeft (14);
        g.setColour (Palette::text);
        g.setFont (juce::Font (juce::FontOptions (14.0f, juce::Font::bold)));
        g.drawText (preset.name, area.removeFromLeft (240), juce::Justification::centredLeft, true);
        g.setFont (juce::Font (juce::FontOptions (11.5f)));
        g.setColour (Palette::dimText);
        g.drawText (preset.author, area.removeFromRight (110).withTrimmedRight (8), juce::Justification::centredRight, true);
        g.setColour (accentOf (*this).withAlpha (0.85f));
        g.drawText (preset.tags.joinIntoString ("  ·  "), area, juce::Justification::centredLeft, true);
    }

    void PresetBrowserPage::selectedRowsChanged (int)
    {
        deleteArmed = false;
        remove.setButtonText ("Delete");
        const bool any = selected() != nullptr;
        load.setEnabled (any);
        rename.setEnabled (any);
        remove.setEnabled (any);
        repaint();
    }

    void PresetBrowserPage::listBoxItemDoubleClicked (int, const juce::MouseEvent&) { loadSelected(); }
    void PresetBrowserPage::returnKeyPressed (int) { loadSelected(); }

    namespace
    {
        constexpr int kSideWidth = 280;
        constexpr int kDetailWidth = 320;
        constexpr int kGap = 16;
    }

    void PresetBrowserPage::paint (juce::Graphics& g)
    {
        auto area = getLocalBounds().reduced (12);
        auto paintPanel = [&g] (juce::Rectangle<int> r, const juce::String& title, juce::Colour accent)
        {
            const float opacity = currentSkin().settings.panelOpacity;
            g.setColour (Palette::panel.withMultipliedAlpha (opacity));
            g.fillRoundedRectangle (r.toFloat(), 8.0f);
            g.setColour (Palette::outline.withMultipliedAlpha (opacity));
            g.drawRoundedRectangle (r.toFloat().reduced (0.5f), 8.0f, 1.0f);
            g.setColour (accent);
            g.fillEllipse ((float) r.getX() + 12.0f, (float) r.getY() + 11.0f, 6.0f, 6.0f);
            g.setColour (Palette::text);
            g.setFont (juce::Font (juce::FontOptions (12.0f, juce::Font::bold)).withExtraKerningFactor (0.08f));
            g.drawText (title.toUpperCase(), r.getX() + 24, r.getY() + 5, r.getWidth() - 30, 18,
                        juce::Justification::centredLeft);
        };
        const auto accent = accentOf (*this);
        const auto side = area.removeFromLeft (kSideWidth);
        area.removeFromLeft (kGap);
        const auto detail = area.removeFromRight (kDetailWidth);
        area.removeFromRight (kGap);
        paintPanel (side, "Find", accent);
        paintPanel (area, juce::String ((int) filtered.size()) + " of " + juce::String ((int) all.size()) + " presets",
                    accent);
        paintPanel (detail, "Preset", accent);

        g.setFont (juce::Font (juce::FontOptions (11.5f)));
        g.setColour (Palette::dimText);
        g.drawText ("Tags (all selected must match)", side.getX() + 14, side.getY() + 72, side.getWidth() - 28, 16,
                    juce::Justification::centredLeft);

        if (all.empty())
        {
            g.setColour (Palette::dimText);
            g.setFont (juce::Font (juce::FontOptions (13.0f)));
            g.drawFittedText ("No presets yet.\n\nShape a sound on the Synth page, then press \"Save as...\" "
                              "in the bar above to name and tag it.\nPresets are stored in "
                              + context.presets.getDirectory().getFullPathName(),
                              area.reduced (30, 60), juce::Justification::centredTop, 8);
        }

        auto d = detail.reduced (16).withTrimmedTop (28);
        if (auto* p = selected())
        {
            g.setColour (Palette::text);
            g.setFont (juce::Font (juce::FontOptions (20.0f, juce::Font::bold)));
            g.drawFittedText (p->name, d.removeFromTop (30), juce::Justification::centredLeft, 1);
            g.setFont (juce::Font (juce::FontOptions (12.5f)));
            auto line = [&] (const juce::String& label, const juce::String& value)
            {
                if (value.isEmpty())
                    return;
                auto r = d.removeFromTop (22);
                g.setColour (Palette::dimText);
                g.drawText (label, r.removeFromLeft (70), juce::Justification::centredLeft);
                g.setColour (Palette::text);
                g.drawText (value, r, juce::Justification::centredLeft, true);
            };
            d.removeFromTop (6);
            line ("Tags", p->tags.joinIntoString (", "));
            line ("Author", p->author);
            line ("Saved", p->modified.toString (true, true, false));
            line ("File", p->file.getFileName());
            if (p->comment.isNotEmpty())
            {
                d.removeFromTop (8);
                g.setColour (Palette::text.withAlpha (0.85f));
                g.drawFittedText (p->comment, d.removeFromTop (80), juce::Justification::topLeft, 5);
            }
        }
        else
        {
            g.setColour (Palette::faintText);
            g.setFont (juce::Font (juce::FontOptions (12.5f)));
            g.drawFittedText ("Select a preset to see its details.\nDouble-click to load it into part "
                                  + juce::String (context.editedPart() + 1) + ".",
                              d.removeFromTop (60), juce::Justification::topLeft, 3);
        }
    }

    void PresetBrowserPage::resized()
    {
        auto area = getLocalBounds().reduced (12);
        auto side = area.removeFromLeft (kSideWidth).reduced (14);
        area.removeFromLeft (kGap);
        auto detail = area.removeFromRight (kDetailWidth).reduced (16);
        area.removeFromRight (kGap);

        side.removeFromTop (24);
        search.setBounds (side.removeFromTop (32));
        side.removeFromTop (34);
        auto buttons = side.removeFromBottom (30);
        reveal.setBounds (buttons.removeFromLeft (buttons.getWidth() / 2 - 4));
        rescan.setBounds (buttons.removeFromRight (buttons.getWidth() - 4));
        layoutChips (tagButtons, side);

        list.setBounds (area.reduced (8).withTrimmedTop (26));

        auto actions = detail.removeFromBottom (34);
        const int w = (actions.getWidth() - 12) / 3;
        load.setBounds (actions.removeFromLeft (w));
        actions.removeFromLeft (6);
        rename.setBounds (actions.removeFromLeft (w));
        actions.removeFromLeft (6);
        remove.setBounds (actions);
    }
}
