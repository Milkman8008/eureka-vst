// Loads the built VST3 the way a DAW does and exercises it: instantiate,
// play notes, change parameters, save/restore state, open the editor.
//
// Usage: ambika_plugin_host_test <path to .vst3>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_events/juce_events.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <cmath>
#include <cstdio>

namespace
{
    int failures = 0;

    void check (bool condition, const juce::String& what)
    {
        std::printf ("%s: %s\n", condition ? "PASS" : "FAIL", what.toRawUTF8());
        if (! condition)
            ++failures;
    }

    struct Rendered
    {
        double rms = 0.0;
        double peak = 0.0;
        bool finite = true;
        bool channelsEqual = true;
    };

    Rendered render (juce::AudioPluginInstance& plugin, int blocks, int blockSize, juce::MidiBuffer midi = {})
    {
        Rendered result;
        juce::AudioBuffer<float> buffer (2, blockSize);
        double sum = 0.0;
        long count = 0;
        for (int b = 0; b < blocks; ++b)
        {
            buffer.clear();
            juce::MidiBuffer blockMidi;
            if (b == 0)
                blockMidi = midi;
            plugin.processBlock (buffer, blockMidi);
            for (int i = 0; i < blockSize; ++i)
            {
                const float l = buffer.getSample (0, i);
                const float r = buffer.getSample (1, i);
                if (! std::isfinite (l) || ! std::isfinite (r))
                    result.finite = false;
                if (std::abs (l - r) > 1e-4f * (1.0f + std::abs (l)))
                    result.channelsEqual = false;
                sum += (double) l * l;
                result.peak = juce::jmax (result.peak, (double) std::abs (l));
                ++count;
            }
        }
        result.rms = std::sqrt (sum / (double) juce::jmax (1L, count));
        return result;
    }

    // Hosted VST3 parameters are identified by number, so look up by name.
    juce::AudioProcessorParameter* findParameter (juce::AudioPluginInstance& plugin, const juce::String& name)
    {
        for (auto* parameter : plugin.getParameters())
            if (parameter->getName (64) == name)
                return parameter;
        return nullptr;
    }
}

