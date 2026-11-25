#include "PluginProcessor.h"

#include "PluginEditor.h"
#include "filter_models.h"

#include <climits>

namespace ambika_plugin
{
    namespace
    {
        constexpr int kStateVersion = 3;  // 2: multi-part, 3: preset names.
    }

    // The main mix, then one optional stereo output per voice card (off
    // until the host or the user enables it). The standalone app only has
    // the main output.
    juce::AudioProcessor::BusesProperties AmbikaAudioProcessor::makeBuses()
    {
        auto buses = BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true);
        if (juce::PluginHostType::getPluginLoadedAs() != juce::AudioProcessor::wrapperType_Standalone)
            for (int v = 0; v < kNumVoices; ++v)
                buses = buses.withOutput ("Voice " + juce::String (v + 1), juce::AudioChannelSet::stereo(), false);
        return buses;
    }

    AmbikaAudioProcessor::AmbikaAudioProcessor()
        : AudioProcessor (makeBuses()),
          state (*this, nullptr, "AmbikaState", createParameterLayout()),
          engine (std::make_unique<ambika::plugin::Engine>())
    {
        const int count = ambika::plugin::NumParameters();
        for (int part = 0; part < kNumParts; ++part)
        {
            auto& raw = rawParameters[(size_t) part];
            raw.reserve ((size_t) count);
            for (int i = 0; i < count; ++i)
                raw.push_back (state.getRawParameterValue (partParameterId (part, ambika::plugin::GetParameter (i))));
            appliedValues[(size_t) part].assign ((size_t) count, INT_MIN);
            rawKeyRange[(size_t) part] = { state.getRawParameterValue (partKeyLowId (part)),
                                           state.getRawParameterValue (partKeyHighId (part)) };
        }
        for (int voice = 0; voice < kNumVoices; ++voice)
        {
            rawVoiceParts[(size_t) voice] = state.getRawParameterValue (voicePartId (voice));
            rawVoiceOutputs[(size_t) voice] = state.getRawParameterValue (voiceOutputId (voice));
        }
        outputGain = state.getRawParameterValue (kOutputGainId);
        voiceMode = state.getRawParameterValue (kVoiceModeId);
        unison = state.getRawParameterValue (kUnisonId);

        // Engine needs a rate before the host calls prepareToPlay (some hosts
        // query state or open the editor first).
        engine->Init (48000.0);
        for (auto& owner : voiceOwners)
            owner = 0;
        for (auto& step : currentSteps)
            step = 0xff;
    }

    bool AmbikaAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
    {
        const auto& out = layouts.getMainOutputChannelSet();
        if (out != juce::AudioChannelSet::mono() && out != juce::AudioChannelSet::stereo())
            return false;
        // Voice outputs: off, mono (folded down) or stereo.
        for (int bus = 1; bus < layouts.outputBuses.size(); ++bus)
        {
            const auto& set = layouts.outputBuses.getReference (bus);
            if (! set.isDisabled() && set != juce::AudioChannelSet::mono() && set != juce::AudioChannelSet::stereo())
                return false;
        }
        return layouts.inputBuses.isEmpty() || layouts.getMainInputChannelSet().isDisabled();
    }

    void AmbikaAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
    {
        engine->Init (sampleRate);
        // Engine::Init restores the defaults; push every parameter again.
        for (auto& values : appliedValues)
            std::fill (values.begin(), values.end(), INT_MIN);
        appliedVoiceParts.fill (INT_MIN);
        appliedVoiceMode = appliedUnison = -1;
        for (auto& range : appliedKeyRange)
            range.fill (INT_MIN);
        applyParameters();
        setLatencySamples (engine->latency_samples());

        left.assign ((size_t) juce::jmax (1, samplesPerBlock), 0.0f);
        right.assign (left.size(), 0.0f);
        voiceScratch.assign (left.size() * 2 * kNumVoices, 0.0f);
        gainSmoothed.reset (sampleRate, 0.02);
        gainSmoothed.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (outputGain->load()));
    }

    void AmbikaAudioProcessor::applyParameters()
    {
        // Voice assignment first, so that patch values reach the right voices.
        bool assignmentChanged = false;
        uint8_t voiceParts[kNumVoices];
        for (int v = 0; v < kNumVoices; ++v)
        {
            // Choice index: 0 = off, 1..6 = part.
            const int owner = juce::roundToInt (rawVoiceParts[(size_t) v]->load (std::memory_order_relaxed));
            voiceParts[v] = owner == 0 ? ambika::plugin::kNoPart : (uint8_t) (owner - 1);
            if (owner != appliedVoiceParts[(size_t) v])
            {
                appliedVoiceParts[(size_t) v] = owner;
                assignmentChanged = true;
            }
        }
        if (assignmentChanged)
            engine->SetVoiceAssignment (voiceParts);

        const int mode = juce::roundToInt (voiceMode->load (std::memory_order_relaxed));
        const int voices = juce::roundToInt (unison->load (std::memory_order_relaxed));
        if (mode != appliedVoiceMode || voices != appliedUnison)
        {
            appliedVoiceMode = mode;
            appliedUnison = voices;
            engine->SetVoiceMode ((uint8_t) mode, (uint8_t) voices);
        }

        for (int part = 0; part < kNumParts; ++part)
        {
            auto& enginePart = engine->part ((uint8_t) part);
            auto settings = enginePart.settings();
            bool settingsChanged = false;
            const auto& raw = rawParameters[(size_t) part];
            auto& applied = appliedValues[(size_t) part];

            for (size_t i = 0; i < raw.size(); ++i)
            {
                const auto& info = ambika::plugin::GetParameter ((int) i);
                const int value = toEngineValue (info, raw[i]->load (std::memory_order_relaxed));
                if (value == applied[i])
                    continue;
                applied[i] = value;

                if (info.target == ambika::plugin::TARGET_PATCH)
                {
                    enginePart.SetPatchValue (info.address, (uint8_t) (int8_t) value);
                }
                else
                {
                    ambika::plugin::SetPartField (&settings, info.address, value);
                    settingsChanged = true;
                }
            }
            if (settingsChanged)
                enginePart.SetSettings (settings);

            // Keyboard split (every part listens on all MIDI channels).
            const std::array<int, 2> range { juce::roundToInt (rawKeyRange[(size_t) part][0]->load (std::memory_order_relaxed)),
                                             juce::roundToInt (rawKeyRange[(size_t) part][1]->load (std::memory_order_relaxed)) };
            if (range != appliedKeyRange[(size_t) part])
            {
                appliedKeyRange[(size_t) part] = range;
                ambika::plugin::PartMapping mapping;
                mapping.keyrange_low = (uint8_t) range[0];
                mapping.keyrange_high = (uint8_t) range[1];
                engine->SetPartMapping ((uint8_t) part, mapping);
            }

        }
    }

    void AmbikaAudioProcessor::handleMidi (const juce::MidiMessage& message)
    {
        const auto channel = (uint8_t) juce::jlimit (0, 15, message.getChannel() - 1);
        if (message.isNoteOn())
            engine->NoteOn (channel, (uint8_t) message.getNoteNumber(), message.getVelocity());
        else if (message.isNoteOff())
            engine->NoteOff (channel, (uint8_t) message.getNoteNumber());
        else if (message.isPitchWheel())
            engine->PitchBend (channel, (uint16_t) message.getPitchWheelValue());
        else if (message.isController())
            engine->ControlChange (channel, (uint8_t) message.getControllerNumber(),
                                   (uint8_t) message.getControllerValue());
        else if (message.isChannelPressure())
            engine->ChannelPressure (channel, (uint8_t) message.getChannelPressureValue());
        else if (message.isAftertouch())
            engine->PolyPressure (channel, (uint8_t) message.getNoteNumber(),
                                  (uint8_t) message.getAfterTouchValue());
    }

    void AmbikaAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
    {
        juce::ScopedNoDenormals noDenormals;
        const int numSamples = buffer.getNumSamples();
        if ((int) left.size() < numSamples)
        {
            left.resize ((size_t) numSamples);  // Only if the host breaks its own block size.
            right.resize ((size_t) numSamples);
            voiceScratch.resize ((size_t) numSamples * 2 * kNumVoices);
        }

        applyParameters();

        // A voice set to "own output" plays there only while the host has
        // that bus enabled; otherwise it stays in the main mix.
        int busMask = 0, directMask = 0;
        for (int v = 0; v < kNumVoices; ++v)
        {
            if (auto* bus = getBus (false, kVoiceBusOffset + v); bus != nullptr && bus->isEnabled()
                                                                  && bus->getNumberOfChannels() > 0)
                busMask |= 1 << v;
            if (juce::roundToInt (rawVoiceOutputs[(size_t) v]->load (std::memory_order_relaxed)) == 1
                && (busMask & (1 << v)))
                directMask |= 1 << v;
        }
        engine->SetDirectOutputs ((uint8_t) directMask);
        voiceBusMask.store (busMask, std::memory_order_relaxed);
        voiceDirectMask.store (directMask, std::memory_order_relaxed);

        // Tempo and transport for synced LFOs, the arpeggiator and the
        // sequencers. Without a song position (or a playhead, as in the
        // standalone app) the clock runs free.
        bool playing = false;
        double ppq = 0.0;
        if (auto* playHead = getPlayHead())
        {
            if (auto position = playHead->getPosition())
            {
                if (auto bpm = position->getBpm())
                    engine->SetTempo (*bpm);
                if (auto songPosition = position->getPpqPosition(); songPosition && position->getIsPlaying())
                {
                    playing = true;
                    ppq = *songPosition;
                }
            }
        }
        engine->SetTransport (playing, ppq);

        // Render between MIDI events so that note timing is sample-accurate.
        const size_t stride = left.size();
        auto renderTo = [&] (int from, int to)
        {
            float* voiceOutputs[2 * kNumVoices];
            for (int c = 0; c < 2 * kNumVoices; ++c)
                voiceOutputs[c] = voiceScratch.data() + (size_t) c * stride + (size_t) from;
            engine->Render (left.data() + from, right.data() + from, to - from, voiceOutputs);
        };
        int rendered = 0;
        for (const auto metadata : midi)
        {
            const int position = juce::jlimit (0, numSamples, metadata.samplePosition);
            if (position > rendered)
            {
                renderTo (rendered, position);
                rendered = position;
            }
            handleMidi (metadata.getMessage());
        }
        if (rendered < numSamples)
            renderTo (rendered, numSamples);

        // Output gain, on the mix and on every voice output.
        gainSmoothed.setTargetValue (juce::Decibels::decibelsToGain (outputGain->load()));
        float* direct[kNumVoices * 2];
        int numDirect = 0;
        for (int v = 0; v < kNumVoices; ++v)
            if (directMask & (1 << v))
                for (int c = 0; c < 2; ++c)
                    direct[numDirect++] = voiceScratch.data() + (size_t) (2 * v + c) * stride;
        for (int i = 0; i < numSamples; ++i)
        {
            const float gain = gainSmoothed.getNextValue();
            left[(size_t) i] *= gain;
            right[(size_t) i] *= gain;
            for (int c = 0; c < numDirect; ++c)
                direct[c][i] *= gain;
        }

        // Every channel the host gave us is written: the mix, the voice
        // outputs, and silence everywhere else.
        buffer.clear();
        auto writePair = [numSamples] (juce::AudioBuffer<float> out, const float* l, const float* r)
        {
            if (out.getNumChannels() >= 2)
            {
                out.copyFrom (0, 0, l, numSamples);
                out.copyFrom (1, 0, r, numSamples);
            }
            else if (out.getNumChannels() == 1)
            {
                // Mono bus: fold the stereo signal down.
                out.copyFrom (0, 0, l, numSamples, 0.5f);
                out.addFrom (0, 0, r, numSamples, 0.5f);
            }
        };
        writePair (getBusBuffer (buffer, false, 0), left.data(), right.data());
        for (int v = 0; v < kNumVoices; ++v)
        {
            if (! (directMask & (1 << v)))
                continue;
            const float* l = voiceScratch.data() + (size_t) (2 * v) * stride;
            const float* r = voiceScratch.data() + (size_t) (2 * v + 1) * stride;
            writePair (getBusBuffer (buffer, false, kVoiceBusOffset + v), l, r);
        }

        for (int v = 0; v < kNumVoices; ++v)
        {
            voiceLevels[(size_t) v].store (engine->voice_level (v), std::memory_order_relaxed);
            voiceOwners[(size_t) v].store (engine->voice_part ((uint8_t) v), std::memory_order_relaxed);
        }
        for (int p = 0; p < kNumParts; ++p)
        {
            const auto& part = engine->part ((uint8_t) p);
            for (int k = 0; k < 3; ++k)
                currentSteps[(size_t) (p * 4 + k)].store (part.current_step ((uint8_t) k), std::memory_order_relaxed);
            currentSteps[(size_t) (p * 4 + 3)].store (part.current_rhythm_step(), std::memory_order_relaxed);
        }
    }

    juce::AudioProcessorEditor* AmbikaAudioProcessor::createEditor()
    {
        return new AmbikaAudioProcessorEditor (*this);
    }

    void AmbikaAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
    {
        auto tree = state.copyState();
        tree.setProperty ("stateVersion", kStateVersion, nullptr);
        tree.setProperty ("editedPart", editedPart(), nullptr);
        if (editorScale() > 0.0f)
            tree.setProperty ("editorScale", editorScale(), nullptr);
        presetManager.writeState (tree);
        if (auto xml = tree.createXml())
            copyXmlToBinary (*xml, destData);
    }

    void AmbikaAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
    {
        if (auto xml = getXmlFromBinary (data, sizeInBytes))
        {
            if (xml->hasTagName (state.state.getType()))
            {
                auto tree = juce::ValueTree::fromXml (*xml);
                // Projects saved before filter models existed played the
                // Ambika SVF; without a stored value they would get the
                // default for new patches (Ripples).
                for (int part = 0; part < kNumParts; ++part)
                {
                    const auto id = partParameterId (part, juce::String ("filter_model"));
                    if (! tree.getChildWithProperty ("id", id).isValid())
                        tree.appendChild (juce::ValueTree ("PARAM", { { "id", id },
                                                                      { "value", ambika::plugin::kFilterModelAmbika } }),
                                          nullptr);
                }
                setEditedPart (tree.getProperty ("editedPart", 0));
                setEditorScale ((float) tree.getProperty ("editorScale", 0.0f));
                // Version 1 states have no multi parameters; replaceState leaves
                // them at their defaults (part 1, all voices, all channels).
                state.replaceState (tree);
                presetManager.readState (tree);
            }
        }
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new ambika_plugin::AmbikaAudioProcessor();
}
