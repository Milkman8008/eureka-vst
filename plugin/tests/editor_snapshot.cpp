// Development tool: renders the plug-in editor to PNG files, with a chord
// playing so the voice LEDs are lit.
//
// Usage: ambika_editor_snapshot <output base path, e.g. /tmp/editor.png>
//        ambika_editor_snapshot --skin-template <directory>
// Writes <base>-synth.png, -mod.png, -multi.png and -synth-part2.png
// (with a 2-part layout, to show part colours).

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <cstdio>

#include "Controls.h"
#include "EurekaLookAndFeel.h"
#include "PluginEditor.h"
#include "Skin.h"
#include "PluginProcessor.h"

namespace
{
    void render (ambika_plugin::AmbikaAudioProcessor& processor, juce::MidiBuffer midi)
    {
        juce::AudioBuffer<float> buffer (2, 512);
        for (int block = 0; block < 10; ++block)
        {
            processor.processBlock (buffer, midi);
            midi.clear();
        }
    }

    void setParameter (ambika_plugin::AmbikaAudioProcessor& processor, const juce::String& id, float value)
    {
        auto* parameter = processor.parameters().getParameter (id);
        parameter->setValueNotifyingHost (parameter->convertTo0to1 (value));
    }

    void snapshot (juce::Component& editor, const juce::File& base, const juce::String& suffix)
    {
        juce::MessageManager::getInstance()->runDispatchLoopUntil (120);  // Let timers update displays.
        const auto out = base.getSiblingFile (base.getFileNameWithoutExtension() + "-" + suffix + ".png");
        out.deleteFile();
        juce::FileOutputStream stream (out);
        if (stream.openedOk())
        {
            juce::PNGImageFormat().writeImageToStream (
                editor.createComponentSnapshot (editor.getLocalBounds(), true, 1.0f), stream);
            std::printf ("%s\n", out.getFullPathName().toRawUTF8());
        }
    }
}

namespace
{
    void writePng (const juce::Image& image, const juce::File& file)
    {
        file.deleteFile();
        juce::FileOutputStream stream (file);
        if (stream.openedOk() && juce::PNGImageFormat().writeImageToStream (image, stream))
            std::printf ("%s\n", file.getFullPathName().toRawUTF8());
    }