int main (int argc, char** argv)
{
    if (argc < 2)
    {
        std::printf ("usage: %s <plugin.vst3>\n", argv[0]);
        return 2;
    }

    juce::ScopedJuceInitialiser_GUI juceInit;

    juce::AudioPluginFormatManager formats;
    formats.addFormat (std::make_unique<juce::VST3PluginFormat>());

    juce::OwnedArray<juce::PluginDescription> descriptions;
    juce::VST3PluginFormat vst3;
    vst3.findAllTypesForFile (descriptions, juce::String (argv[1]));
    check (descriptions.size() == 1, "VST3 bundle contains one plug-in");
    if (descriptions.isEmpty())
        return 1;

    const auto& description = *descriptions[0];
    check (description.isInstrument, "plug-in reports itself as an instrument");
    std::printf ("      name: %s, %d in / %d out\n", description.name.toRawUTF8(),
                 description.numInputChannels, description.numOutputChannels);

    const double rate = 48000.0;
    const int blockSize = 512;
    juce::String error;
    auto plugin = formats.createPluginInstance (description, rate, blockSize, error);
    check (plugin != nullptr, "instantiate (" + error + ")");
    if (plugin == nullptr)
        return 1;

    plugin->setPlayConfigDetails (0, 2, rate, blockSize);
    plugin->prepareToPlay (rate, blockSize);

    std::printf ("      parameters: %d, latency: %d samples\n",
                 plugin->getParameters().size(), plugin->getLatencySamples());
    check (plugin->getParameters().size() > 100, "exposes the patch parameters");

    // Silence, then a chord.
    auto silence = render (*plugin, 20, blockSize);
    check (silence.finite && silence.peak < 1e-4, "silent without notes");

    juce::MidiBuffer chord;
    for (int note : { 48, 55, 60, 64 })
        chord.addEvent (juce::MidiMessage::noteOn (1, note, (juce::uint8) 100), 0);
    auto playing = render (*plugin, 40, blockSize, chord);
    check (playing.finite && playing.rms > 0.01 && playing.peak < 1.0,
           juce::String::formatted ("chord produces sound (rms %.3f, peak %.3f)", playing.rms, playing.peak));
    check (playing.channelsEqual, "centred parts are equal on both channels");

    // A parameter change must reach the engine: closing the filter darkens
    // the sound a lot.
    auto* cutoff = findParameter (*plugin, "P1 Cutoff");
    check (cutoff != nullptr, "P1 Cutoff parameter exists");
    if (cutoff != nullptr)
    {
        cutoff->setValueNotifyingHost (0.0f);
        render (*plugin, 10, blockSize);  // Let the CV settle.
        auto closed = render (*plugin, 20, blockSize);
        check (closed.rms < playing.rms * 0.5,
               juce::String::formatted ("closing the filter reduces level (%.4f -> %.4f)", playing.rms, closed.rms));
    }

    juce::MidiBuffer release;
    for (int note : { 48, 55, 60, 64 })
        release.addEvent (juce::MidiMessage::noteOff (1, note), 0);
    render (*plugin, 200, blockSize, release);
    auto after = render (*plugin, 20, blockSize);
    check (after.peak < 1e-3, "silent after note-off");

    // State round trip.
    juce::MemoryBlock saved;
    plugin->getStateInformation (saved);
    check (saved.getSize() > 0, "state saves");
    if (cutoff != nullptr)
    {
        const float savedValue = cutoff->getValue();
        cutoff->setValueNotifyingHost (1.0f);
        plugin->setStateInformation (saved.getData(), (int) saved.getSize());
        check (std::abs (cutoff->getValue() - savedValue) < 1e-6f, "state restores parameter values");
    }

    // Separate outputs: the main mix plus one optional stereo bus per voice.
    check (plugin->getBusCount (false) == 7, juce::String::formatted ("1 + 6 output buses (%d)", plugin->getBusCount (false)));
    if (auto* voiceOutput = findParameter (*plugin, "Voice 1 Output"); voiceOutput != nullptr && plugin->getBusCount (false) == 7)
    {
        // Measures the main pair and voice 1's pair while one note plays
        // (in Poly mode the first note takes voice card 1).
        auto playOnce = [&] (int numChannels)
        {
            juce::AudioBuffer<float> buffer (numChannels, blockSize);
            std::array<double, 2> energy { 0.0, 0.0 };
            for (int b = 0; b < 30; ++b)
            {
                buffer.clear();
                juce::MidiBuffer midi;
                if (b == 0)
                    midi.addEvent (juce::MidiMessage::noteOn (1, 60, (juce::uint8) 100), 0);
                if (b == 20)
                    midi.addEvent (juce::MidiMessage::noteOff (1, 60), 0);
                plugin->processBlock (buffer, midi);
                for (int i = 0; i < blockSize; ++i)
                {
                    energy[0] += std::pow (buffer.getSample (0, i), 2.0f);
                    if (numChannels >= 4)
                        energy[1] += std::pow (buffer.getSample (2, i), 2.0f);
                }
            }
            render (*plugin, 200, blockSize);  // Let the release finish.
            return energy;
        };
        if (cutoff != nullptr)
            cutoff->setValueNotifyingHost (1.0f);
        voiceOutput->setValueNotifyingHost (1.0f);  // "own output"

        // Bus off: the voice falls back to the main mix.
        auto fallback = playOnce (2);
        check (fallback[0] > 1.0, "voice on a disabled bus stays in the main mix");

        auto layout = plugin->getBusesLayout();
        layout.getChannelSet (false, 1) = juce::AudioChannelSet::stereo();
        plugin->releaseResources();
        check (plugin->setBusesLayout (layout), "enable the Voice 1 output");
        plugin->prepareToPlay (rate, blockSize);
        plugin->setPlayConfigDetails (0, plugin->getTotalNumOutputChannels(), rate, blockSize);
        const int channels = plugin->getTotalNumOutputChannels();
        check (channels == 4, juce::String::formatted ("main + voice 1 = 4 channels (%d)", channels));
        auto routed = playOnce (juce::jmax (channels, 2));
        check (routed[1] > 1.0 && routed[0] < routed[1] * 1e-6,
               juce::String::formatted ("voice 1 plays on its own output only (main %.3g, voice %.3g)", routed[0], routed[1]));

        voiceOutput->setValueNotifyingHost (0.0f);  // back to "main"
        auto mainAgain = playOnce (juce::jmax (channels, 2));
        check (mainAgain[0] > 1.0 && mainAgain[1] < 1e-9, "voice set to main leaves its bus silent");
    }

    // Editor, in a window on screen as a DAW shows it.
    struct OpenEditor
    {
        juce::AudioPluginInstance& plugin;
        std::unique_ptr<juce::AudioProcessorEditor> editor;
        std::unique_ptr<juce::DocumentWindow> window;

        explicit OpenEditor (juce::AudioPluginInstance& p) : plugin (p)
        {
            editor.reset (plugin.createEditorIfNeeded());
            if (editor == nullptr)
                return;
            window = std::make_unique<juce::DocumentWindow> ("eureka!", juce::Colours::black, 0, true);
            window->setUsingNativeTitleBar (true);
            window->setContentNonOwned (editor.get(), true);
            window->setVisible (true);
            settle();
        }
        ~OpenEditor()
        {
            if (window != nullptr)
                window->clearContentComponent();
            window.reset();
            if (editor != nullptr)
                plugin.editorBeingDeleted (editor.get());
            editor.reset();
            settle();
        }
        static void settle() { juce::MessageManager::getInstance()->runDispatchLoopUntil (300); }
    };

    int openedWidth = 0;
    {
        OpenEditor open (*plugin);
        check (open.editor != nullptr, "editor opens");
        if (open.editor != nullptr)
        {
            openedWidth = open.editor->getWidth();
            std::printf ("      editor size: %d x %d\n", open.editor->getWidth(), open.editor->getHeight());
            // The user drags the window larger (125%).
            open.editor->setSize (1500, 1003);
            OpenEditor::settle();
            std::printf ("      resized to: %d x %d\n", open.editor->getWidth(), open.editor->getHeight());
        }
    }
    juce::MemoryBlock withSize;
    {
        OpenEditor open (*plugin);
        const int width = open.editor != nullptr ? open.editor->getWidth() : 0;
        check (std::abs (width - 1500) <= 2,
               juce::String::formatted ("reopened editor keeps its size (%d, expected 1500)", width));
        plugin->getStateInformation (withSize);
    }
    {
        // A new instance loading the project.
        juce::String error2;
        auto reloaded = formats.createPluginInstance (description, rate, blockSize, error2);
        if (reloaded != nullptr)
        {
            reloaded->setStateInformation (withSize.getData(), (int) withSize.getSize());
            OpenEditor open (*reloaded);
            const int width = open.editor != nullptr ? open.editor->getWidth() : 0;
            check (std::abs (width - 1500) <= 2,
                   juce::String::formatted ("a reloaded project opens at its saved size (%d, expected 1500)", width));
        }
    }
    {
        // Back to the size this machine had, so the test leaves the user's
        // window size preference as it was.
        OpenEditor open (*plugin);
        if (open.editor != nullptr && openedWidth > 0)
        {
            open.editor->setSize (openedWidth, juce::roundToInt (openedWidth * 802.0 / 1200.0));
            OpenEditor::settle();
        }
    }

    plugin->releaseResources();
    plugin.reset();

    std::printf ("%s (%d failure%s)\n", failures ? "FAILED" : "OK", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
