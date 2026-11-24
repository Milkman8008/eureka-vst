// Smoke tests for the plug-in engine (no JUCE needed).

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <string>
#include <vector>

#include "engine.h"
#include "parameters.h"
#include "preview.h"
#include "yam_oscillator.h"
#include "surge_filter.h"
#include "filter_models.h"
#include "ripples_filter.h"

using ambika::plugin::Engine;
using ambika::plugin::PartSettings;

namespace {

int g_failures = 0;

void Check(bool condition, const char* what) {
  std::printf("%s: %s\n", condition ? "PASS" : "FAIL", what);
  if (!condition) {
    ++g_failures;
  }
}

struct Stats {
  double rms = 0.0;
  double peak = 0.0;
  bool finite = true;
};

Stats Measure(const std::vector<float>& buffer) {
  Stats stats;
  double sum = 0.0;
  for (float x : buffer) {
    if (!std::isfinite(x)) {
      stats.finite = false;
      continue;
    }
    sum += static_cast<double>(x) * x;
    stats.peak = std::max(stats.peak, static_cast<double>(std::fabs(x)));
  }
  stats.rms = buffer.empty() ? 0.0 : std::sqrt(sum / buffer.size());
  return stats;
}

struct StereoBuffer {
  std::vector<float> left, right;
};

StereoBuffer RenderStereo(Engine& engine, double rate, double seconds) {
  StereoBuffer out;
  out.left.resize(static_cast<size_t>(rate * seconds));
  out.right.resize(out.left.size());
  // Render in host-sized chunks, as a DAW would.
  const int chunk = 256;
  for (size_t i = 0; i < out.left.size(); i += chunk) {
    const int n = static_cast<int>(std::min<size_t>(chunk, out.left.size() - i));
    engine.Render(out.left.data() + i, out.right.data() + i, n);
  }
  return out;
}

// Mid signal (L+R)/2: at centre pan this matches the earlier mono output.
std::vector<float> Render(Engine& engine, double rate, double seconds) {
  StereoBuffer stereo = RenderStereo(engine, rate, seconds);
  std::vector<float> mid(stereo.left.size());
  for (size_t i = 0; i < mid.size(); ++i) {
    mid[i] = 0.5f * (stereo.left[i] + stereo.right[i]);
  }
  return mid;
}

// Dominant period of the signal, by autocorrelation, in samples.
double EstimatePeriod(const std::vector<float>& x, int min_lag, int max_lag) {
  double best = -1e30;
  int best_lag = min_lag;
  for (int lag = min_lag; lag <= max_lag; ++lag) {
    double sum = 0.0;
    for (size_t i = 0; i + lag < x.size(); ++i) {
      sum += x[i] * x[i + lag];
    }
    if (sum > best) {
      best = sum;
      best_lag = lag;
    }
  }
  return best_lag;
}

void TestSilenceAndNote(double rate) {
  char label[128];
  Engine engine;
  engine.Init(rate);

  Stats silence = Measure(Render(engine, rate, 0.2));
  std::snprintf(label, sizeof(label), "%.0f Hz: silent before any note", rate);
  Check(silence.finite && silence.peak < 1e-4, label);

  engine.NoteOn(0, 57, 100);  // A3, 220 Hz.
  std::vector<float> held = Render(engine, rate, 0.5);
  Stats note = Measure(held);
  std::snprintf(label, sizeof(label),
                "%.0f Hz: note produces sound (rms %.3f, peak %.3f)",
                rate, note.rms, note.peak);
  Check(note.finite && note.rms > 0.01 && note.peak < 1.0, label);

  // Pitch: the default patch is saw + square one octave down, so the period
  // should match 220 Hz (or 110 Hz from the lower oscillator).
  std::vector<float> tail(held.end() - static_cast<long>(rate * 0.2), held.end());
  const double period = EstimatePeriod(tail, static_cast<int>(rate / 400),
                                       static_cast<int>(rate / 90));
  const double frequency = rate / period;
  std::snprintf(label, sizeof(label), "%.0f Hz: pitch near 220 or 110 Hz (%.1f Hz)",
                rate, frequency);
  Check(std::fabs(frequency - 220.0) < 6.0 || std::fabs(frequency - 110.0) < 3.0,
        label);

  engine.NoteOff(0, 57);
  Render(engine, rate, 1.5);  // Default release is short.
  Stats after = Measure(Render(engine, rate, 0.2));
  std::snprintf(label, sizeof(label), "%.0f Hz: silent after release (peak %.5f)",
                rate, after.peak);
  Check(after.finite && after.peak < 1e-3, label);
}

void TestNoteBeforeFirstBlock() {
  // A host can send a note in the very first processBlock.
  const double rate = 48000.0;
  Engine engine;
  engine.Init(rate);
  engine.NoteOn(0, 60, 100);
  Stats stats = Measure(Render(engine, rate, 0.1));
  Check(stats.finite && stats.rms > 0.01, "note sent before the first block sounds");
}

void TestPolyphony() {
  const double rate = 48000.0;
  Engine engine;
  engine.Init(rate);
  for (int i = 0; i < 6; ++i) {
    engine.NoteOn(0, 48 + i * 4, 100);
  }
  Render(engine, rate, 0.1);
  int active = 0;
  for (int i = 0; i < 6; ++i) {
    active += engine.voice_level(i) > 0;
  }
  Check(active == 6, "poly: six notes use six voices");

  // A seventh note steals a voice rather than adding one.
  engine.NoteOn(0, 80, 100);
  Render(engine, rate, 0.05);
  Stats stats = Measure(Render(engine, rate, 0.1));
  Check(stats.finite && stats.peak < 1.0, "poly: voice stealing stays in range");
  engine.AllSoundOff();
}

void TestMonoAndUnison() {
  const double rate = 44100.0;
  Engine engine;
  engine.Init(rate);

  PartSettings settings = engine.part(0).settings();
  settings.polyphony_mode = ambika::plugin::POLYPHONY_MONO;
  engine.part(0).SetSettings(settings);
  engine.NoteOn(0, 60, 100);
  engine.NoteOn(0, 64, 100);
  Render(engine, rate, 0.1);
  int active = 0;
  for (int i = 0; i < 6; ++i) {
    active += engine.voice_level(i) > 0;
  }
  // Mono mode stacks every allocated voice on the same note.
  Check(active == 6, "mono: all voices follow the single note");
  engine.NoteOff(0, 64);
  engine.NoteOff(0, 60);
  Render(engine, rate, 1.5);

  settings.polyphony_mode = ambika::plugin::POLYPHONY_UNISON_2X;
  engine.part(0).SetSettings(settings);
  engine.NoteOn(0, 60, 100);
  Render(engine, rate, 0.1);
  active = 0;
  for (int i = 0; i < 6; ++i) {
    active += engine.voice_level(i) > 0;
  }
  Check(active == 2, "unison 2x: one note uses two voices");
}

void TestIndependentInstances() {
  // Two engines must not share state (the firmware uses globals).
  const double rate = 48000.0;
  Engine a;
  Engine b;
  a.Init(rate);
  b.Init(rate);
  a.NoteOn(0, 60, 100);
  Render(a, rate, 0.2);
  Stats quiet = Measure(Render(b, rate, 0.2));
  Check(quiet.peak < 1e-4, "instances: a note on one engine is silent on another");
}

void TestPatchEdits() {
  // Sweep every patch byte through its range while a note plays: nothing may
  // crash or produce non-finite output.
  const double rate = 48000.0;
  Engine engine;
  engine.Init(rate);
  engine.NoteOn(0, 60, 127);
  bool finite = true;
  for (uint8_t address = 0; address < 104; ++address) {
    const uint8_t original = engine.part(0).GetPatchValue(address);
    for (int value = 0; value < 256; value += 51) {
      engine.part(0).SetPatchValue(address, static_cast<uint8_t>(value));
      Stats stats = Measure(Render(engine, rate, 0.003));
      finite = finite && stats.finite;
    }
    engine.part(0).SetPatchValue(address, original);
  }
  Check(finite, "patch: every byte/value combination renders finite audio");
}


int ActiveVoices(const Engine& engine, uint8_t mask) {
  int active = 0;
  for (int i = 0; i < 6; ++i) {
    if ((mask & (1 << i)) && engine.voice_level(i) > 0) ++active;
  }
  return active;
}

void TestMultiPart() {
  const double rate = 48000.0;
  Engine engine;
  engine.Init(rate);

  // Part 1 on channel 1 with voices 1-3, part 2 on channel 2 with voices 4-6.
  ambika::plugin::PartMapping mapping;
  mapping.midi_channel = 1;
  engine.SetPartMapping(0, mapping);
  mapping.midi_channel = 2;
  engine.SetPartMapping(1, mapping);
  const uint8_t split[6] = { 0, 0, 0, 1, 1, 1 };
  engine.SetVoiceAssignment(split);
  Check(engine.part(0).num_voices() == 3 && engine.part(1).num_voices() == 3,
        "multi: voices split 3/3 between parts 1 and 2");

  engine.NoteOn(1, 60, 100);  // MIDI channel 2.
  Render(engine, rate, 0.1);
  Check(ActiveVoices(engine, 0x07) == 0 && ActiveVoices(engine, 0x38) == 1,
        "multi: a channel 2 note plays on a part 2 voice only");
  engine.NoteOn(0, 64, 100);  // MIDI channel 1.
  Render(engine, rate, 0.1);
  Check(ActiveVoices(engine, 0x07) == 1, "multi: a channel 1 note plays on part 1");

  // Each part keeps its own patch: close part 2's filter only.
  engine.part(1).SetPatchValue(ambika::PRM_PATCH_FILTER1_CUTOFF, 0);
  Check(engine.part(0).GetPatchValue(ambika::PRM_PATCH_FILTER1_CUTOFF) != 0,
        "multi: editing part 2 leaves part 1's patch alone");
  engine.AllSoundOff();
  Render(engine, rate, 0.3);  // Let the output coupling settle.

  // Key split on one channel: part 1 below C4, part 2 from C4.
  mapping.midi_channel = 0;
  mapping.keyrange_low = 0;
  mapping.keyrange_high = 59;
  engine.SetPartMapping(0, mapping);
  mapping.keyrange_low = 60;
  mapping.keyrange_high = 127;
  engine.SetPartMapping(1, mapping);
  engine.NoteOn(5, 40, 100);
  engine.NoteOn(5, 72, 100);
  Render(engine, rate, 0.1);
  Check(ActiveVoices(engine, 0x07) == 1 && ActiveVoices(engine, 0x38) == 1,
        "multi: key split sends low and high notes to different parts");
  engine.AllSoundOff();
  Render(engine, rate, 0.3);  // Let the output coupling settle.

  // A part without voices is silent; moving a voice to it makes it play.
  mapping = ambika::plugin::PartMapping();
  mapping.midi_channel = 1;
  engine.SetPartMapping(0, mapping);
  mapping.midi_channel = 2;
  engine.SetPartMapping(1, mapping);
  mapping.midi_channel = 3;
  engine.SetPartMapping(2, mapping);
  engine.NoteOn(2, 60, 100);
  Stats silent = Measure(Render(engine, rate, 0.1));
  Check(silent.peak < 1e-4, "multi: a part with no voices is silent");
  engine.NoteOff(2, 60);
  const uint8_t moved[6] = { 0, 0, 0, 1, 1, 2 };
  engine.SetVoiceAssignment(moved);
  engine.NoteOn(2, 60, 100);
  Render(engine, rate, 0.1);
  Check(engine.voice_level(5) > 0 && ActiveVoices(engine, 0x1f) == 0,
        "multi: a voice moved to part 3 plays part 3's notes");
}


void TestSurgeFilters() {
  using ambika::plugin::SurgeFilter;
  const double rate = 48000.0;
  Engine engine;
  engine.Init(rate);
  auto& part = engine.part(0);
  engine.NoteOn(0, 48, 110);
  engine.NoteOn(0, 55, 110);

  // Every Surge type and variant, with cutoff and resonance swept.
  bool finite = true;
  double loudest = 0.0;
  int combinations = 0;
  for (int type = 1; type <= SurgeFilter::NumModels(); ++type) {
    const int variants = std::max(1, SurgeFilter::NumSubtypes(type));
    for (int variant = 0; variant < variants; ++variant) {
      part.SetPatchValue(ambika::plugin::kPatchFilterModel, static_cast<uint8_t>(type));
      part.SetPatchValue(ambika::plugin::kPatchFilterVariant, static_cast<uint8_t>(variant));
      for (int cutoff : { 10, 70, 127 }) {
        part.SetPatchValue(ambika::PRM_PATCH_FILTER1_CUTOFF, static_cast<uint8_t>(cutoff));
        part.SetPatchValue(ambika::PRM_PATCH_FILTER1_RESONANCE, cutoff == 70 ? 63 : 20);
        Stats stats = Measure(Render(engine, rate, 0.01));
        finite = finite && stats.finite;
        loudest = std::max(loudest, stats.peak);
      }
      ++combinations;
    }
  }
  char label[160];
  std::snprintf(label, sizeof(label),
                "surge: %d filter type/variant combinations render finite audio (peak %.2f)",
                combinations, loudest);
  Check(finite && loudest < 4.0, label);

  // A Surge low-pass actually filters: closing it lowers the level.
  part.SetPatchValue(ambika::plugin::kPatchFilterModel, 2);  // LP Diode Ladder
  part.SetPatchValue(ambika::plugin::kPatchFilterVariant, 3);  // 24 dB
  part.SetPatchValue(ambika::PRM_PATCH_FILTER1_RESONANCE, 0);
  part.SetPatchValue(ambika::PRM_PATCH_FILTER1_CUTOFF, 127);
  Render(engine, rate, 0.1);
  Stats open = Measure(Render(engine, rate, 0.2));
  part.SetPatchValue(ambika::PRM_PATCH_FILTER1_CUTOFF, 20);
  Render(engine, rate, 0.1);
  Stats closed = Measure(Render(engine, rate, 0.2));
  std::snprintf(label, sizeof(label), "surge: closing the LP Diode Ladder lowers the level (%.3f -> %.3f)",
                open.rms, closed.rms);
  Check(open.rms > 0.01 && closed.rms < open.rms * 0.5, label);

  // Back to the Ambika model works too.
  part.SetPatchValue(ambika::plugin::kPatchFilterModel, 0);
  part.SetPatchValue(ambika::PRM_PATCH_FILTER1_CUTOFF, 127);
  Render(engine, rate, 0.1);
  Stats back = Measure(Render(engine, rate, 0.1));
  Check(back.finite && back.rms > 0.01, "surge: switching back to the Ambika SVF still sounds");

  // Names for the editor.
  // The menu: exactly the requested models, in order.
  const char* expected[] = { "Surge MULTI Xpander", "Surge LP Diode Ladder", "Surge LP Legacy Ladder",
                             "Surge LP Vintage Ladder", "Surge FX Comb+", "Surge FX Comb-",
                             "Surge FX Sample and Hold", "Surge FX Cutoff Warp AP" };
  bool order = SurgeFilter::NumModels() == 8;
  for (int m = 1; order && m <= 8; ++m) {
    order = std::string(SurgeFilter::ModelName(m)) == expected[m - 1];
  }
  Check(order, "surge: the filter menu lists the eight chosen models in order");
  Check(SurgeFilter::SubtypeName(2, 3) == "24 dB" && SurgeFilter::SubtypeName(1, 0) == "LP1",
        "surge: variant names come from sst-filters");
  const auto& model = ambika::plugin::GetParameter(0);  // Just to keep the table linked.
  (void) model;
}


void TestPan() {
  const double rate = 48000.0;
  Engine engine;
  engine.Init(rate);
  auto settings = engine.part(0).settings();
  ambika::plugin::SetPartField(&settings, ambika::plugin::PART_PAN, -64);  // Hard left.
  engine.part(0).SetSettings(settings);
  engine.NoteOn(0, 60, 100);
  RenderStereo(engine, rate, 0.1);
  StereoBuffer left = RenderStereo(engine, rate, 0.2);
  Stats l = Measure(left.left), r = Measure(left.right);
  char label[160];
  std::snprintf(label, sizeof(label), "pan: hard left part is silent on the right (L %.3f, R %.5f)",
                l.rms, r.rms);
  Check(l.rms > 0.02 && r.rms < 1e-3, label);

  // Centre: both sides equal.
  ambika::plugin::SetPartField(&settings, ambika::plugin::PART_PAN, 0);
  engine.part(0).SetSettings(settings);
  RenderStereo(engine, rate, 0.1);
  StereoBuffer centre = RenderStereo(engine, rate, 0.2);
  l = Measure(centre.left);
  r = Measure(centre.right);
  Check(std::fabs(l.rms - r.rms) < 0.02 * l.rms, "pan: centred part is equal on both sides");
  engine.AllSoundOff();

  // Modulation: velocity -> pan. A soft note sits left of a loud one.
  auto& part = engine.part(0);
  part.SetPatchValue(ambika::PRM_PATCH_MOD_SOURCE + 0 * 3, ambika::MOD_SRC_VELOCITY);
  part.SetPatchValue(ambika::PRM_PATCH_MOD_DESTINATION + 0 * 3, ambika::plugin::kModDstPan);
  part.SetPatchValue(ambika::PRM_PATCH_MOD_AMOUNT + 0 * 3, 63);
  RenderStereo(engine, rate, 0.3);
  engine.NoteOn(0, 60, 127);
  RenderStereo(engine, rate, 0.1);
  StereoBuffer loud = RenderStereo(engine, rate, 0.2);
  const double loud_balance = Measure(loud.right).rms / std::max(1e-9, Measure(loud.left).rms);
  Check(loud_balance > 2.0, "pan: the mod matrix can route velocity to pan");
}

int ActiveMask(const Engine& engine) {
  int mask = 0;
  for (int i = 0; i < 6; ++i) {
    if (engine.voice_level(i) > 0) mask |= 1 << i;
  }
  return mask;
}

void Play(Engine& engine, uint8_t note) {
  engine.NoteOn(0, note, 100);  // Everything on MIDI channel 1.
  Render(engine, 48000.0, 0.02);
}

void TestVoiceModes() {
  using ambika::plugin::VOICE_MODE_CYCLIC;
  using ambika::plugin::VOICE_MODE_POLY;
  const double rate = 48000.0;
  Engine engine;
  engine.Init(rate);

  // Cards split across parts, notes all on channel 1: cyclic still steps
  // through every card, each with its own part's patch.
  const uint8_t three_parts[6] = { 0, 0, 1, 1, 2, 2 };
  engine.SetVoiceAssignment(three_parts);
  for (uint8_t p = 0; p < 3; ++p) {
    engine.part(p).SetPatchValue(ambika::PRM_PATCH_OSC1_SHAPE, 1 + p);
  }
  engine.SetVoiceMode(VOICE_MODE_CYCLIC, 1);
  bool cycles = true;
  for (int n = 0; n < 6; ++n) {
    Play(engine, 60 + n);
    cycles = cycles && ActiveMask(engine) == (1 << (n + 1)) - 1;
  }
  Check(cycles, "cyclic: notes on one channel step through cards 1-6 across three parts");
  Play(engine, 72);  // Seventh note: back to card 1.
  Check(engine.voice_level(0) > 0, "cyclic: the seventh note wraps around to card 1");
  engine.NoteOff(0, 62);  // Third note: card 3 only.
  Render(engine, rate, 1.5);
  Check(ActiveMask(engine) == 0x3b, "cyclic: note-off releases the card that played the note");
  engine.AllSoundOff();
  Render(engine, rate, 0.05);

  // Each card uses its owner's settings: part 2 at half volume.
  auto settings = engine.part(1).settings();
  settings.volume = 60;
  engine.part(1).SetSettings(settings);
  engine.SetVoiceMode(VOICE_MODE_POLY, 1);
  engine.SetVoiceMode(VOICE_MODE_CYCLIC, 1);
  for (int n = 0; n < 4; ++n) Play(engine, 60 + n);
  Render(engine, rate, 0.2);
  Check(engine.voice_level(2) < engine.voice_level(0) * 0.7 && engine.voice_level(3) == engine.voice_level(2),
        "cyclic: each card sounds with its own part's settings");
  engine.AllSoundOff();
  Render(engine, rate, 0.05);

  // Unison across parts: 2 then 3 cards per note.
  engine.SetVoiceMode(VOICE_MODE_POLY, 1);
  engine.SetVoiceMode(VOICE_MODE_CYCLIC, 2);
  Play(engine, 60);
  const int first = ActiveMask(engine);
  Play(engine, 64);
  Check(first == 0x03 && ActiveMask(engine) == 0x0f, "cyclic unison 2: each note plays two cards (1+2, 3+4)");
  engine.AllSoundOff();
  Render(engine, rate, 0.05);
  engine.SetVoiceMode(VOICE_MODE_POLY, 1);
  engine.SetVoiceMode(VOICE_MODE_CYCLIC, 3);
  Play(engine, 60);
  const int stack = ActiveMask(engine);
  Play(engine, 67);
  Check(stack == 0x07 && ActiveMask(engine) == 0x3f, "cyclic unison 3: notes play cards 1-3, then 4-6");
  engine.NoteOff(0, 60);
  engine.NoteOff(0, 67);
  Render(engine, rate, 1.5);
  Check(ActiveMask(engine) == 0, "cyclic unison 3: note-offs release all six cards");

  // Poly: free cards first, the oldest note stolen when all are busy.
  engine.SetVoiceMode(VOICE_MODE_POLY, 1);
  for (int n = 0; n < 6; ++n) Play(engine, 48 + n);
  Check(ActiveMask(engine) == 0x3f, "poly: six notes use six cards");
  engine.NoteOff(0, 50);  // Card 3 becomes free.
  Render(engine, rate, 1.5);
  Play(engine, 70);
  Check(ActiveMask(engine) == 0x3f, "poly: a new note takes the free card");
  Play(engine, 71);  // All busy: steals the oldest (card 1, note 48).
  engine.NoteOff(0, 48);  // Its note-off must not release the stolen card.
  Render(engine, rate, 0.3);
  Check(engine.voice_level(0) > 0, "poly: stealing hands the card to the new note");
  engine.AllSoundOff();
  Render(engine, rate, 0.05);

  // Sustain pedal holds engine-allocated voices.
  engine.SetVoiceMode(VOICE_MODE_CYCLIC, 1);
  engine.ControlChange(0, 64, 127);
  Play(engine, 60);
  engine.NoteOff(0, 60);
  Render(engine, rate, 1.0);
  const bool held = engine.voice_level(0) > 0;
  engine.ControlChange(0, 64, 0);
  Render(engine, rate, 1.5);
  Check(held && ActiveMask(engine) == 0, "cyclic: sustain holds notes until the pedal is released");
}

}  // namespace