    int writeSkinTemplate (const juce::File& dir)
    {
        using namespace ambika_plugin;
        dir.createDirectory();
        setCurrentSkin ({});

        // Knob body filmstrip: 128 frames, 120 px square, minimum first. The
        // plug-in draws its coloured value ring around it (knobValueArc).
        constexpr int frames = 128, size = 120;
        juce::Image knob (juce::Image::ARGB, size, size * frames, true);
        {
            juce::Graphics g (knob);
            for (int f = 0; f < frames; ++f)
            {
                const float position = (float) f / (frames - 1);
                const float angle = juce::degreesToRadians (225.0f) + position * juce::degreesToRadians (270.0f);
                const juce::Point<float> centre (size * 0.5f, (float) (f * size) + size * 0.5f);
                const float radius = size * 0.5f - 4.0f;
                g.setColour (Palette::panelRaised);
                g.fillEllipse (juce::Rectangle<float> (radius * 2, radius * 2).withCentre (centre));
                g.setColour (Palette::outline);
                g.drawEllipse (juce::Rectangle<float> (radius * 2, radius * 2).withCentre (centre), 2.0f);
                g.setColour (Palette::text);
                g.drawLine ({ centre.getPointOnCircumference (radius * 0.25f, angle),
                              centre.getPointOnCircumference (radius * 0.82f, angle) }, 6.0f);
            }
        }
        writePng (knob, dir.getChildFile ("knob.png"));

        // Logo, 2x of the 230 x 52 header slot.
        juce::Image logo (juce::Image::ARGB, 460, 104, true);
        {
            juce::Graphics g (logo);
            g.setFont (juce::Font (ambika_plugin::eurekaFont (56.0f, juce::Font::bold)));
            const int w = juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), "eureka");
            g.setColour (Palette::text);
            g.drawText ("eureka", 8, 4, w + 8, 72, juce::Justification::centredLeft);
            g.setColour (Palette::part (0));
            g.drawText ("!", 10 + w, 4, 40, 72, juce::Justification::centredLeft);
            g.setColour (Palette::faintText);
            g.setFont (juce::Font (ambika_plugin::eurekaFont (21.0f)));
            g.drawText ("Tootable Instruments", 10, 74, 440, 28, juce::Justification::centredLeft);
        }
        writePng (logo, dir.getChildFile ("logo.png"));

        // Layout guides (full UI) and blank panel canvases, per page, at 2x.
        ambika_plugin::AmbikaAudioProcessor processor;
        processor.presets().setDirectory (juce::File::createTempFile ("eureka-template-presets"));
        processor.setPlayConfigDetails (0, 2, 48000.0, 512);
        processor.prepareToPlay (48000.0, 512);
        const char* pages[] = { "synth", "mod", "multi", "presets" };
        {
            std::unique_ptr<AmbikaAudioProcessorEditor> editor (
                dynamic_cast<AmbikaAudioProcessorEditor*> (processor.createEditor()));
            for (int page = 0; page < 4; ++page)
            {
                editor->showPage (page);
                juce::MessageManager::getInstance()->runDispatchLoopUntil (60);
                writePng (editor->createComponentSnapshot (editor->getLocalBounds(), true, 2.0f),
                          dir.getChildFile ("guide-" + juce::String (pages[page]) + ".png"));
            }
            processor.editorBeingDeleted (editor.get());
        }
        // Empty canvas at 2x: window background with the header and preset
        // bar strips. Panels, labels and controls are drawn by the plug-in
        // on top; use the guides to place artwork around them.
        juce::Image canvas (juce::Image::ARGB, 2400, 1604, true);
        {
            juce::Graphics g (canvas);
            g.fillAll (Palette::background);
            g.setColour (Palette::panel);
            g.fillRect (0, 0, 2400, 128);
            g.setColour (Palette::panel.darker (0.25f));
            g.fillRect (0, 128, 2400, 84);
            g.setColour (Palette::outline);
            g.fillRect (0, 126, 2400, 2);
            g.fillRect (0, 210, 2400, 2);
        }
        writePng (canvas, dir.getChildFile ("background-template.png"));

        dir.getChildFile ("skin.json").replaceWithText (R"({
  "panelOpacity": 1.0,
  "headerOpacity": 1.0,
  "displayOpacity": 1.0,
  "knobValueArc": true,
  "partColours": ["#f0a33a", "#3cc8b4", "#f0647a", "#a6d84a", "#5aa8f0", "#b08cf5"]
}
)");
        dir.getChildFile ("README.txt").replaceWithText (
            "eureka! skin template\n"
            "=====================\n\n"
            "Copy the files you change into Documents/eureka/skin/ and reopen the plug-in window.\n"
            "To ship a skin inside the plug-in, put them in plugin/skin/ and rebuild.\n"
            "Any file left out falls back to the built-in look.\n\n"
            "background.png        Whole window. Design size 1200 x 802; make it 2400 x 1604 for sharp\n"
            "                      Retina display. Used on every page unless a page has its own:\n"
            "background-synth.png, background-mod.png, background-multi.png, background-presets.png\n"
            "                      Start from background-template.png (empty window) and put\n"
            "                      guide-<page>.png (the whole UI) on a layer above to line things up.\n"
            "                      Panels, labels, knobs and the header controls are drawn by the\n"
            "                      plug-in on top of the background.\n"
            "                      To let the background show through the panels, lower\n"
            "                      panelOpacity in skin.json (0 = no panel boxes at all).\n\n"
            "logo.png              Header wordmark, fitted into 230 x 52 (here 460 x 104 for Retina).\n\n"
            "knob.png              Knob filmstrip: square frames stacked vertically, first frame =\n"
            "                      minimum, last = maximum. Any frame size and count (this one: 128\n"
            "                      frames of 120 x 120). With knobValueArc true the plug-in draws the\n"
            "                      coloured value ring around it; set it false if your strip draws\n"
            "                      its own ring.\n\n"
            "skin.json             panelOpacity / headerOpacity / displayOpacity (0..1),\n"
            "                      knobValueArc (true/false), partColours (six #rrggbb colours).\n");
        std::printf ("%s\n", dir.getChildFile ("README.txt").getFullPathName().toRawUTF8());
        return 0;
    }
}

