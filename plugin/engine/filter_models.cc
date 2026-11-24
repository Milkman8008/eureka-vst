#include "filter_models.h"

#include <algorithm>

#include "ripples_filter.h"
#include "surge_filter.h"

namespace ambika {
namespace plugin {

int RipplesFilterModel() { return SurgeFilter::NumModels() + 1; }

int NumFilterModels() { return RipplesFilterModel() + 1; }

bool IsSurgeFilterModel(int model) {
  return model >= 1 && model <= SurgeFilter::NumModels();
}

const char* FilterModelName(int model) {
  if (model == RipplesFilterModel()) return "Ripples";
  if (IsSurgeFilterModel(model)) return SurgeFilter::ModelName(model);
  return "Ambika SVF";
}

int NumFilterVariants(int model) {
  if (model == RipplesFilterModel()) return RipplesFilter::NumVariants();
  if (IsSurgeFilterModel(model)) return SurgeFilter::NumSubtypes(model);
  return 0;
}

std::string FilterVariantName(int model, int variant) {
  if (model == RipplesFilterModel()) return RipplesFilter::VariantName(variant);
  if (IsSurgeFilterModel(model)) return SurgeFilter::SubtypeName(model, variant);
  return "-";
}

void MeasureFilterResponse(int model, int variant, double cutoff_hz, float resonance,
                           double sample_rate, const double* frequencies_hz,
                           double* magnitudes, int count) {
  if (model != RipplesFilterModel()) {
    SurgeFilter::MeasureResponse(model, variant, cutoff_hz, resonance, sample_rate,
                                 frequencies_hz, magnitudes, count);
    return;
  }
  // Self-oscillating settings have no meaningful impulse response, so the
  // display uses the circuit's small-signal response instead.
  (void) sample_rate;
  for (int f = 0; f < count; ++f) {
    magnitudes[f] = RipplesFilter::Magnitude(frequencies_hz[f], cutoff_hz, resonance, variant);
  }
}

}  // namespace plugin
}  // namespace ambika
