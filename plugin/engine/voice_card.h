// Copyright 2011 Emilie Gillet (original voicecard firmware).
// Plug-in port: per-instance version of voicecard/voice.h, sub_oscillator.h
// and transient_generator.h.
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// On the hardware each voicecard is its own microcontroller, so the firmware
// keeps all voice state in static singletons. The plug-in needs six voices per
// instance (and several instances per process), so this file holds the same
// code with the state moved into ordinary members. The DSP is unchanged.

#ifndef AMBIKA_PLUGIN_VOICE_CARD_H_
#define AMBIKA_PLUGIN_VOICE_CARD_H_

#include "avrlib/base.h"
#include "avrlib/op.h"

#include "common/lfo.h"
#include "common/patch.h"

#include "voicecard/envelope.h"
#include "voicecard/oscillator.h"
#include "yam_oscillator.h"
#include "voicecard/voicecard.h"

namespace ambika {
namespace plugin {

// Pitch constants from voicecard/voice.h (14-bit pitch, 128 steps per note).
static const int16_t kLowestNote = 0 * 128;
static const int16_t kHighestNote = 120 * 128;
static const int16_t kOctave = 12 * 128;
static const int16_t kPitchTableStart = 116 * 128;

// Plug-in extension of the modulation matrix: one destination after the
// firmware's MOD_DST_LAST. "pan" places the voice in the stereo field.
static const uint8_t kModDstPan = MOD_DST_LAST;
static const uint8_t kNumPluginModDestinations = MOD_DST_LAST + 1;

// Byte of the part data block that carries the part's pan (0..127, 64 =
// centre). It is one of the bytes the firmware leaves as padding.
static const uint8_t kPartDataPan = 1;

// Mirror of the part data block the controller sends to each voicecard
// (struct Part in voicecard/voice.h).
struct VoicePartData {
  uint8_t volume;
  uint8_t padding[4];
  uint8_t legato;
  uint8_t portamento_time;
};

class SubOscillator {
 public:
  void Reset() {
    phase_.integral = 0;
    phase_.fractional = 0;
    phase_increment_ = phase_;
  }

  void set_increment(uint24_t increment) { phase_increment_ = increment; }

  void Render(uint8_t shape, uint8_t* buffer, uint8_t amount) {
    uint24_t increment = phase_increment_;
    if (shape >= 3) {
      increment = U24ShiftRight(increment);
      shape -= 3;
    }
    uint8_t size = kAudioBlockSize;
    uint8_t pulse_width = shape == 0 ? 0x80 : 0x40;
    uint8_t sub_gain = amount;
    uint8_t mix_gain = ~sub_gain;
    while (size--) {
      phase_ = U24Add(phase_, increment);
      uint8_t v;
      if (shape != 1) {
        v = static_cast<uint8_t>(phase_.integral >> 8) < pulse_width ? 0 : 255;
      } else {
        uint8_t tri = phase_.integral >> 7;
        v = phase_.integral & 0x8000 ? tri : ~tri;
      }
      *buffer = U8Mix(*buffer, v, mix_gain, sub_gain);
      ++buffer;
    }
  }

 private:
  uint24_t phase_;
  uint24_t phase_increment_;
};

class TransientGenerator {
 public:
  void Reset() {
    rng_state_ = 0;
    decimate_ = 0;
    gain_ = 0;
    counter_ = 0;
  }

  void Render(uint8_t shape, uint8_t* buffer, uint8_t amount) {
    if (shape < WAVEFORM_SUB_OSC_CLICK) {
      return;  // Handled by the sub oscillator.
    }
    if (shape > WAVEFORM_SUB_OSC_POP) {
      shape = WAVEFORM_SUB_OSC_POP;
    }
    uint8_t size = kAudioBlockSize;
    while (counter_ && size--) {
      uint8_t value = RenderSample(shape);
      uint8_t amplitude = U8U8MulShift8(gain_, amount);
      *buffer = U8Mix(*buffer, value, amplitude);
      ++buffer;
    }
  }

  void Trigger() { counter_ = 255; }

 private:
  uint8_t RenderSample(uint8_t shape) {
    switch (shape) {
      case WAVEFORM_SUB_OSC_CLICK:
        gain_ = counter_;
        --counter_;
        return counter_ < 32 ? 255 : 0;
      case WAVEFORM_SUB_OSC_GLITCH:
        gain_ = counter_;
        --counter_;
        rng_state_ = rng_state_ * 73 + counter_;
        return rng_state_;
      case WAVEFORM_SUB_OSC_BLOW:
        decimate_ += 2;
        if (decimate_ >= 16) {
          decimate_ -= 17;
          rng_state_ = rng_state_ * 73 + counter_;
          if (decimate_ == 0) {
            --counter_;
            gain_ = (counter_ & 0x80) ? ~counter_ : counter_;
          }
        }
        return rng_state_;
      case WAVEFORM_SUB_OSC_METALLIC:
        --counter_;
        gain_ = counter_ >= 64 ? 255 : counter_ << 2;
        return counter_ * 57;
      default:  // WAVEFORM_SUB_OSC_POP
        --counter_;
        gain_ = counter_ > 0 ? 255 : 0;
        return 0;
    }
  }

