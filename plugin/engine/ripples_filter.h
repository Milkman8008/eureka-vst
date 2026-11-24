// One voice's Ripples filter: Tyler Coy's circuit model of the Mutable
// Instruments Ripples (ripples/ripples_core.h), driven by the voicecard's
// cutoff and resonance CVs. Its four cells give three responses (variants):
// LP4 (24 dB), LP2 (12 dB) and BP2, as on the module's outputs.

#ifndef AMBIKA_PLUGIN_RIPPLES_FILTER_H_
#define AMBIKA_PLUGIN_RIPPLES_FILTER_H_

#include <memory>
#include <string>

namespace ambika {
namespace plugin {

class RipplesFilter {
 public:
  RipplesFilter();
  ~RipplesFilter();

  void Init(double sample_rate);
  void Reset();
  // Off for response measurements; on, a little noise lets the filter start
  // self-oscillating from silence, as the hardware does.
  void set_noise(bool enabled);

  // One sample (eureka! level, +/-1 full scale). resonance 0..1.
  float Process(float input, double cutoff_hz, float resonance, int variant);

  static int NumVariants() { return 3; }
  // Small-signal magnitude response (linear), for the editor's display.
  static double Magnitude(double frequency_hz, double cutoff_hz, float resonance, int variant);
  static std::string VariantName(int variant);

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace plugin
}  // namespace ambika

#endif  // AMBIKA_PLUGIN_RIPPLES_FILTER_H_
