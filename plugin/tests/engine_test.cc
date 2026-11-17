// Smoke tests for the plug-in engine (no JUCE needed).

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <string>
#include <vector>

#include "engine.h"
#include "parameters.h"
#include "surge_filter.h"

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

void TestRoundRobin() {
  const double rate = 48000.0;
  Engine engine;
  engine.Init(rate);
  // One voice per part, every part on all channels, a different patch each.
  const uint8_t one_each[6] = { 0, 1, 2, 3, 4, 5 };
  engine.SetVoiceAssignment(one_each);
  for (uint8_t p = 0; p < 6; ++p) {
    engine.part(p).SetPatchValue(ambika::PRM_PATCH_OSC1_SHAPE, 1 + p);
  }
  engine.SetNoteDistribution(ambika::plugin::DISTRIBUTION_ROUND_ROBIN, 1);

  bool cycles = true;
  for (int n = 0; n < 6; ++n) {
    engine.NoteOn(0, 60 + n, 100);
    Render(engine, rate, 0.02);
    cycles = cycles && ActiveMask(engine) == (1 << (n + 1)) - 1;
  }
  Check(cycles, "round robin: six notes land on voices 1, 2, 3, 4, 5, 6 in turn");
  engine.NoteOff(0, 62);  // The third note: voice 3 only.
  Render(engine, rate, 1.5);
  Check(ActiveMask(engine) == 0x3b, "round robin: note-off releases the part that played the note");
  engine.AllSoundOff();
  Render(engine, rate, 0.05);

  // Two parts per note: 1+2, then 3+4, then 5+6.
  engine.SetNoteDistribution(ambika::plugin::DISTRIBUTION_LAYER, 1);
  engine.SetNoteDistribution(ambika::plugin::DISTRIBUTION_ROUND_ROBIN, 2);
  engine.NoteOn(0, 60, 100);
  Render(engine, rate, 0.02);
  const int first = ActiveMask(engine);
  engine.NoteOn(0, 64, 100);
  Render(engine, rate, 0.02);
  const int second = ActiveMask(engine);
  Check(first == 0x03 && second == 0x0f, "round robin x2: each note triggers two voices");
  engine.AllSoundOff();
  Render(engine, rate, 0.05);

  // Three parts per note.
  engine.SetNoteDistribution(ambika::plugin::DISTRIBUTION_LAYER, 1);
  engine.SetNoteDistribution(ambika::plugin::DISTRIBUTION_ROUND_ROBIN, 3);
  engine.NoteOn(0, 60, 100);
  Render(engine, rate, 0.02);
  Check(ActiveMask(engine) == 0x07, "round robin x3: a note triggers three voices");
  engine.NoteOn(0, 67, 100);
  Render(engine, rate, 0.02);
  Check(ActiveMask(engine) == 0x3f, "round robin x3: the next note takes the other three");
  engine.NoteOff(0, 60);
  engine.NoteOff(0, 67);
  Render(engine, rate, 1.5);
  Check(ActiveMask(engine) == 0, "round robin x3: note-offs release all six voices");
}

}  // namespace

int main() {
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
  TestRoundRobin();
  std::printf("%s (%d failure%s)\n", g_failures ? "FAILED" : "OK",
              g_failures, g_failures == 1 ? "" : "s");
  return g_failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
