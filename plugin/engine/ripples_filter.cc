#include "ripples_filter.h"

#include <algorithm>
#include <cmath>

#include "ripples/ripples_core.h"

namespace ambika {
namespace plugin {

namespace {

// eureka!'s +/-1 full scale as a Eurorack audio level (10 V peak to peak),
// so the cells and the resonance OTA saturate as on the module.
const float kVolts = 5.0f;

}  // namespace

struct RipplesFilter::Impl {
  ripples::RipplesEngine engine;
};

RipplesFilter::RipplesFilter() : impl_(std::make_unique<Impl>()) {}
RipplesFilter::~RipplesFilter() = default;

void RipplesFilter::Init(double sample_rate) {
  impl_->engine.setSampleRate(static_cast<float>(sample_rate));
}

void RipplesFilter::Reset() { impl_->engine.reset(); }

void RipplesFilter::set_noise(bool enabled) { impl_->engine.set_noise(enabled); }

float RipplesFilter::Process(float input, double cutoff_hz, float resonance, int variant) {
  const float v_oct = static_cast<float>(std::log2(std::max(cutoff_hz, 1.0) / 20000.0));
  const auto out = impl_->engine.process(input * kVolts, v_oct, std::clamp(resonance, 0.0f, 1.0f));
  // The module's output amplifiers invert; flip back so every filter model
  // keeps the voice's polarity.
  float y;
  switch (variant) {
    case 1: y = -out.lp2; break;
    case 2: y = -out.bp2; break;
    default: y = -out.lp4; break;
  }
  y *= 1.0f / kVolts;
  return std::isfinite(y) ? y : 0.0f;
}

double RipplesFilter::Magnitude(double frequency_hz, double cutoff_hz, float resonance, int variant) {
  const float v_oct = static_cast<float>(std::log2(std::max(cutoff_hz, 1.0) / 20000.0));
  return std::abs(ripples::RipplesEngine::LinearResponse(
      frequency_hz, v_oct, std::clamp(resonance, 0.0f, 1.0f), std::clamp(variant, 0, NumVariants() - 1)));
}

std::string RipplesFilter::VariantName(int variant) {
  static const char* const kNames[] = { "LP4", "LP2", "BP2" };
  return kNames[std::clamp(variant, 0, NumVariants() - 1)];
}

}  // namespace plugin
}  // namespace ambika
