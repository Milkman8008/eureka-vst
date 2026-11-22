#include "parameters.h"

#include "yam_oscillator.h"

#include <deque>
#include <string>
#include <vector>

#include "voicecard/resources.h"

#include "surge_filter.h"

namespace ambika {
namespace plugin {

namespace {

// Default patch of the controller (init_patch in controller/part.cc).
const prog_Patch init_patch PROGMEM = {
  // Oscillators
  WAVEFORM_SAW, 0, 0, 0,
  WAVEFORM_SQUARE, 32, -12, 12,

  // Mixer
  32, OP_SUM, 31, WAVEFORM_SUB_OSC_SQUARE_1, 0, 0, 0, 0,

  // Filter
  96, 0, 0, 0, 0, 0, 24, 0,
  // ADSR
  0, 40, 20,  60, LFO_WAVEFORM_TRIANGLE, kNumSyncedLfoRates + 24, 0, 0,
  0, 40, 0,   40, LFO_WAVEFORM_TRIANGLE, kNumSyncedLfoRates + 32, 0, 0,
  0, 40, 100, 40, LFO_WAVEFORM_TRIANGLE, kNumSyncedLfoRates + 48, 0, 0,

  LFO_WAVEFORM_TRIANGLE, 72,

  // Routing
  MOD_SRC_ENV_1, MOD_DST_PARAMETER_1, 0,
  MOD_SRC_ENV_1, MOD_DST_PARAMETER_2, 0,
  MOD_SRC_LFO_1, MOD_DST_OSC_1, 0,
  MOD_SRC_LFO_1, MOD_DST_OSC_2, 0,
  MOD_SRC_LFO_2, MOD_DST_PARAMETER_1, 0,
  MOD_SRC_LFO_2, MOD_DST_PARAMETER_2, 0,
  MOD_SRC_LFO_3, MOD_DST_MIX_BALANCE, 0,
  MOD_SRC_LFO_4, MOD_DST_FILTER_CUTOFF, 0,
  MOD_SRC_SEQ_1, MOD_DST_FILTER_CUTOFF, 0,
  MOD_SRC_SEQ_2, MOD_DST_MIX_BALANCE, 0,

  MOD_SRC_ENV_3, MOD_DST_VCA, 63,
  MOD_SRC_VELOCITY, MOD_DST_VCA, 16,
  MOD_SRC_PITCH_BEND, MOD_DST_OSC_1_2_COARSE, 32,
  MOD_SRC_LFO_4, MOD_DST_OSC_1_2_COARSE, 16,

  // Modifiers
  MOD_SRC_LFO_1, MOD_SRC_LFO_2, 0,
  MOD_SRC_LFO_2, MOD_SRC_LFO_3, 0,
  MOD_SRC_LFO_3, MOD_SRC_SEQ_1, 0,
  MOD_SRC_SEQ_1, MOD_SRC_SEQ_2, 0,

  // Padding
  0, 0, 0, 0, 0, 0, 0, 0,
};

// Value names, as shown by the controller (controller/resources.cc).
const char* const kOscillatorWaveforms[] = {
  "none", "saw", "square", "triangle", "sine",
  "zsaw", "lpzsaw", "pkzsaw", "bpzsaw", "hpzsaw",
  "lpzpulse", "pkzpulse", "bpzpulse", "hpzpulse", "ztriangle",
  "pad", "fm", "8bits", "pwm", "noise", "vowel",
  "male", "female", "choir", "tampura", "bowed", "cello", "vibes", "slap",
  "epiano", "organ", "waves", "digital", "drone 1", "drone 2", "metallic",
  "bell", "wavquence",
  // YAM oscillators (yam_oscillator.h), after the stock list.
  "yam_square", "yam_saw", "yam_triangle", "yam_qpwm", "yam_fmfb",
  "yam_csaw", "yam_vowel2",
};
static_assert(sizeof(kOscillatorWaveforms) / sizeof(kOscillatorWaveforms[0]) ==
              kNumWaveforms, "oscillator waveform names");

const char* const kMixOperators[] = {
  "sum", "sync", "ringmod", "xor", "fold", "bits",
};
static_assert(sizeof(kMixOperators) / sizeof(kMixOperators[0]) == OP_LAST,
              "mix operator names");

const char* const kSubOscillatorWaveforms[] = {
  "squ1", "tri1", "pul1", "squ2", "tri2", "pul2",
  "click", "glitch", "blow", "metal", "pop",
};
static_assert(sizeof(kSubOscillatorWaveforms) /
              sizeof(kSubOscillatorWaveforms[0]) == WAVEFORM_SUB_OSC_LAST,
              "sub oscillator names");

const char* const kFilterModes[] = { "lp", "bp", "hp" };

const char* const kLfoShapes[] = {
  "tri", "sqr", "s&h", "ramp",
  "hrm2", "hrm3", "hrm5", "grg1", "grg2", "bat1", "bat2", "spk1", "spk2",
  "lsaw", "lsqr", "rsaw", "rsqr", "stp1", "stp2", "wave 16",
};
static_assert(sizeof(kLfoShapes) / sizeof(kLfoShapes[0]) == LFO_WAVEFORM_LAST,
              "LFO shape names");

const char* const kLfoSyncModes[] = { "free", "env~lfo", "lfo~env" };

const char* const kModulationSources[] = {
  "env 1", "env 2", "env 3", "lfo 1", "lfo 2", "lfo 3", "lfo 4",
  "mod. 1", "mod. 2", "mod. 3", "mod. 4", "seq. 1", "seq. 2", "arp",
  "velocity", "afttch", "bender", "mwheel", "wheel2", "pedal",
  "note", "gate", "noise", "random",
  "= 256", "= 128", "= 64", "= 32", "= 16", "= 8", "= 4",
};
static_assert(sizeof(kModulationSources) / sizeof(kModulationSources[0]) ==
              MOD_SRC_LAST, "modulation source names");

const char* const kModulationDestinations[] = {
  "param 1", "param 2", "osc 1", "osc 2", "osc 1+2", "vibrato",
  "mix", "crossmod", "noise", "subosc", "fuzz", "crush",
  "frequency", "resonance", "attack", "decay", "release", "lfo 4", "vca",
  "pan",  // Plug-in extension (kModDstPan).
};
static_assert(sizeof(kModulationDestinations) /
              sizeof(kModulationDestinations[0]) == kNumPluginModDestinations,
              "modulation destination names");

const char* const kModifiers[] = {
  "none", "add", "prod", "attn", "max", "min", "xor", ">=", "<=", "qtz", "lag",
};
static_assert(sizeof(kModifiers) / sizeof(kModifiers[0]) == MODIFIER_LAST,
              "modifier names");

const char* const kPolyphonyModes[] = { "mono", "poly", "2x unison", "cyclic" };
static_assert(sizeof(kPolyphonyModes) / sizeof(kPolyphonyModes[0]) ==
              POLYPHONY_MODE_LAST, "polyphony mode names");

const char* const kOffOn[] = { "off", "on" };

struct PatchByteRange {
  bool used = false;
  int16_t min_value = 0;
  int16_t max_value = 255;
};

class ParameterTable {
 public:
  ParameterTable() {
    BuildPatchParameters();
    BuildPartParameters();
  }

