// Oscillators from YAM ("Yet Another Mutation"), an alternative Ambika
// voicecard firmware based on Emilie Gillet's original (GPL-3.0).
//
// The stock oscillators are left untouched: these shapes are added after the
// last stock waveform, so existing patches keep their sound. The render code
// is YAM's voicecard/oscillator.cc, kept as close to the original as
// possible (including its 8-bit arithmetic and shared state layout), in its
// own class so it can live next to the stock Oscillator.
//
//   yam_square    PolyBLEP square with proper PWM (no level drop).
//   yam_saw       PolyBLEP saw; the parameter mixes in a 2nd saw edge.
//   yam_triangle  Naive triangle, folded (not clipped) by the parameter.
//   yam_qpwm      Four detuned pulse waves; the parameter sets both the
//                 detune and the pulse width.
//   yam_fmfb      FM with feedback: like fm, with feedback added above 64.
//   yam_csaw      PolyBLEP CS-80 style saw: a notch before the ramp.
//   yam_vowel2    The vowel oscillator without the phase noise.
//
// YAM's "oldsaw" (the original saw) is not included: it is the stock saw.

#ifndef AMBIKA_PLUGIN_YAM_OSCILLATOR_H_
#define AMBIKA_PLUGIN_YAM_OSCILLATOR_H_

#include "avrlib/op.h"
#include "common/patch.h"
#include "voicecard/oscillator.h"

namespace ambika {
namespace plugin {

enum YamShape {
  YAM_SQUARE,
  YAM_SAW,
  YAM_TRIANGLE,
  YAM_QUAD_PWM,
  YAM_FM_FB,
  YAM_CSAW,
  YAM_VOWEL_2,
  YAM_LAST
};

// Patch waveform numbers: the YAM shapes follow the stock list.
const uint8_t kFirstYamWaveform = WAVEFORM_LAST;
const uint8_t kNumWaveforms = WAVEFORM_LAST + YAM_LAST;
const uint8_t kWaveformYamFmFb = kFirstYamWaveform + YAM_FM_FB;

inline bool IsYamWaveform(uint8_t shape) {
  return shape >= kFirstYamWaveform && shape < kNumWaveforms;
}

class YamOscillator {
 public:
  // shape: the patch waveform number (kFirstYamWaveform ...).
  void Render(uint8_t shape, uint8_t note, uint24_t increment,
              uint8_t* sync_input, uint8_t* sync_output, uint8_t* buffer);

  inline void set_parameter(uint8_t parameter) { parameter_ = parameter; }
  inline void set_fm_parameter(uint8_t fm_parameter) {
    fm_parameter_ = fm_parameter;
  }

 private:
  // YAM's OscillatorState: the members share memory, as on the voicecard.
  struct QuadState {
    uint16_t phase[3];
  };
  union State {
    VowelSynthesizerState vw;
    QuadState qs;
    uint8_t qsbytes[6];
    uint16_t secondary_phase;
    uint8_t output_sample;
  };

  void RenderPolyBlepPwm(uint8_t* buffer);
  void RenderPolyBlepSaw(uint8_t* buffer);
  void RenderPolyBlepCSaw(uint8_t* buffer);
  void RenderNewTriangle(uint8_t* buffer);
  void RenderQuadPwm(uint8_t* buffer);
  void RenderFmFeedback(uint8_t* buffer);
  void RenderVowel2(uint8_t* buffer);

  uint24_t phase_;
  uint24_t phase_increment_;
  uint8_t shape_;
  uint8_t parameter_;
  uint8_t fm_parameter_;
  uint8_t note_;
  State data_;
  uint8_t* sync_input_;
  uint8_t* sync_output_;
};

}  // namespace plugin
}  // namespace ambika

#endif  // AMBIKA_PLUGIN_YAM_OSCILLATOR_H_
