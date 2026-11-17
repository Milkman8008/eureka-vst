// Copyright 2011 Emilie Gillet (original controller firmware).
// Plug-in port: the subset of controller/part.{h,cc} that drives the voices.
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// The hardware Part also handles the arpeggiator, step sequencer, MIDI
// forwarding, ragas and the CC/NRPN-to-parameter map. Those are not part of
// this first version. What is here follows controller/part.cc: note handling,
// sustain pedal, voice allocation for MONO/POLY/UNISON_2X/CYCLIC, tuning,
// spread, and the three part LFOs (free-running or clock-synced).

#ifndef AMBIKA_PLUGIN_PART_H_
#define AMBIKA_PLUGIN_PART_H_

#include "avrlib/base.h"

#include "common/lfo.h"
#include "common/patch.h"
#include "controller/note_stack.h"
#include "controller/voice_allocator.h"

#include "voice_card.h"

namespace ambika {
namespace plugin {

static const uint8_t kNumVoices = 6;

// Same order as PolyphonyMode in controller/part.h. CHAIN (forwarding notes
// to a second Ambika over MIDI) has no meaning inside a plug-in.
enum PolyphonyMode {
  POLYPHONY_MONO,
  POLYPHONY_POLY,
  POLYPHONY_UNISON_2X,
  POLYPHONY_CYCLIC,
  POLYPHONY_MODE_LAST
};

// The part settings used by this port (a subset of PartData).
struct PartSettings {
  uint8_t volume = 120;
  int8_t octave = 0;
  int8_t tuning = 0;
  uint8_t spread = 0;
  uint8_t legato = 0;
  uint8_t portamento_time = 0;
  uint8_t polyphony_mode = POLYPHONY_POLY;
  uint8_t pan = 64;  // 0 = left, 64 = centre, 127 = right
};

class Part {
 public:
  Part() { }

  // Starts with no voices; call AssignVoices() to give the part some.
  void Init(VoiceCard* voices);

  // Gives this part the voices whose bit is set in mask (bit 0 = voice 1),
  // like Part::AssignVoices on the controller. Sounding notes are stopped.
  void AssignVoices(uint8_t mask);
  uint8_t num_voices() const { return num_allocated_voices_; }

  // MIDI input.
  void NoteOn(uint8_t note, uint8_t velocity);
  void NoteOff(uint8_t note);
  void ControlChange(uint8_t controller, uint8_t value);
  void PitchBend(uint16_t pitch_bend);
  void Aftertouch(uint8_t note, uint8_t velocity);
  void Aftertouch(uint8_t velocity);
  void AllSoundOff();
  void AllNotesOff();

  // Engine-level voice allocation (cyclic / poly across parts): the engine
  // picks the voicecard (global index 0..5, owned by this part) and the part
  // plays it with its patch, tuning and spread. unison_index offsets the
  // pitch by unison_index * spread, as the hardware's unison does.
  void PlayVoice(uint8_t voice, uint8_t note, uint8_t velocity, uint8_t unison_index);
  void ReleaseVoice(uint8_t voice);
  void VoiceAftertouch(uint8_t voice, uint8_t value);
  void ResetAllControllers();

  // Editing. Patch addresses are byte offsets into struct Patch.
  void SetPatchValue(uint8_t address, uint8_t value);
  uint8_t GetPatchValue(uint8_t address) const;
  void SetPatch(const Patch& patch);
  const Patch& patch() const { return patch_; }

  void SetSettings(const PartSettings& settings);
  const PartSettings& settings() const { return settings_; }

  // Called once per control period (kControlRate samples at the native rate).
  // refresh_cycle alternates 0/1 so that LFO 2 and 3 update at half rate.
  void UpdateLfos(uint8_t refresh_cycle);

  // Called on every MIDI clock tick (24 per quarter note).
  void Clock();

 private:
  void TouchLfos();
  void RetriggerLfos();
  void InitializeAllocators();
  uint16_t TuneNote(uint8_t midi_note) const;
  void InternalNoteOn(uint8_t note, uint8_t velocity);
  void InternalNoteOff(uint8_t note);
  uint8_t GetNextVoice(uint8_t voice_index) const;
  void WritePatchToAllVoices(uint8_t address, uint8_t value);
  void WritePartDataToAllVoices();
  void WriteModulationToAllVoices(uint8_t source, uint8_t value);
  void SyncVoice(VoiceCard& voice);

  VoiceCard& voice(uint8_t i) { return voices_[allocated_voices_[i]]; }

  VoiceCard* voices_ = nullptr;
  Patch patch_;
  PartSettings settings_;

  uint8_t allocated_voices_[kNumVoices];
  uint8_t num_allocated_voices_ = 0;

  NoteStack<12> pressed_keys_;
  NoteStack<7> mono_allocator_;
  VoiceAllocator poly_allocator_;
  uint8_t poly_pool_[kNumVoices * 2];
  uint8_t poly_lru_[kNumVoices * 2];
  uint8_t polyphony_mode_ = POLYPHONY_POLY;
  uint8_t ignore_note_off_messages_ = 0;

  Lfo lfo_[kNumLfos];
  uint8_t lfo_step_[kNumLfos];
  uint8_t lfo_cycle_length_[kNumLfos];
  uint8_t lfo_previous_values_[kNumLfos];
  // Last controller values, so voices joining the part can be brought up to date.
  uint8_t controller_values_[kNumModulationSources];
  // Estimate of the MIDI clock tick duration in LFO refresh periods.
  uint16_t midi_clock_tick_duration_ = 0;

  DISALLOW_COPY_AND_ASSIGN(Part);
};

}  // namespace plugin
}  // namespace ambika

#endif  // AMBIKA_PLUGIN_PART_H_