int main (int argc, char** argv)
{
    if (argc < 2)
    {
        std::printf ("usage: %s <output.png> | --skin-template <directory>\n", argv[0]);
        return 2;
    }
    juce::ScopedJuceInitialiser_GUI juceInit;
    if (juce::String (argv[1]) == "--skin-template" && argc >= 3)
        return writeSkinTemplate (juce::File (juce::String (argv[2])));

    ambika_plugin::AmbikaAudioProcessor processor;
    // Never touch the user's real preset folder.
    const auto presetDir = juce::File::createTempFile ("eureka-snapshot-presets");
    presetDir.createDirectory();
    processor.presets().setDirectory (presetDir);
    processor.setPlayConfigDetails (0, 2, 48000.0, 512);
    processor.prepareToPlay (48000.0, 512);

    juce::MidiBuffer chord;
    for (int note : { 48, 55, 60, 64 })
        chord.addEvent (juce::MidiMessage::noteOn (1, note, (juce::uint8) 100), 0);
    render (processor, chord);

    std::unique_ptr<ambika_plugin::AmbikaAudioProcessorEditor> editor (
        dynamic_cast<ambika_plugin::AmbikaAudioProcessorEditor*> (processor.createEditor()));
    const juce::File base { juce::String (argv[1]) };

    // Render the theme's own colours, whatever highlights this machine's
    // user has chosen (not saved: only the colour pop-up writes them).
    for (int part = 0; part < ambika_plugin::kNumParts; ++part)
        ambika_plugin::Palette::setPartOverride (part, {});
    editor->highlightsChanged();

    const char* pages[] = { "synth", "mod", "multi" };
    for (int page = 0; page < 3; ++page)
    {
        editor->showPage (page);
        snapshot (*editor, base, pages[page]);
    }

    // Two parts: voices 1-3 on part 1, 4-6 on part 2, a different patch on
    // part 2, notes on both channels.
    for (int v = 0; v < 6; ++v)
        setParameter (processor, ambika_plugin::voicePartId (v), v < 3 ? 1.0f : 2.0f);
    setParameter (processor, ambika_plugin::kVoiceModeId, 1.0f);  // cyclic
    setParameter (processor, ambika_plugin::kUnisonId, 2.0f);
    setParameter (processor, "p2_osc1_shape", 21.0f);  // male
    setParameter (processor, "p2_filter_mode", 1.0f);
    setParameter (processor, "p2_filter_resonance", 50.0f);
    setParameter (processor, "p2_env1_attack", 60.0f);
    juce::MidiBuffer twoParts;
    twoParts.addEvent (juce::MidiMessage::noteOn (1, 48, (juce::uint8) 100), 0);
    twoParts.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
    render (processor, twoParts);

    setParameter (processor, ambika_plugin::partKeyLowId (1), 60.0f);  // Part 2 from C3 up.
    editor->editPart (1);
    editor->showPage (0);
    snapshot (*editor, base, "synth-part2");
    editor->showPage (2);
    snapshot (*editor, base, "multi-2parts");

    // Light mode (toggled back afterwards: the toggle saves the preference).
    editor->toggleTheme();
    editor->showPage (0);
    snapshot (*editor, base, "light-synth");
    editor->showPage (2);
    snapshot (*editor, base, "light-multi");
    editor->toggleTheme();

    // A custom highlight colour for the edited part (not saved: the
    // preference is only written by the colour pop-up).
    ambika_plugin::Palette::setPartOverride (1, juce::Colour (0xffe0457b));
    editor->highlightsChanged();
    editor->showPage (0);
    snapshot (*editor, base, "highlight");
    ambika_plugin::Palette::setPartOverride (1, {});
    editor->highlightsChanged();

    // Presets: a few saved sounds, part 2 modified after loading.
    auto& presets = processor.presets();
    presets.save (0, "Warm Saw Keys", { "Keys", "Poly" }, "eureka", "Default-ish saw, slow filter.", true);
    presets.save (1, "Vox Choir", { "Vocal", "Pad" }, "eureka", "Male formant wavetable through the BP filter.", true);
    setParameter (processor, "p2_filter_cutoff", 70.0f);
    presets.save (2, "Acid Line", { "Bass", "Mono", "acid" }, "someone", {}, true);
    presets.save (3, "Glass Bell", { "Bell", "FX" }, {}, {}, true);
    setParameter (processor, "p2_filter_resonance", 30.0f);
    editor->showPage (3);
    snapshot (*editor, base, "presets");

    // Part 2 with a Surge filter: Diode Ladder (model 2), 24 dB variant.
    setParameter (processor, "p2_filter_model", 2.0f);
    setParameter (processor, "p2_filter_variant", 3.0f);
    setParameter (processor, "p2_filter_mode", 0.0f);
    setParameter (processor, "p2_filter_cutoff", 60.0f);
    setParameter (processor, "p2_filter_resonance", 45.0f);
    editor->showPage (0);
    snapshot (*editor, base, "synth-surge");
    editor->showPage (0);
    editor->showSaveDialog();
    snapshot (*editor, base, "save-dialog");

    presetDir.deleteRecursively();
    processor.editorBeingDeleted (editor.get());
    editor.reset();
    return 0;
}