  std::vector<ParameterInfo> parameters;
  PatchByteRange ranges[sizeof(Patch)];

 private:
  const char* Store(const std::string& text) {
    strings_.push_back(text);
    return strings_.back().c_str();
  }

  void AddPatch(const std::string& id, const std::string& name,
                const std::string& group, uint8_t address,
                int min_value, int max_value,
                const char* const* choices = nullptr) {
    const uint8_t byte = reinterpret_cast<const uint8_t*>(&DefaultPatch())[address];
    int default_value = min_value < 0 ? static_cast<int8_t>(byte) : byte;
    if (default_value < min_value) default_value = min_value;
    if (default_value > max_value) default_value = max_value;
    parameters.push_back({ Store(id), Store(name), Store(group), TARGET_PATCH,
                           address, static_cast<int16_t>(min_value),
                           static_cast<int16_t>(max_value),
                           static_cast<int16_t>(default_value), choices });
    ranges[address].used = true;
    ranges[address].min_value = static_cast<int16_t>(min_value);
    ranges[address].max_value = static_cast<int16_t>(max_value);
  }

  void AddPart(const std::string& id, const std::string& name, PartField field,
               int min_value, int max_value,
               const char* const* choices = nullptr) {
    const int default_value = GetPartField(PartSettings(), field);
    parameters.push_back({ Store(id), Store(name), Store("Part"), TARGET_PART,
                           static_cast<uint8_t>(field),
                           static_cast<int16_t>(min_value),
                           static_cast<int16_t>(max_value),
                           static_cast<int16_t>(default_value), choices });
  }