void TestYamOscillators() {
  using namespace ambika::plugin;
  // Names: after the stock list, in order, and the stock list unchanged.
  int shape_index = -1;
  for (int i = 0; i < NumParameters(); ++i) {
    if (std::string(GetParameter(i).id) == "osc1_shape") shape_index = i;
  }
  const auto& shape = GetParameter(shape_index);
  const char* expected[] = { "yam_square", "yam_saw", "yam_triangle", "yam_qpwm",
                             "yam_fmfb", "yam_csaw", "yam_vowel2" };
  bool names = shape.max_value == kNumWaveforms - 1 &&
               std::string(shape.choices[ambika::WAVEFORM_LAST - 1]) == "wavquence" &&
               std::string(shape.choices[ambika::WAVEFORM_SAW]) == "saw";
  for (int i = 0; i < YAM_LAST; ++i) {
    names = names && std::string(shape.choices[kFirstYamWaveform + i]) == expected[i];
  }
  Check(names, "yam: the seven YAM waveforms follow the stock list, in order");

  const double rate = 48000.0;
  bool all_sound = true;
  for (int yam = 0; yam < YAM_LAST; ++yam) {
    for (int parameter : { 0, 64, 127 }) {
      Engine engine;
      engine.Init(rate);
      auto& part = engine.part(0);
      part.SetPatchValue(ambika::PRM_PATCH_OSC1_SHAPE, kFirstYamWaveform + yam);
      part.SetPatchValue(ambika::PRM_PATCH_OSC1_PWM, parameter);
      part.SetPatchValue(ambika::PRM_PATCH_MIX_BALANCE, 0);  // Osc 1 only.
      part.SetPatchValue(ambika::PRM_PATCH_FILTER1_CUTOFF, 127);
      engine.NoteOn(0, 57, 110);
      Stats stats = Measure(Render(engine, rate, 0.1));
      if (!(stats.finite && stats.rms > 0.01 && stats.peak < 1.5)) {
        std::printf("  %s, parameter %d: rms %.4f peak %.3f\n", expected[yam], parameter,
                    stats.rms, stats.peak);
        all_sound = false;
      }
    }
  }
  Check(all_sound, "yam: every YAM waveform sounds at parameter 0, 64 and 127");

  // Pitch: yam_saw on A3 (220 Hz).
  {
    Engine engine;
    engine.Init(rate);
    auto& part = engine.part(0);
    part.SetPatchValue(ambika::PRM_PATCH_OSC1_SHAPE, kFirstYamWaveform + YAM_SAW);
    part.SetPatchValue(ambika::PRM_PATCH_OSC1_PWM, 0);
    part.SetPatchValue(ambika::PRM_PATCH_MIX_BALANCE, 0);
    part.SetPatchValue(ambika::PRM_PATCH_FILTER1_CUTOFF, 127);
    engine.NoteOn(0, 57, 110);
    std::vector<float> audio = Render(engine, rate, 0.3);
    std::vector<float> tail(audio.end() - 8192, audio.end());
    const double frequency = rate / EstimatePeriod(tail, 100, 500);
    char label[120];
    std::snprintf(label, sizeof(label), "yam: yam_saw plays in tune (%.1f Hz for A3)", frequency);
    Check(std::fabs(frequency - 220.0) < 6.0, label);
  }

  // yam_fmfb: feedback (parameter above 64) changes the sound.
  {
    double rms[2];
    for (int k = 0; k < 2; ++k) {
      Engine engine;
      engine.Init(rate);
      auto& part = engine.part(0);
      part.SetPatchValue(ambika::PRM_PATCH_OSC1_SHAPE, kWaveformYamFmFb);
      part.SetPatchValue(ambika::PRM_PATCH_OSC1_PWM, k == 0 ? 60 : 127);
      part.SetPatchValue(ambika::PRM_PATCH_MIX_BALANCE, 0);
      engine.NoteOn(0, 57, 110);
      rms[k] = Measure(Render(engine, rate, 0.1)).rms;
    }
    Check(std::fabs(rms[0] - rms[1]) > 1e-4, "yam: yam_fmfb feedback changes the sound");
  }

  // Previews (the oscillator display) draw a waveform for every YAM shape.
  bool previews = true;
  for (int yam = 0; yam < YAM_LAST; ++yam) {
    uint8_t wave[160];
    RenderOscillatorPreview(kFirstYamWaveform + yam, 32, 0, wave, 160);
    const auto [lo, hi] = std::minmax_element(wave, wave + 160);
    previews = previews && (*hi - *lo) > 32;
  }
  Check(previews, "yam: the oscillator display draws every YAM waveform");
}

