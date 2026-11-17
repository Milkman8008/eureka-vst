// One voice's Surge XT filter (sst-filters library, GPL-3.0, by the Surge
// Synth Team). Wraps the SIMD QuadFilterUnit API behind a scalar interface;
// the header has no sst-filters types so only surge_filter.cc needs C++20 and
// the SIMD headers.

#ifndef AMBIKA_PLUGIN_SURGE_FILTER_H_
#define AMBIKA_PLUGIN_SURGE_FILTER_H_

#include <memory>
#include <string>

namespace ambika {
namespace plugin {

class SurgeFilter {
 public:
  SurgeFilter();
  ~SurgeFilter();

  // block_size: samples between SetParameters() calls; coefficients are
  // interpolated over a block, as in Surge.
  void Init(double sample_rate, int block_size);
  void Reset();

  // Once per block. model is a eureka! filter model (1..NumModels()), which
  // selects one of the Surge filter types eureka! offers; subtype is clamped
  // to that type's range. resonance 0..1.
  void SetParameters(int model, int subtype, double cutoff_hz, float resonance);
  // Processes a block of samples in place (call after SetParameters).
  void Process(float* samples, int num_samples);

  // The Surge filters eureka! offers, in menu order. Model numbers start at
  // 1 (0 is the Ambika SVF, which is not a Surge filter).
  static int NumModels();                  // Valid models are 1..NumModels().
  static const char* ModelName(int model);  // e.g. "Surge LP Diode Ladder"
  static int NumSubtypes(int model);       // 0 if the model has no variants
  static std::string SubtypeName(int model, int subtype);

  // Magnitude response at the given frequencies (linear), measured from the
  // impulse response of a fresh filter. For the editor's display.
  static void MeasureResponse(int model, int subtype, double cutoff_hz, float resonance,
                              double sample_rate, const double* frequencies_hz,
                              double* magnitudes, int count);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace plugin
}  // namespace ambika

#endif  // AMBIKA_PLUGIN_SURGE_FILTER_H_