  void BuildPatchParameters() {
    for (int i = 0; i < kNumOscillators; ++i) {
      const std::string n = std::to_string(i + 1);
      const std::string group = "Oscillator " + n;
      const uint8_t base = PRM_PATCH_OSC1_SHAPE + i * 4;
      AddPatch("osc" + n + "_shape", "Osc " + n + " Waveform", group, base + 0,
               0, kNumWaveforms - 1, kOscillatorWaveforms);
      AddPatch("osc" + n + "_parameter", "Osc " + n + " Parameter", group,
               base + 1, 0, 127);
      AddPatch("osc" + n + "_range", "Osc " + n + " Range", group, base + 2,
               -24, 24);
      AddPatch("osc" + n + "_tune", "Osc " + n + " Tune", group, base + 3,
               -64, 64);
    }

    AddPatch("mix_balance", "Osc Mix", "Mixer", PRM_PATCH_MIX_BALANCE, 0, 63);
    AddPatch("mix_operator", "Crossmod", "Mixer", PRM_PATCH_MIX_OPERATOR,
             0, OP_LAST - 1, kMixOperators);
    AddPatch("mix_amount", "Crossmod Amount", "Mixer", PRM_PATCH_MIX_PARAMETER,
             0, 63);
    AddPatch("mix_sub_shape", "Sub Osc Waveform", "Mixer",
             PRM_PATCH_MIX_SUB_SHAPE, 0, WAVEFORM_SUB_OSC_LAST - 1,
             kSubOscillatorWaveforms);
    AddPatch("mix_sub_level", "Sub Osc Level", "Mixer", PRM_PATCH_MIX_SUB_LEVEL,
             0, 63);
    AddPatch("mix_noise", "Noise", "Mixer", PRM_PATCH_MIX_NOISE_LEVEL, 0, 63);
    AddPatch("mix_fuzz", "Fuzz", "Mixer", PRM_PATCH_MIX_FUZZ, 0, 63);
    AddPatch("mix_crush", "Crush", "Mixer", PRM_PATCH_MIX_CRUSH, 0, 31);

    AddPatch("filter_cutoff", "Cutoff", "Filter", PRM_PATCH_FILTER1_CUTOFF,
             0, 127);
    AddPatch("filter_resonance", "Resonance", "Filter",
             PRM_PATCH_FILTER1_RESONANCE, 0, 63);
    AddPatch("filter_mode", "Filter Mode", "Filter", PRM_PATCH_FILTER1_MODE,
             FILTER_MODE_LP, FILTER_MODE_HP, kFilterModes);
    // Plug-in extension in the patch padding: 0 = the Ambika SVF model,
    // 1.. = the Surge XT filters eureka! offers (SurgeFilter's model list),
    // with a per-model variant.
    AddPatch("filter_model", "Filter Model", "Filter", kPatchFilterModel,
             0, SurgeFilter::NumModels(), FilterModelNames());
    AddPatch("filter_variant", "Filter Variant", "Filter", kPatchFilterVariant,
             0, kMaxFilterVariants - 1);
    AddPatch("filter_env", "Env 2 > Cutoff", "Filter", PRM_PATCH_FILTER1_ENV,
             0, 63);
    AddPatch("filter_lfo", "LFO 2 > Cutoff", "Filter", PRM_PATCH_FILTER1_LFO,
             0, 63);

    for (int i = 0; i < kNumEnvelopes; ++i) {
      const std::string n = std::to_string(i + 1);
      const std::string group = "Envelope/LFO " + n;
      const uint8_t base = PRM_PATCH_ENV_ATTACK + i * 8;
      AddPatch("env" + n + "_attack", "Env " + n + " Attack", group, base + 0,
               0, 127);
      AddPatch("env" + n + "_decay", "Env " + n + " Decay", group, base + 1,
               0, 127);
      AddPatch("env" + n + "_sustain", "Env " + n + " Sustain", group,
               base + 2, 0, 127);
      AddPatch("env" + n + "_release", "Env " + n + " Release", group,
               base + 3, 0, 127);
      AddPatch("lfo" + n + "_shape", "LFO " + n + " Waveform", group, base + 4,
               0, LFO_WAVEFORM_LAST - 1, kLfoShapes);
      AddPatch("lfo" + n + "_rate", "LFO " + n + " Rate", group, base + 5,
               0, kNumSyncedLfoRates + 127, LfoRateNames());
      AddPatch("lfo" + n + "_sync", "LFO " + n + " Trigger", group, base + 7,
               0, LFO_SYNC_MODE_LAST - 1, kLfoSyncModes);
    }

    AddPatch("voice_lfo_shape", "Voice LFO Waveform", "Voice LFO",
             PRM_PATCH_VOICE_LFO_SHAPE, 0, LFO_WAVEFORM_RAMP, kLfoShapes);
    AddPatch("voice_lfo_rate", "Voice LFO Rate", "Voice LFO",
             PRM_PATCH_VOICE_LFO_RATE, 0, 127);

    for (int i = 0; i < kNumModulations; ++i) {
      const std::string n = std::to_string(i + 1);
      const std::string group = "Modulation " + n;
      const uint8_t base = PRM_PATCH_MOD_SOURCE + i * 3;
      AddPatch("mod" + n + "_source", "Mod " + n + " Source", group, base + 0,
               0, MOD_SRC_CONSTANT_256, kModulationSources);
      AddPatch("mod" + n + "_destination", "Mod " + n + " Destination", group,
               base + 1, 0, kNumPluginModDestinations - 1, kModulationDestinations);
      AddPatch("mod" + n + "_amount", "Mod " + n + " Amount", group, base + 2,
               -63, 63);
    }

    for (int i = 0; i < kNumModifiers; ++i) {
      const std::string n = std::to_string(i + 1);
      const std::string group = "Modifier " + n;
      const uint8_t base = PRM_PATCH_MOD_OPERAND1 + i * 3;
      AddPatch("modifier" + n + "_in1", "Modifier " + n + " In 1", group,
               base + 0, 0, MOD_SRC_LAST - 1, kModulationSources);
      AddPatch("modifier" + n + "_in2", "Modifier " + n + " In 2", group,
               base + 1, 0, MOD_SRC_LAST - 1, kModulationSources);
      AddPatch("modifier" + n + "_operator", "Modifier " + n + " Operator",
               group, base + 2, 0, MODIFIER_LAST - 1, kModifiers);
    }
  }

