#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <vector>

#include "PresetManager.h"

namespace ambika_plugin
{
    // Shared by the bar, the browser and the dialog: which part is edited and
    // how to report results.
    struct PresetContext
    {
        PresetManager& presets;
        std::function<int()> editedPart;
        std::function<void (const juce::String&, bool isError)> notify;
        std::function<void()> presetsChanged;  // Files were added/renamed/removed.
    };

    // Name / tags / author / comment form, shown over the editor. Used for
    // "Save as" and "Rename".
    class PresetDialog final : public juce::Component
    {
    public:
        enum class Mode { Save, Rename };

        explicit PresetDialog (PresetContext&);
        void openForSave();
        void openForRename (const PresetInfo& preset);

        void paint (juce::Graphics&) override;
        void resized() override;
        bool keyPressed (const juce::KeyPress&) override;

    private:
        void updateWarning();
        void confirm();
        void close();
        juce::StringArray selectedTags() const;
        juce::Rectangle<int> card() const;

        PresetContext& context;
        Mode mode = Mode::Save;
        PresetInfo renaming;
        juce::Label title, nameLabel, tagsLabel, extraTagsLabel, authorLabel, commentLabel, warning;
        juce::TextEditor name, extraTags, author, comment;
        std::vector<std::unique_ptr<juce::TextButton>> tagButtons;
        juce::TextButton cancelButton { "Cancel" }, confirmButton { "Save" };
    };

    // The strip under the header.
    class PresetBar final : public juce::Component
    {
    public:
        PresetBar (PresetContext&, PresetDialog&, std::function<void()> toggleBrowser);
        void paint (juce::Graphics&) override;
        void resized() override;
        void poll();
        void setBrowserOpen (bool open) { browse.setToggleState (open, juce::dontSendNotification); }

    private:
        void step (int direction);
        void quickSave();

        PresetContext& context;
        PresetDialog& dialog;
        juce::TextButton previous { "<" }, next { ">" }, browse { "Browse" }, save { "Save" },
            saveAs { "Save as..." }, init { "Init" };
        juce::String shownName;
        juce::StringArray shownTags;
        bool shownModified = false;
        int shownPart = -1;
    };

    // Browser page: search, tag filter, list and details.
    class PresetBrowserPage final : public juce::Component, private juce::ListBoxModel
    {
    public:
        PresetBrowserPage (PresetContext&, PresetDialog&);
        void refresh();
        void paint (juce::Graphics&) override;
        void resized() override;
        void visibilityChanged() override;

    private:
        int getNumRows() override;
        void paintListBoxItem (int row, juce::Graphics&, int width, int height, bool selected) override;
        void selectedRowsChanged (int lastRowSelected) override;
        void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override;
        void returnKeyPressed (int row) override;

        void applyFilter();
        void rebuildTagButtons();
        void loadSelected();
        const PresetInfo* selected() const;

        PresetContext& context;
        PresetDialog& dialog;
        std::vector<PresetInfo> all, filtered;
        juce::StringArray activeTags;

        juce::TextEditor search;
        std::vector<std::unique_ptr<juce::TextButton>> tagButtons;
        juce::ListBox list { "presets", this };
        juce::TextButton load { "Load" }, rename { "Rename" }, remove { "Delete" }, reveal { "Show folder" },
            rescan { "Refresh" };
        bool deleteArmed = false;
    };
}
