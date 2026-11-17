// Helpers for the editor's displays. They use the firmware's own code and
// tables, so what is drawn is what the engine plays.

#ifndef AMBIKA_PLUGIN_PREVIEW_H_
#define AMBIKA_PLUGIN_PREVIEW_H_

#include <stdint.h>

namespace ambika {
namespace plugin {

// One cycle of an oscillator waveform (shape and parameter as in the patch),
// rendered by the voicecard oscillator. out receives num_samples values
// (0..255, 128 = centre); num_samples must be a multiple of 40.
void RenderOscillatorPreview(uint8_t shape, uint8_t parameter, int8_t range,
                             uint8_t* out, int num_samples);

// One cycle of an LFO shape (0..255). S&H draws a fixed random staircase.
void RenderLfoPreview(uint8_t shape, uint8_t* out, int num_samples);

// Duration in seconds of an envelope segment for a 0..127 time value
// (attack, decay or release), from the firmware's increment table.
double EnvelopeSegmentSeconds(uint8_t value);

// Frequency of a free-running LFO rate value (0..127), in Hz.
double LfoRateHz(uint8_t rate);

// Magnitude response (linear) of the filter model at frequency_hz, for a
// patch cutoff (0..127) and resonance (0..63), heard at middle C.
double FilterResponse(uint8_t cutoff, uint8_t resonance, uint8_t mode,
                      double frequency_hz);

}  // namespace plugin
}  // namespace ambika

#endif  // AMBIKA_PLUGIN_PREVIEW_H_
