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

// How incoming notes reach the parts (plug-in extension; the hardware only
// has layer/split).
enum NoteDistribution {
  // Every part whose channel and key range accept the note plays it.
  DISTRIBUTION_LAYER,
  // Each note goes to the next accepting part(s) in turn: with one voice per
  // part and a different patch on each, notes cycle through the patches.
  DISTRIBUTION_ROUND_ROBIN,
  DISTRIBUTION_LAST
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

class Engine {
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

  // Note distribution; parts_per_note (1..6) applies to round robin.
  void SetNoteDistribution(uint8_t mode, uint8_t parts_per_note);
  uint8_t note_distribution() const { return distribution_; }

  // Tempo for clock-synced LFO rates (24 clock ticks per quarter note).
  void SetTempo(double bpm) { tempo_bpm_ = bpm > 1.0 ? bpm : 1.0; }

  // Renders stereo output at the host rate.
  void Render(float* left, float* right, int num_samples);

  int latency_samples() const { return latency_samples_; }

  // Activity of each voice for the UI: the VCA CV of the last block (0..255).
  uint8_t voice_level(int voice) const { return voices_[voice].vca(); }

 private:
  void RenderControlBlock();

  VoiceCard voices_[kNumVoices];
  AnalogVoice analog_[kNumVoices];
  Part parts_[kNumParts];
  PartMapping mappings_[kNumParts];
  uint8_t voice_parts_[kNumVoices];
  Resampler resampler_left_;
  Resampler resampler_right_;

  float voice_block_[kAudioBlockSize * kOversampling];
  float block_left_[kAudioBlockSize * kOversampling];
  float block_right_[kAudioBlockSize * kOversampling];
  float pan_[kNumVoices];  // Smoothed pan per voice, 0..1.
  float pan_coefficient_ = 0.0f;

  uint8_t distribution_ = DISTRIBUTION_LAYER;
  uint8_t parts_per_note_ = 1;
  uint8_t round_robin_next_ = 0;  // Part to try first for the next note.
  // Round robin: parts each held note was sent to, by channel and note.
  uint8_t note_parts_[16][128];
  uint8_t lfo_refresh_cycle_ = 0;
  double tempo_bpm_ = 120.0;
  double clock_accumulator_ = 0.0;
  int latency_samples_ = 0;

  DISALLOW_COPY_AND_ASSIGN(Engine);
};

}  // namespace plugin
}  // namespace ambika

#endif  // AMBIKA_PLUGIN_ENGINE_H_
