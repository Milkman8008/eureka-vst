// The filter models a part can choose (patch byte kPatchFilterModel), in menu
// order. Model numbers are stored in patches and presets, so new models are
// only ever added at the end:
//
//   0          Ambika SVF (analog.cc)
//   1..8       Surge XT filters (surge_filter.cc)
//   9          Ripples (ripples_filter.cc)

#ifndef AMBIKA_PLUGIN_FILTER_MODELS_H_
#define AMBIKA_PLUGIN_FILTER_MODELS_H_

#include <string>
#include <vector>

namespace ambika {
namespace plugin {

static const int kFilterModelAmbika = 0;

int NumFilterModels();          // Including the Ambika SVF: models 0..N-1.
// The models in the order the editor's menu lists them: Ripples (the
// default for new patches), the Ambika SVF, then the Surge filters.
std::vector<int> FilterModelMenuOrder();
int RipplesFilterModel();       // The Ripples model number.
bool IsSurgeFilterModel(int model);
const char* FilterModelName(int model);
int NumFilterVariants(int model);  // 0 if the model has no variants.
std::string FilterVariantName(int model, int variant);

// Magnitude response (linear) of a Surge or Ripples model at the given
// frequencies, measured from the filter's impulse response. resonance 0..1.
void MeasureFilterResponse(int model, int variant, double cutoff_hz, float resonance,
                           double sample_rate, const double* frequencies_hz,
                           double* magnitudes, int count);

}  // namespace plugin
}  // namespace ambika

#endif  // AMBIKA_PLUGIN_FILTER_MODELS_H_