// Notes started on the voicecards while rendering: a card counts as
// starting a note when its gate opens, or when it moves to a new pitch with
// its gate open. Rendered in 2 ms chunks.
struct NoteEvent {
  double time;
  int voice;
  int note;
};

std::vector<NoteEvent> RenderNotes(Engine& engine, double rate, double seconds,
                                   std::vector<float>* audio = nullptr) {
  std::vector<NoteEvent> events;
  int last_gate[ambika::plugin::kNumVoices] = { 0 };
  int last_pitch[ambika::plugin::kNumVoices] = { 0 };
  for (int v = 0; v < ambika::plugin::kNumVoices; ++v) {
    last_gate[v] = engine.voice(v).gate();
    last_pitch[v] = engine.voice(v).pitch_target();
  }
  const int chunk = static_cast<int>(rate * 0.002);
  std::vector<float> left(chunk), right(chunk);
  for (double t = 0.0; t < seconds; t += 0.002) {
    engine.Render(left.data(), right.data(), chunk);
    if (audio != nullptr) audio->insert(audio->end(), left.begin(), left.end());
    for (int v = 0; v < ambika::plugin::kNumVoices; ++v) {
      const int gate = engine.voice(v).gate();
      const int pitch = engine.voice(v).pitch_target();
      if (gate && (!last_gate[v] || pitch != last_pitch[v])) {
        events.push_back({ t, v, pitch / 128 });
      }
      last_gate[v] = gate;
      last_pitch[v] = pitch;
    }
  }
  return events;
}

