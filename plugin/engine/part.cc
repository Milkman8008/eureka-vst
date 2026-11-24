// Copyright 2011 Emilie Gillet (original controller firmware).
// Plug-in port of the voice-driving parts of controller/part.cc.
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// In the firmware, the controller talks to the voicecards through
// voicecard_tx (SPI). Here the same calls go straight to VoiceCard objects.

#include "part.h"

#include <string.h>

#include "avrlib/op.h"
#include "avrlib/random.h"
#include "voicecard/resources.h"

#include "controller_tables.h"
#include "parameters.h"

using namespace avrlib;

namespace ambika {
namespace plugin {

namespace {

// MIDI controller numbers handled by Part::ControlChange (midi/midi.h).
const uint8_t kModulationWheelMsb = 0x01;
const uint8_t kBreathController = 0x02;
const uint8_t kFootPedalMsb = 0x04;
const uint8_t kHoldPedal = 0x40;

}  // namespace

void Part::Init(VoiceCard* voices, uint8_t index) {
  voices_ = voices;
  index_ = index;
  note_sink_ = nullptr;
  transport_running_ = false;
  previous_generated_note_ = 0xff;
  num_allocated_voices_ = 0;
  memset(controller_values_, 0, sizeof(controller_values_));
  controller_values_[MOD_SRC_PITCH_BEND] = 128;
  for (uint8_t i = 0; i < kNumLfos; ++i) {
    memset(static_cast<void*>(&lfo_[i]), 0, sizeof(Lfo));
    lfo_step_[i] = 0;
    lfo_cycle_length_[i] = 1;
    lfo_previous_values_[i] = 0;
  }
  midi_clock_tick_duration_ = 0;
  ignore_note_off_messages_ = 0;
  pressed_keys_.Init();
  mono_allocator_.Init();
  settings_ = PartSettings();
  InitializeAllocators();
  SetPatch(DefaultPatch());
  WritePartDataToAllVoices();
  Start(0);
}

void Part::AssignVoices(uint8_t mask) {
  AllSoundOff();
  num_allocated_voices_ = 0;
  for (uint8_t i = 0; i < kNumVoices; ++i) {
    if (mask & (1 << i)) {
      allocated_voices_[num_allocated_voices_++] = i;
    }
  }
  InitializeAllocators();
  for (uint8_t i = 0; i < num_allocated_voices_; ++i) {
    SyncVoice(voice(i));
  }
}

// Brings a voice that just joined this part up to date: patch, part data,
// LFO values and the controllers (bend, wheel...) last received.
void Part::SyncVoice(VoiceCard& v) {
  v.Kill();
  v.set_patch(patch_);
  v.set_part_data(0, settings_.volume);
  v.set_part_data(kPartDataPan, settings_.pan);
  v.set_part_data(5, settings_.legato);
  v.set_part_data(6, settings_.portamento_time);
  for (uint8_t i = 0; i < kNumLfos; ++i) {
    v.set_modulation_source(MOD_SRC_LFO_1 + i, lfo_previous_values_[i]);
  }
  static const uint8_t kControllers[] = {
    MOD_SRC_PITCH_BEND, MOD_SRC_AFTERTOUCH, MOD_SRC_WHEEL, MOD_SRC_WHEEL_2,
    MOD_SRC_EXPRESSION, MOD_SRC_SEQ_1, MOD_SRC_SEQ_2, MOD_SRC_ARP_STEP,
  };
  for (uint8_t source : kControllers) {
    v.set_modulation_source(source, controller_values_[source]);
  }
}

void Part::SetPatch(const Patch& patch) {
  patch_ = patch;
  uint8_t* bytes = reinterpret_cast<uint8_t*>(&patch_);
  for (uint8_t i = 0; i < sizeof(Patch); ++i) {
    bytes[i] = ClampPatchByte(i, bytes[i]);
  }
  for (uint8_t i = 0; i < num_allocated_voices_; ++i) {
    voice(i).set_patch(patch_);
  }
  TouchLfos();
}

uint8_t Part::GetPatchValue(uint8_t address) const {
  if (address >= sizeof(Patch)) {
    return 0;
  }
  return reinterpret_cast<const uint8_t*>(&patch_)[address];
}

void Part::SetPatchValue(uint8_t address, uint8_t value) {
  if (address >= sizeof(Patch)) {
    return;
  }
  value = ClampPatchByte(address, value);
  uint8_t* bytes = reinterpret_cast<uint8_t*>(&patch_);
  if (bytes[address] == value) {
    return;
  }
  bytes[address] = value;
  WritePatchToAllVoices(address, value);
  if (address >= PRM_PATCH_ENV_ATTACK && address < PRM_PATCH_VOICE_LFO_SHAPE) {
    TouchLfos();
  }
}

void Part::SetSettings(const PartSettings& settings) {
  const bool mode_changed = settings.polyphony_mode != settings_.polyphony_mode;
  const bool sequencer_changed = settings.arp_mode != settings_.arp_mode;
  const bool direction_changed = settings.arp_direction != settings_.arp_direction;
  if (sequencer_changed) {
    // Keys held under the old mode were routed differently: stop everything
    // rather than leave notes without their note-off.
    const uint8_t ignore = ignore_note_off_messages_;
    ignore_note_off_messages_ = 0;
    AllNotesOff();
    ignore_note_off_messages_ = ignore;
  }
  settings_ = settings;
  if (settings_.polyphony_mode >= POLYPHONY_MODE_LAST) {
    settings_.polyphony_mode = POLYPHONY_POLY;
  }
  if (settings_.arp_mode >= ARP_SEQUENCER_MODE_LAST) settings_.arp_mode = ARP_SEQUENCER_MODE_STEP;
  if (settings_.arp_direction >= ARPEGGIO_DIRECTION_LAST) settings_.arp_direction = ARPEGGIO_DIRECTION_UP;
  settings_.arp_octave = Clip(settings_.arp_octave, 1, 4);
  if (settings_.arp_pattern >= kNumArpeggiatorPatterns) settings_.arp_pattern = 0;
  if (settings_.arp_divider > 14) settings_.arp_divider = 14;
  for (uint8_t i = 0; i < kNumSequences; ++i) {
    settings_.sequence_length[i] = Clip(settings_.sequence_length[i], 1, kNumSteps);
    if (sequencer_step_[i] >= settings_.sequence_length[i]) sequencer_step_[i] = 0;
  }
  WritePartDataToAllVoices();
  TouchClock();
  if (direction_changed) {
    arp_direction_ = settings_.arp_direction == ARPEGGIO_DIRECTION_DOWN ? -1 : 1;
    StartArpeggio();
  }
  if (mode_changed) {
    AllSoundOff();
    InitializeAllocators();
  }
}

void Part::InitializeAllocators() {
  if (settings_.polyphony_mode == POLYPHONY_MONO) {
    mono_allocator_.Init();
  } else {
    uint8_t size = num_allocated_voices_;
    if (settings_.polyphony_mode == POLYPHONY_UNISON_2X) {
      size = (num_allocated_voices_ + 1) >> 1;
    }
    poly_allocator_.Init(
        size,
        settings_.polyphony_mode == POLYPHONY_CYCLIC,
        poly_pool_,
        poly_lru_);
  }
  polyphony_mode_ = settings_.polyphony_mode;
}

void Part::TouchLfos() {
  for (uint8_t i = 0; i < kNumLfos; ++i) {
    if (patch_.env_lfo[i].rate < kNumSyncedLfoRates) {
      lfo_cycle_length_[i] = ResourcesManager::Lookup<uint8_t, uint8_t>(
          midi_clock_tick_per_step, patch_.env_lfo[i].rate);
    } else {
      uint8_t rate = patch_.env_lfo[i].rate - kNumSyncedLfoRates;
      if (rate > 127) {
        rate = 127;
      }
      lfo_[i].set_phase_increment(ResourcesManager::Lookup<uint16_t, uint8_t>(
          lut_res_lfo_increments, rate));
    }
  }
}

void Part::NoteOn(uint8_t note, uint8_t velocity) {
  if (velocity == 0) {
    NoteOff(note);
    return;
  }
  if (sequencing() && pressed_keys_.size() == 0 && !transport_running_) {
    // Free-running clock: start the sequence on the first key, so it plays
    // at once and from its first step.
    Start(0);
  }
  pressed_keys_.NoteOn(note, velocity);
  if (!sequencing()) {
    // Sequencer and arpeggiator are off, we directly trigger the note.
    InternalNoteOn(note, velocity);
  }
}

void Part::NoteOff(uint8_t note) {
  if (ignore_note_off_messages_) {
    for (uint8_t i = 1; i <= pressed_keys_.max_size(); ++i) {
      // Flag the note so that it is removed once the sustain pedal is released.
      NoteEntry* e = pressed_keys_.mutable_note(i);
      if (e->note == note && e->velocity) {
        e->velocity |= 0x80;
      }
    }
    return;
  }
  pressed_keys_.NoteOff(note);
  if (!sequencing()) {
    // Sequencer and arpeggiator are off, we directly kill the note.
    InternalNoteOff(note);
  } else if (settings_.arp_mode == ARP_SEQUENCER_MODE_ARPEGGIATOR &&
             settings_.arp_direction == ARPEGGIO_DIRECTION_CHORD) {
    // Chord mode retriggers every held key: release this one now, to avoid
    // stuck notes (as the firmware does).
    GeneratedNoteOff(note);
    if (pressed_keys_.size() == 0) {
      previous_generated_note_ = 0xff;
    }
  } else if (pressed_keys_.size() == 0) {
    // The sequencer and arpeggiator might still have pending notes.
    AllNotesOff();
  }
}

void Part::ControlChange(uint8_t controller, uint8_t value) {
  switch (controller) {
    case kModulationWheelMsb:
      WriteModulationToAllVoices(MOD_SRC_WHEEL, value << 1);
      break;
    case kBreathController:
      WriteModulationToAllVoices(MOD_SRC_WHEEL_2, value << 1);
      break;
    case kFootPedalMsb:
      WriteModulationToAllVoices(MOD_SRC_EXPRESSION, value << 1);
      break;
    case kHoldPedal:
      if (value >= 64) {
        ignore_note_off_messages_ = 1;
      } else {
        // The pedal was released. Kill all the sustained notes.
        ignore_note_off_messages_ = 0;
        for (uint8_t i = 1; i <= pressed_keys_.max_size(); ++i) {
          NoteEntry* e = pressed_keys_.mutable_note(i);
          if (e->velocity & 0x80) {
            NoteOff(e->note);
          }
        }
      }
      break;
    default:
      break;
  }
}

void Part::PitchBend(uint16_t pitch_bend) {
  WriteModulationToAllVoices(MOD_SRC_PITCH_BEND, U14ShiftRight6(pitch_bend));
}

uint8_t Part::GetNextVoice(uint8_t index) const {
  if (index + 1 < num_allocated_voices_) {
    return index + 1;
  } else {
    return 0;
  }
}

void Part::Aftertouch(uint8_t note, uint8_t velocity) {
  if (settings_.polyphony_mode == POLYPHONY_POLY ||
      settings_.polyphony_mode == POLYPHONY_CYCLIC) {
    // Send the aftertouch change to the voicecard playing the affected note.
    uint8_t voice_index = poly_allocator_.Find(note);
    uint8_t size = poly_allocator_.size();
    if (settings_.polyphony_mode == POLYPHONY_CYCLIC) {
      size >>= 1;
    }
    if (voice_index < size) {
      voice(voice_index).set_modulation_source(MOD_SRC_AFTERTOUCH, velocity);
    }
  } else if (settings_.polyphony_mode == POLYPHONY_UNISON_2X) {
    uint8_t voice_index = poly_allocator_.Find(note);
    if (voice_index < poly_allocator_.size()) {
      voice_index <<= 1;
      voice(voice_index).set_modulation_source(MOD_SRC_AFTERTOUCH, velocity);
      voice(GetNextVoice(voice_index)).set_modulation_source(
          MOD_SRC_AFTERTOUCH, velocity);
    }
  } else {
    // Process the message as if it were a global aftertouch message.
    Aftertouch(velocity);
  }
}

void Part::Aftertouch(uint8_t velocity) {
  WriteModulationToAllVoices(MOD_SRC_AFTERTOUCH, velocity);
}

void Part::AllSoundOff() {
  if (polyphony_mode_ == POLYPHONY_MONO) {
    mono_allocator_.Clear();
  } else {
    poly_allocator_.Clear();
  }
  pressed_keys_.Clear();
  for (uint8_t i = 0; i < num_allocated_voices_; ++i) {
    voice(i).Kill();
  }
  previous_generated_note_ = 0xff;
}

void Part::AllNotesOff() {
  if (ignore_note_off_messages_) {
    return;
  }
  if (polyphony_mode_ == POLYPHONY_MONO) {
    mono_allocator_.Clear();
  } else {
    poly_allocator_.ClearNotes();
  }
  pressed_keys_.Clear();
  for (uint8_t i = 0; i < num_allocated_voices_; ++i) {
    voice(i).Release();
  }
  if (note_sink_ != nullptr) {
    note_sink_->PartAllNotesOff(index_);
  }
  previous_generated_note_ = 0xff;
}

void Part::PlayVoice(uint8_t voice, uint8_t note, uint8_t velocity, uint8_t unison_index) {
  const uint16_t tuned_note = TuneNote(note) + unison_index * settings_.spread;
  voices_[voice].Trigger(tuned_note, velocity << 1, 0);
  RetriggerLfos();
}

void Part::ReleaseVoice(uint8_t voice) {
  voices_[voice].Release();
}

void Part::VoiceAftertouch(uint8_t voice, uint8_t value) {
  voices_[voice].set_modulation_source(MOD_SRC_AFTERTOUCH, value);
}

void Part::ResetAllControllers() {
  ignore_note_off_messages_ = 0;
  memset(controller_values_, 0, sizeof(controller_values_));
  controller_values_[MOD_SRC_PITCH_BEND] = 128;
  for (uint8_t i = 0; i < num_allocated_voices_; ++i) {
    voice(i).ResetAllControllers();
  }
}

void Part::TouchClock() {
  midi_clock_prescaler_ = ResourcesManager::Lookup<uint8_t, uint8_t>(
      midi_clock_tick_per_step, settings_.arp_divider);
}

void Part::Start(uint32_t next_tick) {
  // Release whatever the sequencers were playing.
  if (previous_generated_note_ != 0xff) {
    if (settings_.arp_direction == ARPEGGIO_DIRECTION_CHORD &&
        settings_.arp_mode == ARP_SEQUENCER_MODE_ARPEGGIATOR) {
      for (uint8_t i = 0; i < pressed_keys_.size(); ++i) {
        GeneratedNoteOff(pressed_keys_.sorted_note(i).note);
      }
    } else {
      GeneratedNoteOff(previous_generated_note_);
    }
  }
  previous_generated_note_ = 0xff;
  TouchClock();
  // Steps already played before next_tick, and where the next one falls.
  const uint32_t prescaler = midi_clock_prescaler_;
  uint32_t step = next_tick / prescaler;
  const uint32_t phase = next_tick % prescaler;
  if (phase) {
    ++step;
  }
  midi_clock_counter_ = static_cast<uint8_t>(phase ? phase - 1 : prescaler - 1);
  for (uint8_t i = 0; i < kNumSequences; ++i) {
    const uint8_t length = settings_.sequence_length[i] ? settings_.sequence_length[i] : 1;
    sequencer_step_[i] = static_cast<uint8_t>(step % length);
    current_step_[i] = 0xff;
  }
  arp_pattern_mask_ = static_cast<uint16_t>(1 << (step % 16));
  current_rhythm_step_ = 0xff;
  arp_direction_ = settings_.arp_direction == ARPEGGIO_DIRECTION_DOWN ? -1 : 1;
  StartArpeggio();
  arp_restarted_ = true;
}

void Part::Stop() {
  ignore_note_off_messages_ = 0;
  if (sequencing()) {
    // Held keys stay held; only what the sequencers play stops.
    Start(0);
  }
}

void Part::GeneratedNoteOn(uint8_t note, uint8_t velocity) {
  if (note_sink_ != nullptr) {
    note_sink_->PartNoteOn(index_, note, velocity);
  } else {
    InternalNoteOn(note, velocity);
  }
}

void Part::GeneratedNoteOff(uint8_t note) {
  if (note == 0xff) {
    return;
  }
  if (note_sink_ != nullptr) {
    note_sink_->PartNoteOff(index_, note);
  } else {
    InternalNoteOff(note);
  }
}

void Part::ClockSequencer() {
  // Update the value of the sequencer in the modulation matrix.
  for (uint8_t i = 0; i < 2; ++i) {
    WriteModulationToAllVoices(MOD_SRC_SEQ_1 + i,
                               settings_.step_value[i][sequencer_step_[i]]);
  }

  // Trigger notes if there's a note sequence, and if a key is pressed on the
  // keyboard.
  if (settings_.arp_mode == ARP_SEQUENCER_MODE_NOTE && pressed_keys_.size()) {
    const uint8_t step = sequencer_step_[2];
    const uint8_t note = static_cast<uint8_t>(Clip(
        static_cast<int16_t>(settings_.pattern_note[step]) +
        pressed_keys_.most_recent_note().note - 60, 0, 127));
    const uint8_t velocity = settings_.pattern_velocity[step] & 0x7f;
    if (!settings_.pattern_gate[step]) {
      // Just kill the previous note.
      GeneratedNoteOff(previous_generated_note_);
      previous_generated_note_ = 0xff;
    } else {
      if (!settings_.pattern_legato[step]) {
        // Kill the previous note and move to the new note.
        GeneratedNoteOff(previous_generated_note_);
        GeneratedNoteOn(note, velocity);
      } else {
        // Kill the previous note, but only after having started playing the
        // new one.
        if (previous_generated_note_ != note) {
          GeneratedNoteOn(note, velocity);
          GeneratedNoteOff(previous_generated_note_);
        }
        // Else: this is just a note being held.
      }
      previous_generated_note_ = note;
    }
  }

  // Jump to the next step in the sequencer.
  for (uint8_t i = 0; i < kNumSequences; ++i) {
    current_step_[i] = sequencer_step_[i];
    ++sequencer_step_[i];
    if (sequencer_step_[i] >= settings_.sequence_length[i]) {
      sequencer_step_[i] = 0;
    }
  }
}

void Part::ClockArpeggiator() {
  const uint16_t pattern = ResourcesManager::Lookup<uint16_t, uint8_t>(
      arpeggiator_patterns, settings_.arp_pattern);
  const uint8_t has_arpeggiator_note = (arp_pattern_mask_ & pattern) ? 255 : 0;
  current_rhythm_step_ = 0;
  while (current_rhythm_step_ < 15 && !(arp_pattern_mask_ & (1 << current_rhythm_step_))) {
    ++current_rhythm_step_;
  }
  // Update the gate value in the modulation matrix.
  WriteModulationToAllVoices(MOD_SRC_ARP_STEP, has_arpeggiator_note);

  // Trigger notes only if the arp is on, and if keys are pressed.
  if (settings_.arp_mode == ARP_SEQUENCER_MODE_ARPEGGIATOR) {
    if (pressed_keys_.size() && has_arpeggiator_note) {
      if (settings_.arp_direction != ARPEGGIO_DIRECTION_CHORD) {
        GeneratedNoteOff(previous_generated_note_);
        if (arp_restarted_ && settings_.arp_direction != ARPEGGIO_DIRECTION_RANDOM) {
          // Plug-in change: after a (re)start, begin on the first note of the
          // arpeggio (the firmware steps first, so it began on the second).
          // Here the clock restarts with the first key, so it shows.
          StartArpeggio();
        } else {
          StepArpeggio();
        }
        arp_restarted_ = false;
        const NoteEntry* arpeggio_note = &pressed_keys_.sorted_note(arp_step_);
        if (settings_.arp_direction == ARPEGGIO_DIRECTION_AS_PLAYED) {
          arpeggio_note = &pressed_keys_.played_note(arp_step_);
        }
        uint8_t note = arpeggio_note->note;
        const uint8_t velocity = arpeggio_note->velocity & 0x7f;
        int16_t transposed = note + 12 * arp_octave_;
        while (transposed > 127) {
          transposed -= 12;
        }
        note = static_cast<uint8_t>(transposed);
        GeneratedNoteOn(note, velocity);
        previous_generated_note_ = note;
      } else {
        for (uint8_t i = 0; i < pressed_keys_.size(); ++i) {
          const NoteEntry* retriggered_note = &pressed_keys_.sorted_note(i);
          GeneratedNoteOn(retriggered_note->note, retriggered_note->velocity & 0x7f);
        }
        // This is arbitrary, and used only to know at the next blank step in
        // the sequence that we have to send a note off event.
        previous_generated_note_ = 60;
      }
    } else {
      if (settings_.arp_direction != ARPEGGIO_DIRECTION_CHORD) {
        GeneratedNoteOff(previous_generated_note_);
      } else if (previous_generated_note_ != 0xff) {
        for (uint8_t i = 0; i < pressed_keys_.size(); ++i) {
          GeneratedNoteOff(pressed_keys_.sorted_note(i).note);
        }
      }
      previous_generated_note_ = 0xff;
    }
  }

  arp_pattern_mask_ <<= 1;
  if (!arp_pattern_mask_) {
    arp_pattern_mask_ = 1;
  }
}

void Part::StartArpeggio() {
  if (arp_direction_ == 1) {
    arp_octave_ = 0;
    arp_step_ = 0;
  } else {
    arp_step_ = static_cast<int8_t>(pressed_keys_.size() - 1);
    arp_octave_ = static_cast<int8_t>(settings_.arp_octave - 1);
  }
}

void Part::StepArpeggio() {
  const uint8_t num_notes = pressed_keys_.size();
  if (settings_.arp_direction == ARPEGGIO_DIRECTION_RANDOM) {
    const uint8_t random_byte = Random::GetByte();
    arp_octave_ = random_byte & 0xf;
    arp_step_ = (random_byte & 0xf0) >> 4;
    while (arp_octave_ >= settings_.arp_octave) {
      arp_octave_ -= settings_.arp_octave;
    }
    while (arp_step_ >= num_notes) {
      arp_step_ -= num_notes;
    }
  } else {
    arp_step_ += arp_direction_;
    uint8_t change_octave = 0;
    if (arp_step_ >= num_notes) {
      arp_step_ = 0;
      change_octave = 1;
    } else if (arp_step_ < 0) {
      arp_step_ = static_cast<int8_t>(num_notes - 1);
      change_octave = 1;
    }
    if (change_octave) {
      arp_octave_ += arp_direction_;
      if (arp_octave_ >= settings_.arp_octave || arp_octave_ < 0) {
        if (settings_.arp_direction == ARPEGGIO_DIRECTION_UP_DOWN) {
          arp_direction_ = -arp_direction_;
          StartArpeggio();
          if (num_notes > 1 || settings_.arp_octave > 1) {
            StepArpeggio();
          }
        } else {
          StartArpeggio();
        }
      }
    }
  }
}

void Part::Clock() {
  ++midi_clock_counter_;
  if (midi_clock_counter_ >= midi_clock_prescaler_) {
    midi_clock_counter_ = 0;
    ClockSequencer();
    ClockArpeggiator();
  }

  for (uint8_t i = 0; i < kNumLfos; ++i) {
    if (patch_.env_lfo[i].rate < kNumSyncedLfoRates) {
      ++lfo_step_[i];
      if (lfo_step_[i] >= lfo_cycle_length_[i]) {
        lfo_step_[i] = 0;
      }
      uint16_t increment = ResourcesManager::Lookup<uint16_t, uint8_t>(
          lfo_phase_increment_per_clock_tick, patch_.env_lfo[i].rate);
      uint16_t lfo_phase = increment * lfo_step_[i];
      // Force the phase of the LFO to match the MIDI clock.
      lfo_[i].set_phase(lfo_phase);
      // Set the LFO increment so that, if the MIDI clock is regular, we will
      // have reached the expected phase at the next clock tick.
      uint16_t duration = midi_clock_tick_duration_ ? midi_clock_tick_duration_ : 1;
      lfo_[i].set_phase_increment(increment / duration);
    }
  }
  midi_clock_tick_duration_ = 0;
}

uint16_t Part::TuneNote(uint8_t midi_note) const {
  int16_t n = Clip(
      static_cast<int16_t>(midi_note) + S8U8Mul(settings_.octave, 12), 0, 127);
  int16_t note = U8U8Mul(n, 128);
  // Apply part tuning.
  note += settings_.tuning;
  return note;
}

void Part::InternalNoteOn(uint8_t note, uint8_t velocity) {
  // The voicecard protocol sends velocity << 1 (voicecard_tx.cc).
  const uint8_t voice_velocity = velocity << 1;
  uint8_t retrigger_lfos = 0;
  if (settings_.polyphony_mode == POLYPHONY_MONO) {
    mono_allocator_.NoteOn(note, velocity);
    uint16_t tuned_note = TuneNote(note);
    uint8_t legato = mono_allocator_.size() > 1;
    uint8_t pitch_drift = 0;
    for (uint8_t i = 0; i < num_allocated_voices_; ++i) {
      voice(i).Trigger(tuned_note + pitch_drift, voice_velocity, legato);
      pitch_drift += settings_.spread;
    }
    retrigger_lfos = !legato || !settings_.legato;
  } else {
    // Prevent the same note to be allocated twice on two different voices.
    uint8_t voice_index = poly_allocator_.FindActive(note);
    if (voice_index == 0xff) {
      voice_index = poly_allocator_.NoteOn(note);
    }
    if (settings_.polyphony_mode == POLYPHONY_UNISON_2X) {
      if (voice_index < poly_allocator_.size()) {
        voice_index <<= 1;
        uint16_t tuned_note = TuneNote(note);
        voice(voice_index).Trigger(tuned_note, voice_velocity, 0);
        voice(GetNextVoice(voice_index)).Trigger(
            tuned_note + settings_.spread, voice_velocity, 0);
        retrigger_lfos = 1;
      }
    } else {
      if (voice_index < poly_allocator_.size()) {
        uint16_t tuned_note = TuneNote(note);
        voice(voice_index).Trigger(
            tuned_note + voice_index * settings_.spread, voice_velocity, 0);
        retrigger_lfos = 1;
      }
    }
  }

  // A note was played, we can retrigger the LFOs which are synced to the
  // envelopes.
  if (retrigger_lfos) {
    RetriggerLfos();
  }
}

void Part::InternalNoteOff(uint8_t note) {
  if (note == 0xff) {
    return;
  }
  uint8_t retrigger_lfos = 0;
  if (settings_.polyphony_mode == POLYPHONY_MONO) {
    uint8_t top_note = mono_allocator_.most_recent_note().note;
    mono_allocator_.NoteOff(note);
    if (mono_allocator_.size() == 0) {
      // No key is pressed, we trigger the release segment.
      for (uint8_t i = 0; i < num_allocated_voices_; ++i) {
        voice(i).Release();
      }
    } else {
      // We have released the most recent pressed key, but some other keys
      // are still pressed. Retrigger the most recent of them.
      if (top_note == note) {
        uint16_t tuned_note = TuneNote(mono_allocator_.most_recent_note().note);
        uint8_t velocity = mono_allocator_.most_recent_note().velocity & 0x7f;
        uint8_t pitch_drift = 0;
        for (uint8_t i = 0; i < num_allocated_voices_; ++i) {
          voice(i).Trigger(tuned_note + pitch_drift, velocity << 1, 1);
          pitch_drift += settings_.spread;
        }
        retrigger_lfos = !settings_.legato;
      }
    }
  } else {
    uint8_t voice_index = poly_allocator_.NoteOff(note);
    if (settings_.polyphony_mode == POLYPHONY_UNISON_2X) {
      if (voice_index < poly_allocator_.size()) {
        voice_index <<= 1;
        voice(voice_index).Release();
        voice(GetNextVoice(voice_index)).Release();
      }
    } else {
      if (voice_index < poly_allocator_.size()) {
        voice(voice_index).Release();
      }
    }
  }
  if (retrigger_lfos) {
    RetriggerLfos();
  }
}

void Part::UpdateLfos(uint8_t refresh_cycle) {
  ++midi_clock_tick_duration_;
  if (num_allocated_voices_ == 0) {
    return;
  }
  for (uint8_t i = 0; i < kNumLfos; ++i) {
    uint8_t shape = patch_.env_lfo[i].shape;
    if (shape >= LFO_WAVEFORM_LAST) {
      shape = LFO_WAVEFORM_TRIANGLE;
    }
    uint8_t new_lfo_value = lfo_[i].Render(shape);
    // In order to avoid flooding the voicecard with too many LFO messages,
    // LFO2 and LFO3 are refreshed at half the control rate. This makes LFO1
    // better for pseudo-audio rate modulation!
    if ((i == 0) || refresh_cycle) {
      if (new_lfo_value != lfo_previous_values_[i]) {
        for (uint8_t j = 0; j < num_allocated_voices_; ++j) {
          voice(j).set_modulation_source(MOD_SRC_LFO_1 + i, new_lfo_value);
        }
      }
      lfo_previous_values_[i] = new_lfo_value;
    }
    if (patch_.env_lfo[i].retrigger_mode == LFO_SYNC_MODE_MASTER) {
      // Detect LFO cycle completion - by comparing the phase to the increment
      // if the LFO is free running, or by looking the step index if the LFO
      // is tied to the MIDI clock.
      uint8_t lfo_looped = lfo_[i].looped();
      if (patch_.env_lfo[i].rate < kNumSyncedLfoRates) {
        lfo_looped = lfo_step_[i] == 0;
      }
      if (lfo_looped) {
        for (uint8_t j = 0; j < num_allocated_voices_; ++j) {
          voice(j).TriggerEnvelope(i, ATTACK);
        }
      }
    }
  }
}

void Part::RetriggerLfos() {
  for (uint8_t i = 0; i < kNumLfos; ++i) {
    if (patch_.env_lfo[i].retrigger_mode == LFO_SYNC_MODE_SLAVE) {
      lfo_[i].set_phase(0);
      lfo_step_[i] = 0;
    }
  }
}

void Part::WritePatchToAllVoices(uint8_t address, uint8_t value) {
  for (uint8_t i = 0; i < num_allocated_voices_; ++i) {
    voice(i).set_patch_data(address, value);
  }
}

void Part::WritePartDataToAllVoices() {
  for (uint8_t i = 0; i < num_allocated_voices_; ++i) {
    VoiceCard& v = voice(i);
    v.set_part_data(0, settings_.volume);
    v.set_part_data(kPartDataPan, settings_.pan);
    v.set_part_data(5, settings_.legato);
    v.set_part_data(6, settings_.portamento_time);
  }
}

void Part::WriteModulationToAllVoices(uint8_t source, uint8_t value) {
  controller_values_[source] = value;
  for (uint8_t i = 0; i < num_allocated_voices_; ++i) {
    voice(i).set_modulation_source(source, value);
  }
}

}  // namespace plugin
}  // namespace ambika
