#include "surge_filter.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <vector>

#include "sst/filters.h"

namespace ambika {
namespace plugin {

namespace sf = sst::filters;

namespace {

constexpr int kDelayLength =
    sf::utilities::MAX_FB_COMB + sf::utilities::SincTable::FIRipol_N;

// The Surge filters eureka! offers, in menu order (model 1, 2, ...).
struct Model {
  sf::FilterType type;
  const char* name;
};
const Model kModels[] = {
  { sf::fut_obxd_xpander, "Surge MULTI Xpander" },
  { sf::fut_diode, "Surge LP Diode Ladder" },
  { sf::fut_lpmoog, "Surge LP Legacy Ladder" },
  { sf::fut_vintageladder, "Surge LP Vintage Ladder" },
  { sf::fut_comb_pos, "Surge FX Comb+" },
  { sf::fut_comb_neg, "Surge FX Comb-" },
  { sf::fut_SNH, "Surge FX Sample and Hold" },
  { sf::fut_cutoffwarp_ap, "Surge FX Cutoff Warp AP" },
};
constexpr int kNumModels = sizeof(kModels) / sizeof(kModels[0]);

const Model& ModelFor(int model) {
  return kModels[std::clamp(model, 1, kNumModels) - 1];
}

// From here on "type" is an sst::filters::FilterType.
int ClampSubtype(int type, int subtype) {
  const int count = sf::fut_subcount[type];
  return count > 0 ? std::clamp(subtype, 0, count - 1) : 0;
}

}  // namespace

struct SurgeFilter::Impl {
  sf::QuadFilterUnitState state {};
  sf::FilterCoefficientMaker<> maker;
  // Comb filters read a delay line per SIMD lane; all four must be valid.
  std::vector<float> delay = std::vector<float>(4 * kDelayLength, 0.0f);
  sf::FilterUnitQFPtr unit = nullptr;
  int type = -1;
  int subtype = -1;
  double sample_rate = 48000.0;
  int block_size = 32;