  void BuildPartParameters() {
    AddPart("part_volume", "Volume", PART_VOLUME, 0, 127);
    AddPart("part_pan", "Pan", PART_PAN, -64, 63);
    AddPart("part_octave", "Octave", PART_OCTAVE, -2, 2);
    AddPart("part_tuning", "Tune", PART_TUNING, -127, 127);
    AddPart("part_spread", "Spread", PART_SPREAD, 0, 40);
    AddPart("part_legato", "Legato", PART_LEGATO, 0, 1, kOffOn);
    AddPart("part_portamento", "Portamento", PART_PORTAMENTO, 0, 63);
    AddPart("part_polyphony", "Voicing", PART_POLYPHONY_MODE,
            0, POLYPHONY_MODE_LAST - 1, kPolyphonyModes);
  }

  const char* const* FilterModelNames() {
    if (filter_model_names_.empty()) {
      filter_model_names_.push_back("Ambika SVF");
      for (int model = 1; model <= SurgeFilter::NumModels(); ++model) {
        filter_model_names_.push_back(SurgeFilter::ModelName(model));
      }
    }
    return filter_model_names_.data();
  }

  // LFO rates 0..14 are clock divisions, then free-running rates 0..127.
  const char* const* LfoRateNames() {
    if (lfo_rate_names_.empty()) {
      static const char* const kSynced[kNumSyncedLfoRates] = {
        "1/1", "3/4", "2/3", "1/2", "3/8", "1/3", "1/4", "1/6",
        "1/8", "1/12", "1/16", "1/24", "1/32", "1/48", "1/96",
      };
      for (const char* name : kSynced) {
        lfo_rate_names_.push_back(name);
      }
      for (int i = 0; i < 128; ++i) {
        lfo_rate_names_.push_back(Store(std::to_string(i)));
      }
    }
    return lfo_rate_names_.data();
  }

