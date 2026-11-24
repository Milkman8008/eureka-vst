// The complete synth: a multi of six parts sharing six voicecards, each
// voicecard followed by an analog stage, mixed and resampled to the host rate.
//
// Timing follows the hardware: the voicecards run at 20 MHz / 510
// (~39.2 kHz) and compute one 40-sample block per control period (~1 ms),
// which is also when the controller refreshes the part LFOs.
//
// MIDI routing and voice assignment follow controller/multi.{h,cc}: each part
// listens on one MIDI channel (or all) within a key range, and owns a set of
// voices. A voice belongs to at most one part.
//
// Engine has no JUCE dependency so it can be tested on its own.

#ifndef AMBIKA_PLUGIN_ENGINE_H_
#define AMBIKA_PLUGIN_ENGINE_H_

#include <stdint.h>

#include "analog.h"
#include "part.h"
#include "resampler.h"
#include "voice_card.h"

namespace ambika {
namespace plugin {

static const uint8_t kNumParts = 6;
static const uint8_t kNoPart = 0xff;

// How notes are given to the six voicecards (plug-in extension).
enum VoiceMode {
  // As on the hardware: every part plays every note on its own voices, with
  // its own Voicing (poly, mono, 2x unison, cyclic). Parts layer.
  VOICE_MODE_PER_PART,
  // Notes step through voicecards 1..6 in order, whatever part each belongs
  // to; each card sounds its own part's patch.
  VOICE_MODE_CYCLIC,
  // Each note takes the free card that has been idle longest, stealing the
  // oldest note when all are busy.
  VOICE_MODE_POLY,
  VOICE_MODE_LAST
};

// PartMapping in controller/multi.h (voice allocation is kept separately).
struct PartMapping {
  uint8_t midi_channel = 0;  // 0 = all channels, 1..16
  uint8_t keyrange_low = 0;
  uint8_t keyrange_high = 127;

  bool receive_channel(uint8_t channel) const {  // channel: 0..15
    return !midi_channel || channel + 1 == midi_channel;
  }
  bool accept_note(uint8_t note) const {
    if (keyrange_low <= keyrange_high) {
      return note >= keyrange_low && note <= keyrange_high;
    }
    return note <= keyrange_high || note >= keyrange_low;
  }
};

class Engine : private PartNoteSink {
 public:
  static constexpr double kNativeSampleRate = 20000000.0 / 510.0;
  static constexpr int kOversampling = 2;

  Engine() { }

  // Resets everything: part 1 owns all six voices on all MIDI channels.
  void Init(double host_sample_rate);

  // MIDI input. channel is 0..15.
  void NoteOn(uint8_t channel, uint8_t note, uint8_t velocity);
  void NoteOff(uint8_t channel, uint8_t note);
  void ControlChange(uint8_t channel, uint8_t controller, uint8_t value);
  void PitchBend(uint8_t channel, uint16_t value);
  void ChannelPressure(uint8_t channel, uint8_t value);
  void PolyPressure(uint8_t channel, uint8_t note, uint8_t value);
  void AllNotesOff();
  void AllSoundOff();

  // Multi configuration.
  void SetPartMapping(uint8_t part, const PartMapping& mapping);
  const PartMapping& part_mapping(uint8_t part) const { return mappings_[part]; }
  // voice_parts[v] = part owning voice v, or kNoPart.
  void SetVoiceAssignment(const uint8_t* voice_parts);
  uint8_t voice_part(uint8_t voice) const { return voice_parts_[voice]; }

  // Editing a part.
  Part& part(uint8_t index) { return parts_[index]; }
  const Part& part(uint8_t index) const { return parts_[index]; }

  // Voice allocation; unison (1..6) = voicecards per note in cyclic/poly.
  void SetVoiceMode(uint8_t mode, uint8_t unison);
  uint8_t voice_mode() const { return voice_mode_; }

  // Tempo for clock-synced LFO rates, the arpeggiator and the sequencers
  // (24 clock ticks per quarter note).
  void SetTempo(double bpm) { tempo_bpm_ = bpm > 1.0 ? bpm : 1.0; }

