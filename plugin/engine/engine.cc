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
    parts_[i].Init(voices_);
    mappings_[i] = PartMapping();
  }
  uint8_t all_to_part_1[kNumVoices];
  memset(all_to_part_1, 0, sizeof(all_to_part_1));
  memset(voice_parts_, kNoPart, sizeof(voice_parts_));
  SetVoiceAssignment(all_to_part_1);

  resampler_left_.Init(internal_rate, host_sample_rate);
  resampler_right_.Init(internal_rate, host_sample_rate);
  latency_samples_ = static_cast<int>(std::lround(
      resampler_left_.latency_in_input_samples() * host_sample_rate / internal_rate));
  for (float& p : pan_) p = 0.5f;
  pan_coefficient_ = static_cast<float>(1.0 - std::exp(-1.0 / (0.002 * internal_rate)));
  distribution_ = DISTRIBUTION_LAYER;
  parts_per_note_ = 1;
  round_robin_next_ = 0;
  memset(note_parts_, 0, sizeof(note_parts_));
  lfo_refresh_cycle_ = 0;
  clock_accumulator_ = 0.0;
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
  // Voices nobody owns must not keep sounding.
  for (uint8_t v = 0; v < kNumVoices; ++v) {
    if (voice_parts_[v] == kNoPart) {
      voices_[v].Kill();
    }
  }
}

void Engine::SetNoteDistribution(uint8_t mode, uint8_t parts_per_note) {
  if (mode >= DISTRIBUTION_LAST) mode = DISTRIBUTION_LAYER;
  if (parts_per_note < 1) parts_per_note = 1;
  if (parts_per_note > kNumParts) parts_per_note = kNumParts;
  if (mode != distribution_) {
    // Notes held under the old scheme would not get their note-off.
    AllNotesOff();
    memset(note_parts_, 0, sizeof(note_parts_));
    round_robin_next_ = 0;
  }
  distribution_ = mode;
  parts_per_note_ = parts_per_note;
}

void Engine::NoteOn(uint8_t channel, uint8_t note, uint8_t velocity) {
  channel &= 0x0f;
  note &= 0x7f;
  if (velocity == 0) {
    NoteOff(channel, note);
    return;
  }
  if (distribution_ == DISTRIBUTION_ROUND_ROBIN) {
    // Release whatever this key is still holding before re-assigning it.
    if (note_parts_[channel][note]) {
      NoteOff(channel, note);
    }
    // Parts that may play this note, starting from the round robin position.
    uint8_t mask = 0;
    uint8_t taken = 0;
    uint8_t last = round_robin_next_;
    for (uint8_t n = 0; n < kNumParts && taken < parts_per_note_; ++n) {
      const uint8_t i = (round_robin_next_ + n) % kNumParts;
      if (parts_[i].num_voices() && mappings_[i].receive_channel(channel) &&
          mappings_[i].accept_note(note)) {
        mask |= 1 << i;
        ++taken;
        last = i;
      }
    }
    if (!mask) {
      return;
    }
    round_robin_next_ = (last + 1) % kNumParts;
    note_parts_[channel][note] = mask;
    for (uint8_t i = 0; i < kNumParts; ++i) {
      if (mask & (1 << i)) parts_[i].NoteOn(note, velocity);
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
  if (distribution_ == DISTRIBUTION_ROUND_ROBIN) {
    const uint8_t mask = note_parts_[channel][note];
    note_parts_[channel][note] = 0;
    for (uint8_t i = 0; i < kNumParts; ++i) {
      if (mask & (1 << i)) parts_[i].NoteOff(note);
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
  if (distribution_ == DISTRIBUTION_ROUND_ROBIN) {
    const uint8_t mask = note_parts_[channel & 0x0f][note & 0x7f];
    for (uint8_t i = 0; i < kNumParts; ++i) {
      if (mask & (1 << i)) parts_[i].Aftertouch(note, value << 1);
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
  memset(note_parts_, 0, sizeof(note_parts_));
}

void Engine::AllSoundOff() {
  for (uint8_t i = 0; i < kNumParts; ++i) parts_[i].AllSoundOff();
  memset(note_parts_, 0, sizeof(note_parts_));
}

void Engine::RenderControlBlock() {
  // Controller side: MIDI clock ticks for synced LFOs, then the part LFOs.
  const double control_rate = kNativeSampleRate / kAudioBlockSize;
  clock_accumulator_ += tempo_bpm_ * 24.0 / 60.0 / control_rate;
  while (clock_accumulator_ >= 1.0) {
    clock_accumulator_ -= 1.0;
    for (uint8_t i = 0; i < kNumParts; ++i) parts_[i].Clock();
  }
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
        block_left_[k] += voice_block_[k] * gain_left;
        block_right_[k] += voice_block_[k] * gain_right;
      }
    }
    pan_[i] = pan;
  }
  resampler_left_.Push(block_left_, n);
  resampler_right_.Push(block_right_, n);
}

void Engine::Render(float* left, float* right, int num_samples) {
  for (int i = 0; i < num_samples; ++i) {
    while (!resampler_left_.CanPop()) {
      RenderControlBlock();
    }
    left[i] = resampler_left_.Pop();
    right[i] = resampler_right_.Pop();
  }
}

}  // namespace plugin
}  // namespace ambika
