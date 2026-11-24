#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <optional>

namespace ambika_plugin
{
    // Optional images and settings that restyle the editor without code.
    //
    // Looked up in this order, file by file:
    //   1. Documents/eureka/skin/   (edit and reopen the plug-in window)
    //   2. plugin/skin/ in the source tree, compiled into the plug-in
    // Anything missing falls back to the built-in drawing.
    //
    //   background.png            whole window, 1200 x 802 (or 2x: 2400 x 1604)
    //   background-synth.png      per-page backgrounds (override background.png)
    //   background-mod.png
    //   background-multi.png
    //   background-presets.png
    //   logo.png                  header wordmark, fitted into 230 x 52
    //   knob.png                  knob filmstrip: square frames stacked
    //                             vertically, first frame = minimum
    //   skin.json                 settings, see Skin::Settings
    struct Skin
    {
        struct Settings
        {
            float panelOpacity = 1.0f;     // 0 = panels invisible, background shows through
            float headerOpacity = 1.0f;    // Header and preset bar
            float displayOpacity = 1.0f;   // Waveform/filter/envelope display backgrounds
            bool knobValueArc = true;      // Draw the coloured value ring around filmstrip knobs
            std::optional<std::array<juce::Colour, 6>> partColours;
        };

        juce::Image background;
        std::array<juce::Image, 5> pageBackgrounds;  // synth, mod, multi, presets, seq
        juce::Image logo;
        juce::Image knob;
        int knobFrames = 0;
        Settings settings;
        juce::String source;  // Where the skin came from, for the user.

        static juce::File userDirectory();
        static Skin load();

        const juce::Image& backgroundFor (int page) const;
        bool isCustom() const { return background.isValid() || logo.isValid() || knob.isValid()
                                       || settings.partColours.has_value(); }
    };

    // The skin in use by the open editor (set by the editor, read while painting).
    const Skin& currentSkin();
    void setCurrentSkin (Skin);
}