  // deque: push_back never moves existing strings, so c_str() stays valid.
  std::deque<std::string> strings_;
  std::vector<const char*> lfo_rate_names_;
  std::vector<const char*> filter_model_names_;
};

const ParameterTable& Table() {
  static const ParameterTable table;
  return table;
}

}  // namespace

const Patch& DefaultPatch() {
  static const Patch patch = [] {
    Patch p;
    ResourcesManager::Load(&init_patch, 0, &p);
    return p;
  }();
  return patch;
}

int NumParameters() { return static_cast<int>(Table().parameters.size()); }

const ParameterInfo& GetParameter(int index) { return Table().parameters[index]; }

uint8_t ClampPatchByte(uint8_t address, uint8_t value) {
  if (address >= sizeof(Patch)) {
    return value;
  }
  const PatchByteRange& range = Table().ranges[address];
  if (!range.used) {
    return value;
  }
  int v = range.min_value < 0 ? static_cast<int8_t>(value) : value;
  if (v < range.min_value) v = range.min_value;
  if (v > range.max_value) v = range.max_value;
  return static_cast<uint8_t>(v);
}

int GetPartField(const PartSettings& settings, uint8_t field) {
  switch (field) {
    case PART_VOLUME: return settings.volume;
    case PART_OCTAVE: return settings.octave;
    case PART_TUNING: return settings.tuning;
    case PART_SPREAD: return settings.spread;
    case PART_LEGATO: return settings.legato;
    case PART_PORTAMENTO: return settings.portamento_time;
    case PART_POLYPHONY_MODE: return settings.polyphony_mode;
    case PART_PAN: return settings.pan - 64;
    default: return 0;
  }
}

void SetPartField(PartSettings* settings, uint8_t field, int value) {
  auto clamp = [&](int lo, int hi) { return value < lo ? lo : value > hi ? hi : value; };
  switch (field) {
    case PART_VOLUME: settings->volume = clamp(0, 127); break;
    case PART_OCTAVE: settings->octave = clamp(-2, 2); break;
    case PART_TUNING: settings->tuning = clamp(-127, 127); break;
    case PART_SPREAD: settings->spread = clamp(0, 40); break;
    case PART_LEGATO: settings->legato = clamp(0, 1); break;
    case PART_PORTAMENTO: settings->portamento_time = clamp(0, 63); break;
    case PART_PAN: settings->pan = static_cast<uint8_t>(clamp(-64, 63) + 64); break;
    case PART_POLYPHONY_MODE:
      settings->polyphony_mode = clamp(0, POLYPHONY_MODE_LAST - 1);
      break;
    default: break;
  }
}

}  // namespace plugin
}  // namespace ambika
