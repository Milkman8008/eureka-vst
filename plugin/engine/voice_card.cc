// Copyright 2011 Emilie Gillet (original voicecard firmware).
// Plug-in port: per-instance version of voicecard/voice.cc.
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// The code below follows voicecard/voice.cc line for line. Differences:
// - state lives in VoiceCard members instead of statics;
// - the block is written to output_ instead of the DAC ring buffer;
// - the voice LFO shape is clamped to the shapes the hardware UI allows
//   (the voicecard's LFO wavetable is a 2-byte placeholder in its resources).

#include "voice_card.h"

#include <string.h>

#include "avrlib/random.h"
#include "voicecard/resources.h"

using namespace avrlib;

namespace ambika {
namespace plugin {

namespace {

const prog_Patch init_patch PROGMEM = {
  // Oscillators
  WAVEFORM_NONE, 0, 0, 0,
  WAVEFORM_NONE, 0, 0, 0,

  // Mixer
  32, OP_SUM, 0, WAVEFORM_SUB_OSC_SQUARE_1, 0, 0, 0, 0,

  // Filter
  127, 0, 0, 0, 0, 0, 63, 0,
  // ADSR
  0, 40, 20, 60, 0, 0, 0, 0,
  0, 40, 20, 60, 0, 0, 0, 0,
  0, 40, 20, 60, 0, 0, 0, 0,

  LFO_WAVEFORM_TRIANGLE, 16,

  // Routing
  MOD_SRC_LFO_1, MOD_DST_OSC_1, 0,
  MOD_SRC_ENV_1, MOD_DST_OSC_2, 0,
  MOD_SRC_LFO_1, MOD_DST_OSC_1, 0,
  MOD_SRC_ENV_1, MOD_DST_OSC_2, 0,
  MOD_SRC_ENV_1, MOD_DST_PARAMETER_1, 0,
  MOD_SRC_LFO_1, MOD_DST_PARAMETER_2, 0,
  MOD_SRC_LFO_2, MOD_DST_MIX_BALANCE, 0,
  MOD_SRC_LFO_4, MOD_DST_PARAMETER_1, 63,
  MOD_SRC_SEQ_1, MOD_DST_PARAMETER_1, 0,
  MOD_SRC_SEQ_2, MOD_DST_PARAMETER_2, 0,
  MOD_SRC_ENV_2, MOD_DST_VCA, 32,
  MOD_SRC_VELOCITY, MOD_DST_VCA, 0,
  MOD_SRC_PITCH_BEND, MOD_DST_OSC_1_2_COARSE, 0,
  MOD_SRC_LFO_1, MOD_DST_OSC_1_2_COARSE, 0,

  // Modifiers
  0, 0, 0,
  0, 0, 0,
  0, 0, 0,
  0, 0, 0,

  // Padding
  0, 0, 0, 0, 0, 0, 0, 0,
};

template<typename T>
void ZeroFill(T* object) {
  memset(static_cast<void*>(object), 0, sizeof(T));
}

}  // namespace

void VoiceCard::Init() {
  // The firmware's globals start zeroed; the plug-in objects do not.
  ZeroFill(&patch_);
  ZeroFill(&part_);
  ZeroFill(&osc_1_);
  ZeroFill(&osc_2_);
  sub_osc_.Reset();
  transient_generator_.Reset();
  for (uint8_t i = 0; i < kNumEnvelopes; ++i) {
    ZeroFill(&envelope_[i]);
  }
  ZeroFill(&voice_lfo_);
  gate_ = 0;
  memset(modulation_sources_, 0, sizeof(modulation_sources_));
  memset(modulation_destinations_, 0, sizeof(modulation_destinations_));
  memset(dst_, 0, sizeof(dst_));
  pitch_increment_ = pitch_target_ = 0;
  memset(buffer_, 0, sizeof(buffer_));
  memset(osc2_buffer_, 0, sizeof(osc2_buffer_));
  memset(sync_state_, 0, sizeof(sync_state_));
  memset(dummy_sync_state_, 0, sizeof(dummy_sync_state_));
  memset(output_, 128, sizeof(output_));

  // Voice::Init()
  pitch_value_ = 0;
  for (uint8_t i = 0; i < kNumEnvelopes; ++i) {
    envelope_[i].Init();
  }
  memset(no_sync_, 0, kAudioBlockSize);
  ResourcesManager::Load(&init_patch, 0, &patch_);
  ResetAllControllers();
  part_.volume = 127;
  part_.portamento_time = 0;
  part_.legato = 0;
  Kill();

  // Envelope rates are only computed in ProcessBlock(). On the hardware the
  // voicecard has been running blocks long before the first note arrives;
  // without this, a note sent before the first block would get an attack
  // rate of 0 and stay silent.
  ProcessBlock();
}

void VoiceCard::ResetAllControllers() {
  modulation_sources_[MOD_SRC_PITCH_BEND] = 128;
  modulation_sources_[MOD_SRC_AFTERTOUCH] = 0;
  modulation_sources_[MOD_SRC_WHEEL] = 0;
  modulation_sources_[MOD_SRC_WHEEL_2] = 0;
  modulation_sources_[MOD_SRC_EXPRESSION] = 0;
  modulation_sources_[MOD_SRC_CONSTANT_4] = 4;
  modulation_sources_[MOD_SRC_CONSTANT_8] = 8;
  modulation_sources_[MOD_SRC_CONSTANT_16] = 16;
  modulation_sources_[MOD_SRC_CONSTANT_32] = 32;
  modulation_sources_[MOD_SRC_CONSTANT_64] = 64;
  modulation_sources_[MOD_SRC_CONSTANT_128] = 128;
  modulation_sources_[MOD_SRC_CONSTANT_256] = 255;
}

void VoiceCard::TriggerEnvelope(uint8_t stage) {
  for (uint8_t i = 0; i < kNumEnvelopes; ++i) {
    TriggerEnvelope(i, stage);
  }
}

void VoiceCard::TriggerEnvelope(uint8_t index, uint8_t stage) {
  envelope_[index].Trigger(stage);
}

void VoiceCard::Trigger(uint16_t note, uint8_t velocity, uint8_t legato) {
  pitch_target_ = note;
  if (!part_.legato || !legato) {
    gate_ = 255;
    TriggerEnvelope(ATTACK);
    transient_generator_.Trigger();
    modulation_sources_[MOD_SRC_VELOCITY] = velocity;
    modulation_sources_[MOD_SRC_RANDOM] = Random::state_msb();
    osc_2_.Reset();
  }
  if (pitch_value_ == 0 || (part_.legato && !legato)) {
    pitch_value_ = pitch_target_;
  }
  int16_t delta = pitch_target_ - pitch_value_;
  int32_t increment = ResourcesManager::Lookup<uint16_t, uint8_t>(
      lut_res_env_portamento_increments,
      part_.portamento_time);
  pitch_increment_ = (delta * increment) >> 16;
  if (pitch_increment_ == 0) {
    if (delta < 0) {
      pitch_increment_ = -1;
    } else {
      pitch_increment_ = 1;
    }
  }
}

void VoiceCard::Release() {
  gate_ = 0;
  TriggerEnvelope(RELEASE);
}

void VoiceCard::LoadSources() {
  uint8_t ops[9];

  // Rescale the value of each modulation sources. Envelopes are in the
  // 0-16383 range ; just like pitch. All are scaled to 0-255.
  modulation_sources_[MOD_SRC_NOISE] = Random::GetByte();
  modulation_sources_[MOD_SRC_ENV_1] = envelope_[0].Render();
  modulation_sources_[MOD_SRC_ENV_2] = envelope_[1].Render();
  modulation_sources_[MOD_SRC_ENV_3] = envelope_[2].Render();
  modulation_sources_[MOD_SRC_NOTE] = U14ShiftRight6(pitch_value_);
  modulation_sources_[MOD_SRC_GATE] = gate_;
  uint8_t voice_lfo_shape = patch_.voice_lfo_shape;
  if (voice_lfo_shape > LFO_WAVEFORM_RAMP) {
    voice_lfo_shape = LFO_WAVEFORM_RAMP;
  }
  modulation_sources_[MOD_SRC_LFO_4] = voice_lfo_.Render(voice_lfo_shape);

  // Apply the modulation operators
  for (uint8_t i = 0; i < kNumModifiers; ++i) {
    if (!patch_.modifier[i].op) {
      continue;
    }
    uint8_t x = patch_.modifier[i].operands[0];
    uint8_t y = patch_.modifier[i].operands[1];
    if (x >= kNumModulationSources) x = MOD_SRC_CONSTANT_4;
    if (y >= kNumModulationSources) y = MOD_SRC_CONSTANT_4;
    x = modulation_sources_[x];
    y = modulation_sources_[y];
    uint8_t op = patch_.modifier[i].op;
    if (op <= MODIFIER_LE) {
      if (x > y) {
        ops[4] = x;  ops[7] = 255;
        ops[5] = y;  ops[8] = 0;
      } else {
        ops[4] = y;  ops[7] = 0;
        ops[5] = x;  ops[8] = 255;
      }
      ops[1] = (x >> 1) + (y >> 1);
      ops[2] = U8U8MulShift8(x, y);
      ops[3] = S8U8MulShift8(x + 128, y) + 128;
      ops[6] = x ^ y;
      modulation_sources_[MOD_SRC_OP_1 + i] = ops[op];
    } else if (op == MODIFIER_QUANTIZE) {
      uint8_t mask = 0;
      while (y >>= 1) {
        mask >>= 1;
        mask |= 0x80;
      }
      modulation_sources_[MOD_SRC_OP_1 + i] = x & mask;
    } else if (op == MODIFIER_LAG_PROCESSOR) {
      y >>= 2;
      ++y;
      uint16_t v = U8U8Mul(256 - y, modulation_sources_[MOD_SRC_OP_1 + i]);
      v += U8U8Mul(y, x);
      modulation_sources_[MOD_SRC_OP_1 + i] = v >> 8;
    }
  }

  modulation_destinations_[MOD_DST_VCA] = part_.volume << 1;

  // Load and scale to 0-16383 the initial value of each modulated parameter.
  dst_[MOD_DST_OSC_1] = dst_[MOD_DST_OSC_2] = 8192;
  dst_[MOD_DST_OSC_1_2_COARSE] = dst_[MOD_DST_OSC_1_2_FINE] = 8192;
  dst_[MOD_DST_PARAMETER_1] = U8U8Mul(patch_.osc[0].parameter, 128);
  dst_[MOD_DST_PARAMETER_2] = U8U8Mul(patch_.osc[1].parameter, 128);

  dst_[MOD_DST_MIX_BALANCE] = patch_.mix_balance << 8;
  dst_[MOD_DST_MIX_PARAM] = patch_.mix_parameter << 8;
  dst_[MOD_DST_MIX_FUZZ] = patch_.mix_fuzz << 8;
  dst_[MOD_DST_MIX_CRUSH] = patch_.mix_crush << 8;
  dst_[MOD_DST_MIX_NOISE] = patch_.mix_noise << 8;
  dst_[MOD_DST_MIX_SUB_OSC] = patch_.mix_sub_osc << 8;

  uint16_t cutoff = U8U8Mul(patch_.filter[0].cutoff, 128);
  dst_[MOD_DST_FILTER_CUTOFF] = S16ClipU14(cutoff + pitch_value_ - 8192);
  dst_[MOD_DST_FILTER_RESONANCE] = patch_.filter[0].resonance << 8;

  dst_[MOD_DST_ATTACK] = 8192;
  dst_[MOD_DST_DECAY] = 8192;
  dst_[MOD_DST_RELEASE] = 8192;
  dst_[MOD_DST_LFO_4] = U8U8Mul(patch_.voice_lfo_rate, 128);
  // Plug-in: pan, from the part's pan setting (64 = centre -> 8192).
  uint8_t pan = part_.padding[kPartDataPan - 1];
  if (pan > 127) pan = 127;
  dst_[kModDstPan] = U8U8Mul(pan, 128) + (pan == 127 ? 127 : 0);
}

void VoiceCard::ProcessModulationMatrix() {
  // Apply the modulations in the modulation matrix.
  for (uint8_t i = 0; i < kNumModulations; ++i) {
    int8_t amount = patch_.modulation[i].amount;

    // The rate of the last modulation is adjusted by the wheel.
    if (i == kNumModulations - 1) {
      amount = S8U8MulShift8(amount, modulation_sources_[MOD_SRC_WHEEL]);
    }
    uint8_t source = patch_.modulation[i].source;
    uint8_t destination = patch_.modulation[i].destination;
    // Out-of-range bytes can only come from a corrupt or hand-edited patch;
    // the firmware would read past the array, the plug-in ignores the slot.
    if (source >= kNumModulationSources ||
        destination >= kNumPluginModDestinations) {
      continue;
    }
    uint8_t source_value = modulation_sources_[source];
    if (destination != MOD_DST_VCA) {
      int16_t modulation = dst_[destination];
      if ((source >= MOD_SRC_LFO_1 && source <= MOD_SRC_LFO_4) ||
           source == MOD_SRC_PITCH_BEND ||
           source == MOD_SRC_NOTE) {
        // These sources are "AC-coupled" (128 = no modulation).
        modulation += S8S8Mul(amount, source_value + 128);
      } else {
        modulation += S8U8Mul(amount, source_value);
      }
      dst_[destination] = S16ClipU14(modulation);
    } else {
      // The VCA modulation is multiplicative, not additive.
      if (amount < 0) {
        amount = -amount;
        source_value = 255 - source_value;
      }
      if (amount != 63) {
        source_value = U8Mix(255, source_value, amount << 2);
      }
      modulation_destinations_[MOD_DST_VCA] = U8U8MulShift8(
            modulation_destinations_[MOD_DST_VCA],
            source_value);
    }
  }
}

void VoiceCard::UpdateDestinations() {
  // Hardcoded filter modulations.
  uint16_t cutoff = dst_[MOD_DST_FILTER_CUTOFF];
  cutoff = S16ClipU14(cutoff + S8U8Mul(patch_.filter_env,
      modulation_sources_[MOD_SRC_ENV_2]));
  cutoff = S16ClipU14(cutoff + S8S8Mul(patch_.filter_lfo,
      modulation_sources_[MOD_SRC_LFO_2] + 128));

  // Store in memory all the updated parameters.
  modulation_destinations_[MOD_DST_FILTER_CUTOFF] = U14ShiftRight6(cutoff);
  modulation_destinations_[MOD_DST_FILTER_RESONANCE] = U14ShiftRight6(
      dst_[MOD_DST_FILTER_RESONANCE]);
  modulation_destinations_[MOD_DST_MIX_CRUSH] = (
      dst_[MOD_DST_MIX_CRUSH] >> 8) + 1;
  modulation_destinations_[kModDstPan] = U14ShiftRight6(dst_[kModDstPan]);

  osc_1_.set_parameter(U15ShiftRight7(dst_[MOD_DST_PARAMETER_1]));
  osc_1_.set_fm_parameter(patch_.osc[0].range + 36);
  osc_2_.set_parameter(U15ShiftRight7(dst_[MOD_DST_PARAMETER_2]));
  osc_2_.set_fm_parameter(patch_.osc[1].range + 36);

  int8_t attack_mod = U15ShiftRight7(dst_[MOD_DST_ATTACK]) - 64;
  int8_t decay_mod = U15ShiftRight7(dst_[MOD_DST_DECAY]) - 64;
  int8_t release_mod = U15ShiftRight7(dst_[MOD_DST_RELEASE]) - 64;
  for (int i = 0; i < kNumEnvelopes; ++i) {
    int16_t new_attack = patch_.env_lfo[i].attack;
    new_attack = Clip(new_attack + attack_mod, 0, 127);
    int16_t new_decay = patch_.env_lfo[i].decay;
    new_decay = Clip(new_decay + decay_mod, 0, 127);
    int16_t new_release = patch_.env_lfo[i].release;
    new_release = Clip(new_release + release_mod, 0, 127);
    envelope_[i].Update(
          new_attack,
          new_decay,
          patch_.env_lfo[i].sustain,
          new_release);
  }

  voice_lfo_.set_phase_increment(
      ResourcesManager::Lookup<uint16_t, uint8_t>(
          lut_res_lfo_increments, U14ShiftRight6(dst_[MOD_DST_LFO_4]) >> 1));
}

void VoiceCard::RenderOscillators() {
  // Apply portamento.
  int16_t base_pitch = pitch_value_ + pitch_increment_;
  if ((pitch_increment_ > 0) ^ (base_pitch < pitch_target_)) {
    base_pitch = pitch_target_;
    pitch_increment_ = 0;
  }
  pitch_value_ = base_pitch;

  // -4 / +4 semitones by the vibrato and pitch bend (coarse).
  // -0.5 / +0.5 semitones by the vibrato and pitch bend (fine).
  base_pitch += (dst_[MOD_DST_OSC_1_2_COARSE] - 8192) >> 4;
  base_pitch += (dst_[MOD_DST_OSC_1_2_FINE] - 8192) >> 7;

  // Update the oscillator parameters.
  for (uint8_t i = 0; i < kNumOscillators; ++i) {
    int16_t pitch = base_pitch;
    // -36 / +36 semitones by the range controller.
    if (patch_.osc[i].shape != WAVEFORM_FM) {
      pitch += S8U8Mul(patch_.osc[i].range, 128);
    }
    // -1 / +1 semitones by the detune controller.
    pitch += patch_.osc[i].detune;
    // -16 / +16 semitones by the routed modulations.
    pitch += (dst_[MOD_DST_OSC_1 + i] - 8192) >> 2;

    if (pitch >= kHighestNote) {
      pitch = kHighestNote;
    }
    // Extract the pitch increment from the pitch table.
    int16_t ref_pitch = pitch - kPitchTableStart;
    uint8_t num_shifts = 0;
    while (ref_pitch < 0) {
      ref_pitch += kOctave;
      ++num_shifts;
    }
    uint24_t increment;
    increment.integral = ResourcesManager::Lookup<uint16_t, uint16_t>(
        lut_res_oscillator_increments, ref_pitch >> 1);
    increment.fractional = 0;
    // Divide the pitch increment by the number of octaves we had to transpose
    // to get a value in the lookup table.
    while (num_shifts--) {
      increment = U24ShiftRight(increment);
    }

    // Now the oscillators can recompute all their internal variables!
    int8_t midi_note = U15ShiftRight7(pitch) - 12;
    if (midi_note < 0) {
      midi_note = 0;
    }
    if (i == 0) {
      sub_osc_.set_increment(U24ShiftRight(increment));
      osc_1_.Render(
          patch_.osc[0].shape,
          midi_note,
          increment,
          no_sync_,
          sync_state_,
          buffer_);
    } else {
      osc_2_.Render(
          patch_.osc[1].shape,
          midi_note,
          increment,
          patch_.mix_op == OP_SYNC ? sync_state_ : no_sync_,
          dummy_sync_state_,
          osc2_buffer_);
    }
  }
}

void VoiceCard::ProcessBlock() {
  LoadSources();
  ProcessModulationMatrix();
  UpdateDestinations();

  // Skip the oscillator rendering code if the VCA output has converged to
  // a small value.
  if (vca() < 2) {
    memset(output_, 128, kAudioBlockSize);
    return;
  }

  RenderOscillators();
  uint8_t op = patch_.mix_op;
  uint8_t osc_2_gain = U14ShiftRight6(dst_[MOD_DST_MIX_BALANCE]);
  uint8_t osc_1_gain = ~osc_2_gain;
  uint8_t wet_gain = U14ShiftRight6(dst_[MOD_DST_MIX_PARAM]);
  uint8_t dry_gain = ~wet_gain;

  // Mix oscillators.
  switch (op) {
    case OP_RING_MOD:
      for (uint8_t i = 0; i < kAudioBlockSize; ++i) {
        uint8_t mix = U8Mix(
            buffer_[i],
            osc2_buffer_[i],
            osc_1_gain,
            osc_2_gain);
        uint8_t ring = S8S8MulShift8(
            buffer_[i] + 128,
            osc2_buffer_[i] + 128) + 128;
        buffer_[i] = U8Mix(mix, ring, dry_gain, wet_gain);
      }
      break;
    case OP_XOR:
      for (uint8_t i = 0; i < kAudioBlockSize; ++i) {
        uint8_t mix = U8Mix(
            buffer_[i],
            osc2_buffer_[i],
            osc_1_gain,
            osc_2_gain);
        uint8_t xord = buffer_[i] ^ osc2_buffer_[i];
        buffer_[i] = U8Mix(mix, xord, dry_gain, wet_gain);
      }
      break;
    case OP_FOLD:
      for (uint8_t i = 0; i < kAudioBlockSize; ++i) {
        uint8_t mix = U8Mix(
            buffer_[i],
            osc2_buffer_[i],
            osc_1_gain,
            osc_2_gain);
        buffer_[i] = U8Mix(mix, mix + 128, dry_gain, wet_gain);
      }
      break;
    case OP_BITS:
      {
        wet_gain >>= 5;
        wet_gain = 255 - ((1 << wet_gain) - 1);
        for (uint8_t i = 0; i < kAudioBlockSize; ++i) {
          buffer_[i] = U8Mix(
              buffer_[i],
              osc2_buffer_[i],
              osc_1_gain,
              osc_2_gain) & wet_gain;
        }
        break;
      }
    default:
      for (uint8_t i = 0; i < kAudioBlockSize; ++i) {
        buffer_[i] = U8Mix(buffer_[i], osc2_buffer_[i], osc_1_gain, osc_2_gain);
      }
      break;
  }

  // Mix-in sub oscillator or transient generator.
  uint8_t sub_gain = U15ShiftRight7(dst_[MOD_DST_MIX_SUB_OSC]);
  if (patch_.mix_sub_osc_shape < WAVEFORM_SUB_OSC_CLICK) {
    sub_osc_.Render(patch_.mix_sub_osc_shape, buffer_, sub_gain);
  } else {
    sub_gain <<= 1;
    transient_generator_.Render(patch_.mix_sub_osc_shape, buffer_, sub_gain);
  }

  uint8_t noise = Random::state_msb();
  uint8_t noise_gain = U15ShiftRight7(dst_[MOD_DST_MIX_NOISE]);
  uint8_t signal_gain = ~noise_gain;
  wet_gain = U14ShiftRight6(dst_[MOD_DST_MIX_FUZZ]);
  dry_gain = ~wet_gain;

  // Mix with noise, and apply distortion.
  for (uint8_t i = 0; i < kAudioBlockSize; ++i) {
    noise = (noise * 73) + 1;
    uint8_t signal_noise = U8Mix(buffer_[i], noise, signal_gain, noise_gain);
    output_[i] = U8Mix(
        signal_noise,
        ResourcesManager::Lookup<uint8_t, uint8_t>(
            wav_res_distortion, signal_noise),
        dry_gain, wet_gain);
  }
}

}  // namespace plugin
}  // namespace ambika