  uint8_t rng_state_;
  uint8_t decimate_;
  uint8_t gain_;
  uint8_t counter_;
};

class VoiceCard {
 public:
  VoiceCard() { }

  // Equivalent of the voicecard power-up: zero all state (the firmware relies
  // on zero-initialised globals), then Voice::Init().
  void Init();

  // Commands received from the controller (see voicecard/voicecard_rx.h).
  void Trigger(uint16_t note, uint8_t velocity, uint8_t legato);
  void Release();
  void Kill() { TriggerEnvelope(DEAD); }
  void TriggerEnvelope(uint8_t stage);
  void TriggerEnvelope(uint8_t index, uint8_t stage);
  void ResetAllControllers();

  void set_modulation_source(uint8_t i, uint8_t value) {
    modulation_sources_[i] = value;
  }
  void set_patch_data(uint8_t address, uint8_t value) {
    if (address < sizeof(Patch)) {
      reinterpret_cast<uint8_t*>(&patch_)[address] = value;
    }
  }
  void set_part_data(uint8_t address, uint8_t value) {
    if (address < sizeof(VoicePartData)) {
      reinterpret_cast<uint8_t*>(&part_)[address] = value;
    }
  }
  void set_patch(const Patch& patch) { patch_ = patch; }

  // Renders kAudioBlockSize 8-bit samples (128 = silence) into output().
  // Called once per control period (40 samples, ~1 ms).
  void ProcessBlock();
  const uint8_t* output() const { return output_; }

  // Analog control voltages computed by the last ProcessBlock().
  uint8_t cutoff() const { return modulation_destinations_[MOD_DST_FILTER_CUTOFF]; }
  uint8_t resonance() const {
    return modulation_destinations_[MOD_DST_FILTER_RESONANCE];
  }
  uint8_t vca() const { return modulation_destinations_[MOD_DST_VCA]; }
  uint8_t crush() const { return modulation_destinations_[MOD_DST_MIX_CRUSH]; }
  // Stereo position, 0 = left, 128 = centre, 255 = right.
  uint8_t pan() const { return static_cast<uint8_t>(modulation_destinations_[kModDstPan]); }
  uint8_t filter_mode() const { return patch_.filter[0].mode; }
  // Plug-in extensions, stored in the patch padding (never read by the
  // voicecard firmware): filter model and its variant. See parameters.cc.
  uint8_t filter_model() const { return patch_.padding[0]; }
  uint8_t filter_variant() const { return patch_.padding[1]; }

  const Patch& patch() const { return patch_; }
  uint8_t gate() const { return gate_; }
  // The note being played (7.7 fixed point: MIDI note * 128), and the value
  // of a modulation source. For tests.
  int16_t pitch_target() const { return pitch_target_; }
  uint8_t modulation_source(uint8_t source) const { return modulation_sources_[source]; }

 private:
  void LoadSources();
  void ProcessModulationMatrix();
  void UpdateDestinations();
  void RenderOscillators();

  Patch patch_;
  VoicePartData part_;

  Oscillator osc_1_;
  Oscillator osc_2_;
  YamOscillator yam_1_;  // Used instead of osc_1_ / osc_2_ for YAM shapes.
  YamOscillator yam_2_;
  SubOscillator sub_osc_;
  TransientGenerator transient_generator_;

  Envelope envelope_[kNumEnvelopes];
  uint8_t gate_;
  Lfo voice_lfo_;
  uint8_t modulation_sources_[kNumModulationSources];
  int8_t modulation_destinations_[kNumPluginModDestinations];
  int16_t dst_[kNumPluginModDestinations];

  // Pitches are stored on 14 bits: 7 bits of MIDI note, 7 bits of fine tune.
  int16_t pitch_increment_;
  int16_t pitch_target_;
  int16_t pitch_value_;

  uint8_t buffer_[kAudioBlockSize];
  uint8_t osc2_buffer_[kAudioBlockSize];
  uint8_t sync_state_[kAudioBlockSize];
  uint8_t no_sync_[kAudioBlockSize];
  uint8_t dummy_sync_state_[kAudioBlockSize];
  uint8_t output_[kAudioBlockSize];

  DISALLOW_COPY_AND_ASSIGN(VoiceCard);
};

}  // namespace plugin
}  // namespace ambika

#endif  // AMBIKA_PLUGIN_VOICE_CARD_H_
