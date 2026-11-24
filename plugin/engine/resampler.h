// Band-limited sample rate converter (Kaiser-windowed sinc).
//
// Converts the engine's fixed internal rate to the host rate. The cutoff is
// placed below the lower of the two Nyquist frequencies, so downsampling
// (e.g. 78.4 kHz -> 44.1/48 kHz) does not alias.

#ifndef AMBIKA_PLUGIN_RESAMPLER_H_
#define AMBIKA_PLUGIN_RESAMPLER_H_

#include <stdint.h>

#include <vector>

namespace ambika {
namespace plugin {

class Resampler {
 public:
  void Init(double input_rate, double output_rate);
  void Reset();
  // Empties the buffer and takes other's read/write positions, so this
  // resampler stays sample-aligned with other from now on (both must have
  // been initialised with the same rates).
  void SyncTo(const Resampler& other);

  // Input side.
  void Push(const float* samples, int count);

  // Output side: true if Pop() can produce a sample without more input.
  bool CanPop() const;
  float Pop();

  // Input samples between the newest input and the next output (latency).
  int latency_in_input_samples() const { return half_width_; }

 private:
  float Kernel(double t) const;

  static const int kZeroCrossings = 16;
  static const int kTableResolution = 256;  // Points per input sample.
  static const int kBufferSize = 8192;       // Power of two.

  double step_ = 1.0;     // Input samples per output sample.
  double position_ = 0.0;  // Absolute input position of the next output.
  int64_t written_ = 0;    // Input samples written so far.
  int half_width_ = 16;    // Kernel half width in input samples.
  double cutoff_ = 1.0;    // Relative to the input Nyquist frequency.
  std::vector<float> table_;
  std::vector<float> buffer_;
};

}  // namespace plugin
}  // namespace ambika

#endif  // AMBIKA_PLUGIN_RESAMPLER_H_