bool AnyGateOpen(const Engine& engine) {
  for (int v = 0; v < ambika::plugin::kNumVoices; ++v) {
    if (engine.voice(v).gate()) return true;
  }
  return false;
}

void TestArpeggiatorAndSequencer() {
  using namespace ambika::plugin;
  const double rate = 48000.0;
  const uint8_t all_part_1[kNumVoices] = { 0, 0, 0, 0, 0, 0 };

  // The arpeggiator in both kinds of voice allocation: the part's own
  // (per part) and the engine's (poly, the plug-in default).
  for (int mode : { VOICE_MODE_PER_PART, VOICE_MODE_POLY }) {
    Engine engine;
    engine.Init(rate);
    engine.SetVoiceAssignment(all_part_1);
    engine.SetVoiceMode(static_cast<uint8_t>(mode), 1);
    engine.SetTempo(120.0);
    PartSettings settings = engine.part(0).settings();
    settings.arp_mode = ARP_SEQUENCER_MODE_ARPEGGIATOR;
    settings.arp_direction = ARPEGGIO_DIRECTION_UP;
    settings.arp_octave = 2;
    settings.arp_pattern = 0;   // x.x. x.x. ...: every other step
    settings.arp_divider = 10;  // 1/16
    engine.part(0).SetSettings(settings);
    // Keys pressed in an order other than the arpeggio's.
    engine.NoteOn(0, 67, 100);
    engine.NoteOn(0, 60, 100);
    engine.NoteOn(0, 64, 100);
    const auto events = RenderNotes(engine, rate, 1.95);
    const int expected[] = { 60, 64, 67, 72, 76, 79, 60, 64 };
    bool order = events.size() == 8;
    for (size_t i = 0; order && i < 8; ++i) order = events[i].note == expected[i];
    const char* name = mode == VOICE_MODE_POLY ? "poly" : "per part";
    std::printf("      arp (%s): %zu notes:", name, events.size());
    for (const auto& e : events) std::printf(" %d@%.3f", e.note, e.time);
    std::printf("\n");
    Check(order, mode == VOICE_MODE_POLY ? "arp (poly mode): up over 2 octaves, in pitch order"
                                         : "arp (per part): up over 2 octaves, in pitch order");
    // One note per eighth (1/16 steps, every other one) at 120 bpm.
    bool spacing = events.size() >= 2;
    for (size_t i = 1; spacing && i < events.size(); ++i) {
      spacing = std::fabs(events[i].time - events[i - 1].time - 0.25) < 0.004;
    }
    Check(spacing, "arp: one note every 1/8 at 120 bpm");

    // Releasing the keys stops it.
    for (int note : { 60, 64, 67 }) engine.NoteOff(0, note);
    const auto after = RenderNotes(engine, rate, 1.0);
    Check(after.empty() && !AnyGateOpen(engine), "arp: stops when the keys are released");
  }

  // Note pattern, transposed by the key (60 in the pattern = the key).
  {
    Engine engine;
    engine.Init(rate);
    engine.SetVoiceAssignment(all_part_1);
    engine.SetVoiceMode(VOICE_MODE_POLY, 1);
    engine.SetTempo(120.0);
    PartSettings settings = engine.part(0).settings();
    settings.arp_mode = ARP_SEQUENCER_MODE_NOTE;
    settings.sequence_length[2] = 4;
    const uint8_t notes[4] = { 60, 67, 72, 48 };
    const uint8_t gates[4] = { 1, 1, 0, 1 };
    for (int i = 0; i < 4; ++i) {
      settings.pattern_note[i] = notes[i];
      settings.pattern_gate[i] = gates[i];
      settings.pattern_legato[i] = 0;
    }
    engine.part(0).SetSettings(settings);
    engine.NoteOn(0, 62, 100);  // D: the pattern plays a tone up.
    const auto events = RenderNotes(engine, rate, 0.99);
    // Steps every 1/16 (125 ms); step 3 has no gate. 8 steps = two loops.
    const int expected[] = { 62, 69, 50, 62, 69, 50 };
    bool ok = events.size() == 6;
    for (size_t i = 0; ok && i < 6; ++i) ok = events[i].note == expected[i];
    Check(ok, "pattern: steps transposed by the key, rests skipped, loops at its length");
    engine.NoteOff(0, 62);
    RenderNotes(engine, rate, 0.3);
    Check(!AnyGateOpen(engine), "pattern: stops with the key");
  }

  // Seq 1 / Seq 2 drive their modulation sources, and follow the host's
  // song position.
  {
    Engine engine;
    engine.Init(rate);
    engine.SetVoiceAssignment(all_part_1);
    engine.SetTempo(120.0);
    PartSettings settings = engine.part(0).settings();
    for (int i = 0; i < kNumSteps; ++i) settings.step_value[0][i] = static_cast<uint8_t>(i * 16);
    engine.part(0).SetSettings(settings);
    // Transport starts at beat 2: 24 ticks = 4 sixteenths in.
    engine.SetTransport(true, 1.0);
    std::vector<float> l(96), r(96);
    engine.Render(l.data(), r.data(), 96);
    Check(engine.voice(0).modulation_source(ambika::MOD_SRC_SEQ_1) == 4 * 16,
          "seq 1: starts at the step under the song position");
    // 1/16 (125 ms) later, the next step.
    for (int i = 0; i < 6240 / 96; ++i) engine.Render(l.data(), r.data(), 96);
    Check(engine.voice(0).modulation_source(ambika::MOD_SRC_SEQ_1) == 5 * 16,
          "seq 1: advances one step per 1/16");
    // A loop back to the start.
    engine.SetTransport(true, 0.0);
    engine.Render(l.data(), r.data(), 96);
    Check(engine.voice(0).modulation_source(ambika::MOD_SRC_SEQ_1) == 0,
          "seq 1: jumps with the song position (loop)");
  }

  // Switching the mode while keys are held leaves no stuck notes.
  {
    Engine engine;
    engine.Init(rate);
    engine.SetVoiceAssignment(all_part_1);
    engine.SetVoiceMode(VOICE_MODE_POLY, 1);
    engine.SetTempo(120.0);
    PartSettings settings = engine.part(0).settings();
    settings.arp_mode = ARP_SEQUENCER_MODE_ARPEGGIATOR;
    settings.arp_direction = ARPEGGIO_DIRECTION_CHORD;
    engine.part(0).SetSettings(settings);
    engine.NoteOn(0, 60, 100);
    engine.NoteOn(0, 64, 100);
    RenderNotes(engine, rate, 0.3);
    settings.arp_mode = ARP_SEQUENCER_MODE_STEP;
    engine.part(0).SetSettings(settings);
    engine.NoteOff(0, 60);
    engine.NoteOff(0, 64);
    RenderNotes(engine, rate, 0.2);
    Check(!AnyGateOpen(engine), "arp: no stuck notes after switching it off with keys held");
  }
}