  // Host transport, once per audio block: whether it plays, and the song
  // position in quarter notes at the start of the block. While it plays the
  // sequencers follow the song position (they restart in place after a jump
  // or loop); while it is stopped the clock runs free at the tempo.
  void SetTransport(bool playing, double ppq_position);

  // Voices whose bit is set (bit 0 = voice 1) leave the main mix and go to
  // their own stereo output instead (plug-in extension).
  void SetDirectOutputs(uint8_t mask);
  uint8_t direct_outputs() const { return direct_mask_; }

  // Renders stereo output at the host rate. voice_outputs, if not null,
  // holds 2 * kNumVoices channel pointers (voice 1 left, voice 1 right,
  // voice 2 left...); a direct-output voice is written to its pair, which
  // must then be non-null. Pairs of voices in the main mix are not touched.
  void Render(float* left, float* right, int num_samples,
              float* const* voice_outputs = nullptr);

  int latency_samples() const { return latency_samples_; }

  // Activity of each voice for the UI: the VCA CV of the last block (0..255).
  uint8_t voice_level(int voice) const { return voices_[voice].vca(); }
  const VoiceCard& voice(int index) const { return voices_[index]; }

 private:
  void RenderControlBlock();

  // Engine-level allocation (cyclic / poly). slot is the MIDI channel
  // (0..15) for keys, or kPartSlot + part for notes a part's arpeggiator or
  // note pattern generates; allowed = the voicecards that may play it.
  static const uint8_t kPartSlot = 16;
  void AllocatedNoteOn(uint8_t slot, uint8_t note, uint8_t velocity, uint8_t allowed);
  void ReleaseSlotNote(uint8_t slot, uint8_t note);
  // Cards that play keys directly for a MIDI note (their part accepts it and
  // is not arpeggiating).
  uint8_t DirectCards(uint8_t channel, uint8_t note) const;

  // PartNoteSink: notes generated by the parts in cyclic / poly mode.
  void PartNoteOn(uint8_t part, uint8_t note, uint8_t velocity) override;
  void PartNoteOff(uint8_t part, uint8_t note) override;
  void PartAllNotesOff(uint8_t part) override;
  void ReleaseAllocatedVoice(uint8_t voice);
  void ResetAllocation();

  VoiceCard voices_[kNumVoices];
  AnalogVoice analog_[kNumVoices];
  Part parts_[kNumParts];
  PartMapping mappings_[kNumParts];
  uint8_t voice_parts_[kNumVoices];
  Resampler resampler_left_;
  Resampler resampler_right_;
  Resampler voice_resamplers_[kNumVoices][2];
  float voice_left_[kNumVoices][kAudioBlockSize * kOversampling];
  float voice_right_[kNumVoices][kAudioBlockSize * kOversampling];
  uint8_t direct_mask_ = 0;

  float voice_block_[kAudioBlockSize * kOversampling];
  float block_left_[kAudioBlockSize * kOversampling];
  float block_right_[kAudioBlockSize * kOversampling];
  float pan_[kNumVoices];  // Smoothed pan per voice, 0..1.
  float pan_coefficient_ = 0.0f;

  uint8_t voice_mode_ = VOICE_MODE_PER_PART;
  uint8_t unison_ = 1;
  uint8_t cyclic_next_ = 0;           // Voicecard to try first.
  uint8_t voice_note_[kNumVoices];    // Note held by each card, 0xff = free.
  uint8_t voice_channel_[kNumVoices];
  uint32_t voice_age_[kNumVoices];    // When each card was last (re)assigned.
  uint32_t age_counter_ = 0;
  bool sustain_ = false;
  uint8_t sustained_ = 0;             // Cards whose note-off waits for the pedal.
  uint8_t note_voices_[16 + kNumParts][128];  // Cards each held note plays, by slot.
  uint8_t lfo_refresh_cycle_ = 0;
  double tempo_bpm_ = 120.0;
  double song_ticks_ = 0.0;   // Clock position in ticks (24 per quarter note).
  int64_t next_tick_ = 0;     // Next tick to send to the parts.
  bool transport_playing_ = false;
  int latency_samples_ = 0;

  DISALLOW_COPY_AND_ASSIGN(Engine);
};

}  // namespace plugin
}  // namespace ambika

#endif  // AMBIKA_PLUGIN_ENGINE_H_
