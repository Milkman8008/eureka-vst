#include "engine.h"

#include <cmath>
#include <cstring>

namespace ambika {
namespace plugin {

namespace {

// Per-voice level in the mix. Six voices at full level stay below 0 dBFS.
// The constant-power pan law gives 1/sqrt(2) per side at centre; the
// sqrt(2) makes a centred voice as loud as in the earlier mono builds.
const float kVoiceGain = 0.16f * 1.41421356f;
const float kHalfPi = 1.57079633f;

// MIDI channel mode messages.
const uint8_t kAllSoundOff = 120;
const uint8_t kResetAllControllers = 121;
const uint8_t kAllNotesOff = 123;

}  // namespace

void Engine::Init(double host_sample_rate) {
  const double internal_rate = kNativeSampleRate * kOversampling;
  for (uint8_t i = 0; i < kNumVoices; ++i) {
    voices_[i].Init();
    analog_[i].Init(internal_rate, kOversampling);
  }
  for (uint8_t i = 0; i < kNumParts; ++i) {
    parts_[i].Init(voices_, i);
    mappings_[i] = PartMapping();
  }
  uint8_t all_to_part_1[kNumVoices];
  memset(all_to_part_1, 0, sizeof(all_to_part_1));
  memset(voice_parts_, kNoPart, sizeof(voice_parts_));
  SetVoiceAssignment(all_to_part_1);

  resampler_left_.Init(internal_rate, host_sample_rate);
  resampler_right_.Init(internal_rate, host_sample_rate);
  for (auto& pair : voice_resamplers_) {
    for (auto& r : pair) r.Init(internal_rate, host_sample_rate);
  }
  direct_mask_ = 0;
  latency_samples_ = static_cast<int>(std::lround(
      resampler_left_.latency_in_input_samples() * host_sample_rate / internal_rate));
  for (float& p : pan_) p = 0.5f;
  pan_coefficient_ = static_cast<float>(1.0 - std::exp(-1.0 / (0.002 * internal_rate)));
  voice_mode_ = VOICE_MODE_PER_PART;
  unison_ = 1;
  sustain_ = false;
  age_counter_ = 0;
  ResetAllocation();
  lfo_refresh_cycle_ = 0;
  song_ticks_ = 0.0;
  next_tick_ = 0;
  transport_playing_ = false;
}

void Engine::SetTransport(bool playing, double ppq_position) {
  const double ticks = ppq_position * 24.0;
  if (playing && !transport_playing_) {
    // Start: the sequencers take their place at the song position.
    song_ticks_ = ticks;
    next_tick_ = static_cast<int64_t>(std::ceil(ticks - 1e-9));
    for (uint8_t i = 0; i < kNumParts; ++i) {
      parts_[i].set_transport_running(true);
      parts_[i].Start(static_cast<uint32_t>(next_tick_ < 0 ? 0 : next_tick_));
    }
  } else if (playing) {
    // A jump or loop: start again from the new position. The engine runs
    // ahead of the output by its latency, a fraction of a tick, which this
    // tolerance absorbs.
    if (std::fabs(ticks - song_ticks_) > 1.0) {
      song_ticks_ = ticks;
      next_tick_ = static_cast<int64_t>(std::ceil(ticks - 1e-9));
      for (uint8_t i = 0; i < kNumParts; ++i) {
        parts_[i].Start(static_cast<uint32_t>(next_tick_ < 0 ? 0 : next_tick_));
      }
    }
  } else if (transport_playing_) {
    for (uint8_t i = 0; i < kNumParts; ++i) {
      parts_[i].set_transport_running(false);
      parts_[i].Stop();
    }
  }
  transport_playing_ = playing;
}

void Engine::SetPartMapping(uint8_t part, const PartMapping& mapping) {
  if (part >= kNumParts) {
    return;
  }
  PartMapping clamped = mapping;
  if (clamped.midi_channel > 16) clamped.midi_channel = 16;
  if (clamped.keyrange_low > 127) clamped.keyrange_low = 127;
  if (clamped.keyrange_high > 127) clamped.keyrange_high = 127;
  if (clamped.midi_channel != mappings_[part].midi_channel ||
      clamped.keyrange_low != mappings_[part].keyrange_low ||
      clamped.keyrange_high != mappings_[part].keyrange_high) {
    // Notes held under the old mapping would never get their note-off.
    parts_[part].AllNotesOff();
  }
  mappings_[part] = clamped;
}

void Engine::SetVoiceAssignment(const uint8_t* voice_parts) {
  uint8_t old_masks[kNumParts] = { 0 };
  uint8_t new_masks[kNumParts] = { 0 };
  for (uint8_t v = 0; v < kNumVoices; ++v) {
    if (voice_parts_[v] < kNumParts) old_masks[voice_parts_[v]] |= 1 << v;
    voice_parts_[v] = voice_parts[v] < kNumParts ? voice_parts[v] : kNoPart;
    if (voice_parts_[v] < kNumParts) new_masks[voice_parts_[v]] |= 1 << v;
  }
  // Parts that lose voices first, so a voice moving between parts is
  // released by its old part before the new part sets it up.
  for (uint8_t p = 0; p < kNumParts; ++p) {
    if (new_masks[p] != old_masks[p] && (old_masks[p] & ~new_masks[p])) {
      parts_[p].AssignVoices(new_masks[p]);
    }
  }
  for (uint8_t p = 0; p < kNumParts; ++p) {
    if (new_masks[p] != old_masks[p] && !(old_masks[p] & ~new_masks[p])) {
      parts_[p].AssignVoices(new_masks[p]);
    }
  }
  // Engine-allocated notes refer to cards that may have changed owner.
  ResetAllocation();
  // Voices nobody owns must not keep sounding.
  for (uint8_t v = 0; v < kNumVoices; ++v) {
    if (voice_parts_[v] == kNoPart) {
      voices_[v].Kill();
    }
  }
}

void Engine::SetDirectOutputs(uint8_t mask) {
  mask &= (1 << kNumVoices) - 1;
  for (uint8_t v = 0; v < kNumVoices; ++v) {
    if ((mask & (1 << v)) && !(direct_mask_ & (1 << v))) {
      // Joins from silence, aligned with the main output.
      voice_resamplers_[v][0].SyncTo(resampler_left_);
      voice_resamplers_[v][1].SyncTo(resampler_left_);
    }
  }
  direct_mask_ = mask;
}

void Engine::SetVoiceMode(uint8_t mode, uint8_t unison) {
  if (mode >= VOICE_MODE_LAST) mode = VOICE_MODE_PER_PART;
  if (unison < 1) unison = 1;
  if (unison > kNumVoices) unison = kNumVoices;
  if (mode != voice_mode_) {
    // Notes held under the old scheme would not get their note-off.
    for (uint8_t i = 0; i < kNumParts; ++i) parts_[i].AllNotesOff();
    ResetAllocation();
  }
  voice_mode_ = mode;
  unison_ = unison;
  for (uint8_t i = 0; i < kNumParts; ++i) {
    parts_[i].set_note_sink(mode == VOICE_MODE_PER_PART ? nullptr : this);
  }
}

void Engine::ResetAllocation() {
  memset(voice_note_, 0xff, sizeof(voice_note_));
  memset(voice_channel_, 0, sizeof(voice_channel_));
  memset(voice_age_, 0, sizeof(voice_age_));
  memset(note_voices_, 0, sizeof(note_voices_));
  sustained_ = 0;
  cyclic_next_ = 0;
}

void Engine::ReleaseAllocatedVoice(uint8_t v) {
  const uint8_t owner = voice_parts_[v];
  if (voice_note_[v] != 0xff) {
    note_voices_[voice_channel_[v]][voice_note_[v]] &= ~(1 << v);
  }
  if (owner < kNumParts) {
    parts_[owner].ReleaseVoice(v);
  }
  voice_note_[v] = 0xff;
  sustained_ &= ~(1 << v);
  voice_age_[v] = ++age_counter_;
}

uint8_t Engine::DirectCards(uint8_t channel, uint8_t note) const {
  uint8_t mask = 0;
  for (uint8_t v = 0; v < kNumVoices; ++v) {
    const uint8_t owner = voice_parts_[v];
    if (owner < kNumParts && mappings_[owner].receive_channel(channel) &&
        mappings_[owner].accept_note(note) && !parts_[owner].sequencing()) {
      mask |= 1 << v;
    }
  }
  return mask;
}

void Engine::ReleaseSlotNote(uint8_t slot, uint8_t note) {
  const uint8_t mask = note_voices_[slot][note];
  for (uint8_t v = 0; v < kNumVoices; ++v) {
    if (mask & (1 << v)) ReleaseAllocatedVoice(v);
  }
}

void Engine::PartNoteOn(uint8_t part, uint8_t note, uint8_t velocity) {
  uint8_t allowed = 0;
  for (uint8_t v = 0; v < kNumVoices; ++v) {
    if (voice_parts_[v] == part) allowed |= 1 << v;
  }
  AllocatedNoteOn(kPartSlot + part, note & 0x7f, velocity, allowed);
}

void Engine::PartNoteOff(uint8_t part, uint8_t note) {
  ReleaseSlotNote(kPartSlot + part, note & 0x7f);
}

void Engine::PartAllNotesOff(uint8_t part) {
  for (uint8_t v = 0; v < kNumVoices; ++v) {
    if (voice_note_[v] != 0xff && voice_channel_[v] == kPartSlot + part) {
      ReleaseAllocatedVoice(v);
    }
  }
}

void Engine::AllocatedNoteOn(uint8_t slot, uint8_t note, uint8_t velocity, uint8_t allowed) {
  // A key pressed again while still sounding: release it first.
  ReleaseSlotNote(slot, note);

  // The allowed voicecards, in cyclic order if needed.
  uint8_t candidates[kNumVoices];
  uint8_t num_candidates = 0;
  for (uint8_t n = 0; n < kNumVoices; ++n) {
    const uint8_t v = voice_mode_ == VOICE_MODE_CYCLIC ? (cyclic_next_ + n) % kNumVoices : n;
    if ((allowed & (1 << v)) && voice_parts_[v] < kNumParts) {
      candidates[num_candidates++] = v;
    }
  }
  if (!num_candidates) {
    return;
  }
  const uint8_t count = unison_ < num_candidates ? unison_ : num_candidates;

  uint8_t chosen[kNumVoices];
  if (voice_mode_ == VOICE_MODE_CYCLIC) {
    // Next cards in order, busy or not (like the hardware's cyclic mode).
    for (uint8_t k = 0; k < count; ++k) chosen[k] = candidates[k];
    cyclic_next_ = (chosen[count - 1] + 1) % kNumVoices;
  } else {
    // Poly: free cards idle longest first, then the oldest busy ones.
    for (uint8_t k = 0; k < count; ++k) {
      int best = -1;
      for (uint8_t c = 0; c < num_candidates; ++c) {
        const uint8_t v = candidates[c];
        bool taken = false;
        for (uint8_t j = 0; j < k; ++j) taken = taken || chosen[j] == v;
        if (taken) continue;
        if (best < 0) { best = v; continue; }
        const bool v_free = voice_note_[v] == 0xff, best_free = voice_note_[best] == 0xff;
        if ((v_free && !best_free) ||
            (v_free == best_free && voice_age_[v] < voice_age_[best])) {
          best = v;
        }
      }
      chosen[k] = static_cast<uint8_t>(best);
    }
  }

  uint8_t mask = 0;
  for (uint8_t k = 0; k < count; ++k) {
    const uint8_t v = chosen[k];
    if (voice_note_[v] != 0xff) {
      // Stolen from another held note.
      note_voices_[voice_channel_[v]][voice_note_[v]] &= ~(1 << v);
    }
    sustained_ &= ~(1 << v);
    parts_[voice_parts_[v]].PlayVoice(v, note, velocity, k);
    voice_note_[v] = note;
    voice_channel_[v] = slot;
    voice_age_[v] = ++age_counter_;
    mask |= 1 << v;
  }
  note_voices_[slot][note] = mask;
}

void Engine::NoteOn(uint8_t channel, uint8_t note, uint8_t velocity) {
  channel &= 0x0f;
  note &= 0x7f;
  if (velocity == 0) {
    NoteOff(channel, note);
    return;
  }
  if (voice_mode_ != VOICE_MODE_PER_PART) {
    // Arpeggiating parts take the key; the others play it on their cards.
    for (uint8_t i = 0; i < kNumParts; ++i) {
      if (parts_[i].sequencing() && parts_[i].num_voices() &&
          mappings_[i].receive_channel(channel) && mappings_[i].accept_note(note)) {
        parts_[i].NoteOn(note, velocity);
      }
    }
    const uint8_t direct = DirectCards(channel, note);
    if (direct) {
      AllocatedNoteOn(channel, note, velocity, direct);
    }
    return;
  }
  for (uint8_t i = 0; i < kNumParts; ++i) {
    if (mappings_[i].receive_channel(channel) && mappings_[i].accept_note(note)) {
      parts_[i].NoteOn(note, velocity);
    }
  }
}

void Engine::NoteOff(uint8_t channel, uint8_t note) {
  channel &= 0x0f;
  note &= 0x7f;
  if (voice_mode_ != VOICE_MODE_PER_PART) {
    const uint8_t mask = note_voices_[channel][note];
    for (uint8_t v = 0; v < kNumVoices; ++v) {
      if (!(mask & (1 << v))) continue;
      if (sustain_) {
        sustained_ |= 1 << v;  // Released when the pedal comes up.
      } else {
        ReleaseAllocatedVoice(v);
      }
    }
    // Every part that may hold the key (its mode may have changed since).
    for (uint8_t i = 0; i < kNumParts; ++i) {
      if (parts_[i].sequencing() && mappings_[i].receive_channel(channel) &&
          mappings_[i].accept_note(note)) {
        parts_[i].NoteOff(note);
      }
    }
    return;
  }
  for (uint8_t i = 0; i < kNumParts; ++i) {
    if (mappings_[i].receive_channel(channel) && mappings_[i].accept_note(note)) {
      parts_[i].NoteOff(note);
    }
  }
}

void Engine::ControlChange(uint8_t channel, uint8_t controller, uint8_t value) {
  if (controller == 64 && voice_mode_ != VOICE_MODE_PER_PART) {
    // Sustain pedal for engine-allocated voices; arpeggiating parts hold
    // their keys themselves.
    sustain_ = value >= 64;
    if (!sustain_) {
      for (uint8_t v = 0; v < kNumVoices; ++v) {
        if (sustained_ & (1 << v)) ReleaseAllocatedVoice(v);
      }
    }
    for (uint8_t i = 0; i < kNumParts; ++i) {
      if (mappings_[i].receive_channel(channel)) parts_[i].ControlChange(controller, value);
    }
    return;
  }
  if ((controller == kAllSoundOff || controller == kAllNotesOff) &&
      voice_mode_ != VOICE_MODE_PER_PART) {
    ResetAllocation();
  }
  for (uint8_t i = 0; i < kNumParts; ++i) {
    if (!mappings_[i].receive_channel(channel)) {
      continue;
    }
    switch (controller) {
      case kAllSoundOff: parts_[i].AllSoundOff(); break;
      case kResetAllControllers: parts_[i].ResetAllControllers(); break;
      case kAllNotesOff: parts_[i].AllNotesOff(); break;
      default: parts_[i].ControlChange(controller, value); break;
    }
  }
}

void Engine::PitchBend(uint8_t channel, uint16_t value) {
  for (uint8_t i = 0; i < kNumParts; ++i) {
    if (mappings_[i].receive_channel(channel)) parts_[i].PitchBend(value);
  }
}

void Engine::ChannelPressure(uint8_t channel, uint8_t value) {
  for (uint8_t i = 0; i < kNumParts; ++i) {
    if (mappings_[i].receive_channel(channel)) parts_[i].Aftertouch(value << 1);
  }
}

void Engine::PolyPressure(uint8_t channel, uint8_t note, uint8_t value) {
  if (voice_mode_ != VOICE_MODE_PER_PART) {
    const uint8_t mask = note_voices_[channel & 0x0f][note & 0x7f];
    for (uint8_t v = 0; v < kNumVoices; ++v) {
      if ((mask & (1 << v)) && voice_parts_[v] < kNumParts) {
        parts_[voice_parts_[v]].VoiceAftertouch(v, value << 1);
      }
    }
    return;
  }
  for (uint8_t i = 0; i < kNumParts; ++i) {
    if (mappings_[i].receive_channel(channel) && mappings_[i].accept_note(note)) {
      parts_[i].Aftertouch(note, value << 1);
    }
  }
}

void Engine::AllNotesOff() {
  for (uint8_t i = 0; i < kNumParts; ++i) parts_[i].AllNotesOff();
  ResetAllocation();
}

void Engine::AllSoundOff() {
  for (uint8_t i = 0; i < kNumParts; ++i) parts_[i].AllSoundOff();
  ResetAllocation();
}

void Engine::RenderControlBlock() {
  // Controller side: MIDI clock ticks for synced LFOs, then the part LFOs.
  const double control_rate = kNativeSampleRate / kAudioBlockSize;
  // Ticks due by the start of this block, then advance the clock.
  while (static_cast<double>(next_tick_) <= song_ticks_) {
    ++next_tick_;
    for (uint8_t i = 0; i < kNumParts; ++i) parts_[i].Clock();
  }
  song_ticks_ += tempo_bpm_ * 24.0 / 60.0 / control_rate;
  ++lfo_refresh_cycle_;
  for (uint8_t i = 0; i < kNumParts; ++i) {
    parts_[i].UpdateLfos(lfo_refresh_cycle_ & 1);
  }

  // Voicecards, each panned into the stereo mix.
  const int n = kAudioBlockSize * kOversampling;
  memset(block_left_, 0, sizeof(block_left_));
  memset(block_right_, 0, sizeof(block_right_));
  for (uint8_t i = 0; i < kNumVoices; ++i) {
    VoiceCard& voice = voices_[i];
    voice.ProcessBlock();
    memset(voice_block_, 0, sizeof(voice_block_));
    analog_[i].Process(voice.output(), kAudioBlockSize,
                       voice.cutoff(), voice.resonance(), voice.filter_mode(),
                       voice.vca(), voice.crush(), voice.filter_model(),
                       voice.filter_variant(), voice_block_);
    // Constant-power pan, smoothed (the pan CV updates once per block).
    // 128 is exactly centre; 0 and 255 are hard left and right.
    const bool direct = direct_mask_ & (1 << i);
    float* mix_left = block_left_;
    float* mix_right = block_right_;
    if (direct) {
      mix_left = voice_left_[i];
      mix_right = voice_right_[i];
      memset(mix_left, 0, sizeof(voice_left_[i]));
      memset(mix_right, 0, sizeof(voice_right_[i]));
    }
    const uint8_t pan_cv = voice.pan();
    const float target = pan_cv >= 255 ? 1.0f : static_cast<float>(pan_cv) / 256.0f;
    float pan = pan_[i];
    for (int s = 0; s < n; s += 8) {
      pan += (pan_coefficient_ * 8.0f) * (target - pan);
      if (pan < 0.0f) pan = 0.0f;
      if (pan > 1.0f) pan = 1.0f;
      const float gain_left = std::cos(pan * kHalfPi) * kVoiceGain;
      const float gain_right = std::sin(pan * kHalfPi) * kVoiceGain;
      for (int k = s; k < s + 8 && k < n; ++k) {
        mix_left[k] += voice_block_[k] * gain_left;
        mix_right[k] += voice_block_[k] * gain_right;
      }
    }
    pan_[i] = pan;
    if (direct) {
      voice_resamplers_[i][0].Push(mix_left, n);
      voice_resamplers_[i][1].Push(mix_right, n);
    }
  }
  resampler_left_.Push(block_left_, n);
  resampler_right_.Push(block_right_, n);
}

void Engine::Render(float* left, float* right, int num_samples,
                    float* const* voice_outputs) {
  for (int i = 0; i < num_samples; ++i) {
    while (!resampler_left_.CanPop()) {
      RenderControlBlock();
    }
    left[i] = resampler_left_.Pop();
    right[i] = resampler_right_.Pop();
    if (!direct_mask_) continue;
    for (uint8_t v = 0; v < kNumVoices; ++v) {
      if (!(direct_mask_ & (1 << v))) continue;
      // Popped even without a destination, to stay in step with the mix.
      const float l = voice_resamplers_[v][0].Pop();
      const float r = voice_resamplers_[v][1].Pop();
      if (voice_outputs != nullptr && voice_outputs[2 * v] != nullptr) {
        voice_outputs[2 * v][i] = l;
        voice_outputs[2 * v + 1][i] = r;
      }
    }
  }
}

}  // namespace plugin
}  // namespace ambika