void TestRipplesFilter() {
  using namespace ambika::plugin;
  const double rate = 48000.0;
  const int ripples = RipplesFilterModel();
  Check(std::string(FilterModelName(ripples)) == "Ripples" && NumFilterVariants(ripples) == 3 &&
            FilterVariantName(ripples, 1) == "LP2",
        "ripples: listed after the Surge filters, with LP4 / LP2 / BP2");
  Check(GetParameter(0).id != nullptr, "parameters build");

  // Cutoff sweep: a closed filter is darker (quieter) than an open one, and
  // nothing blows up at full resonance.
  double rms[2];
  for (int k = 0; k < 2; ++k) {
    Engine engine;
    engine.Init(rate);
    auto& part = engine.part(0);
    part.SetPatchValue(kPatchFilterModel, static_cast<uint8_t>(ripples));
    part.SetPatchValue(ambika::PRM_PATCH_FILTER1_CUTOFF, k == 0 ? 127 : 30);
    engine.NoteOn(0, 48, 100);
    const auto stats = Measure(Render(engine, rate, 0.3));
    rms[k] = stats.rms;
    Check(stats.finite && stats.peak < 2.0, "ripples: finite, bounded output");
  }
  Check(rms[0] > 0.01 && rms[1] < rms[0] * 0.5, "ripples: closing the cutoff darkens the sound");

  {
    Engine engine;
    engine.Init(rate);
    auto& part = engine.part(0);
    part.SetPatchValue(kPatchFilterModel, static_cast<uint8_t>(ripples));
    part.SetPatchValue(ambika::PRM_PATCH_FILTER1_RESONANCE, 63);
    part.SetPatchValue(ambika::PRM_PATCH_FILTER1_CUTOFF, 70);
    engine.NoteOn(0, 36, 127);
    const auto stats = Measure(Render(engine, rate, 0.5));
    Check(stats.finite && stats.peak < 2.0, "ripples: full resonance stays bounded");
  }

  // The display's small-signal response matches the circuit model: measure
  // the simulated LP4 at a few frequencies below and around the cutoff.
  {
    RipplesFilter filter;
    const double fs = Engine::kNativeSampleRate * Engine::kOversampling;
    filter.Init(fs);
    filter.set_noise(false);
    const double cutoff = 1000.0;
    bool close = true;
    for (double hz : { 200.0, 1000.0, 3000.0 }) {
      filter.Reset();
      double in2 = 0.0, out2 = 0.0;
      const int n = static_cast<int>(fs * 0.4);
      for (int i = 0; i < n; ++i) {
        const float x = 0.05f * static_cast<float>(std::sin(2.0 * M_PI * hz * i / fs));
        const float y = filter.Process(x, cutoff, 0.5f, 0);
        if (i > n / 2) { in2 += x * x; out2 += y * y; }
      }
      const double measured = std::sqrt(out2 / in2);
      const double predicted = RipplesFilter::Magnitude(hz, cutoff, 0.5f, 0);
      std::printf("      ripples LP4 at %.0f Hz: simulated %.3f, small-signal %.3f\n", hz, measured, predicted);
      close = close && std::fabs(20.0 * std::log10(measured / predicted)) < 1.0;
    }
    Check(close, "ripples: display response within 1 dB of the simulation");
  }
}

int main() {
  TestArpeggiatorAndSequencer();
  TestRipplesFilter();
  TestSilenceAndNote(44100.0);
  TestSilenceAndNote(48000.0);
  TestSilenceAndNote(96000.0);
  TestNoteBeforeFirstBlock();
  TestPolyphony();
  TestMonoAndUnison();
  TestIndependentInstances();
  TestPatchEdits();
  TestMultiPart();
  TestSurgeFilters();
  TestPan();
  TestVoiceModes();
  TestYamOscillators();
  std::printf("%s (%d failure%s)\n", g_failures ? "FAILED" : "OK",
              g_failures, g_failures == 1 ? "" : "s");
  return g_failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