  void ResetState() {
    std::fill(delay.begin(), delay.end(), 0.0f);
    for (int i = 0; i < 4; ++i) {
      state.DB[i] = delay.data() + i * kDelayLength;
      state.active[i] = static_cast<int>(0xffffffff);
      state.WP[i] = 0;
    }
    for (auto& r : state.R) r = SIMD_MM(setzero_ps)();
    for (auto& c : state.C) c = SIMD_MM(setzero_ps)();
    for (auto& d : state.dC) d = SIMD_MM(setzero_ps)();
    state.sampleRate = static_cast<float>(sample_rate);
    state.sampleRateInv = static_cast<float>(1.0 / sample_rate);
    maker.Reset();
    maker.setSampleRateAndBlockSize(static_cast<float>(sample_rate), block_size);
  }
};

SurgeFilter::SurgeFilter() : impl_(std::make_unique<Impl>()) {}
SurgeFilter::~SurgeFilter() = default;

void SurgeFilter::Init(double sample_rate, int block_size) {
  impl_->sample_rate = sample_rate;
  impl_->block_size = std::max(1, block_size);
  Reset();
}

void SurgeFilter::Reset() {
  impl_->ResetState();
  impl_->type = -1;
  impl_->subtype = -1;
}

void SurgeFilter::SetParameters(int model, int subtype, double cutoff_hz, float resonance) {
  const int type = ModelFor(model).type;
  subtype = ClampSubtype(type, subtype);
  if (type != impl_->type || subtype != impl_->subtype) {
    // A different model: start from silence rather than reinterpret the
    // previous filter's registers.
    impl_->ResetState();
    impl_->type = type;
    impl_->subtype = subtype;
    impl_->unit = sf::GetQFPtrFilterUnit(static_cast<sf::FilterType>(type),
                                         static_cast<sf::FilterSubType>(subtype));
  }
  // sst-filters takes the cutoff as a MIDI-style note number, A440 = 0.
  const double hz = std::clamp(cutoff_hz, 5.0, impl_->sample_rate * 0.45);
  const float note = static_cast<float>(12.0 * std::log2(hz / 440.0));
  impl_->maker.MakeCoeffs(note, std::clamp(resonance, 0.0f, 0.99f),
                          static_cast<sf::FilterType>(type),
                          static_cast<sf::FilterSubType>(subtype), nullptr, false);
  impl_->maker.updateState(impl_->state);
}

void SurgeFilter::Process(float* samples, int num_samples) {
  if (impl_->unit == nullptr) {
    return;
  }
  for (int i = 0; i < num_samples; ++i) {
    const auto out = impl_->unit(&impl_->state, SIMD_MM(set1_ps)(samples[i]));
    const float y = SIMD_MM(cvtss_f32)(out);
    // A filter driven into instability must not poison the mix.
    samples[i] = std::isfinite(y) ? y : 0.0f;
  }
  impl_->maker.updateCoefficients(impl_->state);
}

int SurgeFilter::NumModels() { return kNumModels; }

const char* SurgeFilter::ModelName(int model) { return ModelFor(model).name; }

int SurgeFilter::NumSubtypes(int model) { return sf::fut_subcount[ModelFor(model).type]; }

std::string SurgeFilter::SubtypeName(int model, int subtype) {
  const int type = ModelFor(model).type;
  if (sf::fut_subcount[type] == 0) {
    return "-";
  }
  const int i = ClampSubtype(type, subtype);
  // Mirrors sst-filters' FilterConfigurationLabels.h, without its fmt dependency.
  switch (static_cast<sf::FilterType>(type)) {
    case sf::fut_lpmoog:
    case sf::fut_diode:
      return sf::fut_ldr_subtypes[i];
    case sf::fut_notch12:
    case sf::fut_notch24:
    case sf::fut_apf:
      return sf::fut_notch_subtypes[i];
    case sf::fut_comb_pos:
    case sf::fut_comb_neg:
      return sf::fut_comb_subtypes[i];
    case sf::fut_vintageladder:
      return sf::fut_vintageladder_subtypes[i];
    case sf::fut_obxd_2pole_lp:
    case sf::fut_obxd_2pole_hp:
    case sf::fut_obxd_2pole_n:
    case sf::fut_obxd_2pole_bp:
      return sf::fut_obxd_2p_subtypes[i];
    case sf::fut_obxd_4pole:
      return sf::fut_obxd_4p_subtypes[i];
    case sf::fut_k35_lp:
    case sf::fut_k35_hp:
      return sf::fut_k35_subtypes[i];
    case sf::fut_cutoffwarp_lp:
    case sf::fut_cutoffwarp_hp:
    case sf::fut_cutoffwarp_n:
    case sf::fut_cutoffwarp_bp:
    case sf::fut_cutoffwarp_ap:
    case sf::fut_resonancewarp_lp:
    case sf::fut_resonancewarp_hp:
    case sf::fut_resonancewarp_n:
    case sf::fut_resonancewarp_bp:
    case sf::fut_resonancewarp_ap:
      return std::string(sf::fut_nlf_subtypes[i & 3]) + " " +
             sf::fut_nlf_saturators[(i >> 2) & 3];
    case sf::fut_bp12:
      return sf::fut_bp12_subtypes[i];
    case sf::fut_tripole:
      return std::string(sf::fut_tripole_subtypes[i & 3]) + " " +
             sf::fut_tripole_output_stage[(i >> 2) & 3];
    case sf::fut_cytomic_svf:
      return sf::fut_cytomic_subtypes[i];
    case sf::fut_obxd_xpander:
      return sf::fut_obxd_xpander_subtypes[i];
    case sf::fut_cytomic_cascade_lp:
    case sf::fut_cytomic_cascade_hp:
    case sf::fut_cytomic_cascade_bp:
    case sf::fut_cytomic_cascade_n:
    case sf::fut_cytomic_cascade_pk:
    case sf::fut_cytomic_cascade_ap:
      return sf::fut_cytomic_cascade_subtypes[i];
    default:
      return sf::fut_def_subtypes[std::min(i, 2)];
  }
}

void SurgeFilter::MeasureResponse(int model, int subtype, double cutoff_hz, float resonance,
                                  double sample_rate, const double* frequencies_hz,
                                  double* magnitudes, int count) {
  constexpr int kLength = 4096;
  constexpr int kBlock = 64;
  // A small impulse keeps the saturating models near their linear region.
  constexpr float kAmplitude = 0.05f;

  SurgeFilter filter;
  filter.Init(sample_rate, kBlock);
  std::vector<float> response(kLength, 0.0f);
  response[0] = kAmplitude;
  for (int i = 0; i < kLength; i += kBlock) {
    filter.SetParameters(model, subtype, cutoff_hz, resonance);
    filter.Process(response.data() + i, kBlock);
  }
  for (int f = 0; f < count; ++f) {
    const double w = 2.0 * 3.14159265358979323846 * frequencies_hz[f] / sample_rate;
    // DFT at one frequency, with a rotating phasor instead of sin/cos per tap.
    const std::complex<double> step = std::polar(1.0, -w);
    std::complex<double> phasor = 1.0;
    std::complex<double> sum = 0.0;
    for (int n = 0; n < kLength; ++n) {
      sum += static_cast<double>(response[n]) * phasor;
      phasor *= step;
    }
    magnitudes[f] = std::abs(sum) / kAmplitude;
  }
}

}  // namespace plugin
}  // namespace ambika
