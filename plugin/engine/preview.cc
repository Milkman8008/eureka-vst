#include "preview.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstring>

#include "analog.h"
#include "avrlib/random.h"
#include "common/lfo.h"
#include "engine.h"
#include "voicecard/oscillator.h"
#include "voicecard/resources.h"
#include "yam_oscillator.h"

namespace ambika {
namespace plugin {

namespace {

const double kControlRate = Engine::kNativeSampleRate / kAudioBlockSize;

template<typename T>
void ZeroFill(T* object) {
  memset(static_cast<void*>(object), 0, sizeof(T));
}

}  // namespace

void RenderOscillatorPreview(uint8_t shape, uint8_t parameter, int8_t range,
                             uint8_t* out, int num_samples) {
  if (shape >= kNumWaveforms) {
    shape = WAVEFORM_NONE;
  }
  Oscillator oscillator;
  ZeroFill(&oscillator);
  oscillator.Reset();
  oscillator.set_parameter(parameter);
  oscillator.set_fm_parameter(range + 36);
  YamOscillator yam;
  ZeroFill(&yam);
  yam.set_parameter(parameter);
  yam.set_fm_parameter(range + 36);

  // One cycle per num_samples: the phase is 16.8 fixed point over 2^16.
  const uint32_t increment = (1u << 24) / static_cast<uint32_t>(num_samples);
  uint24_t phase_increment;
  phase_increment.integral = static_cast<uint16_t>(increment >> 8);
  phase_increment.fractional = static_cast<uint8_t>(increment & 0xff);

  uint8_t no_sync[kAudioBlockSize] = { 0 };
  uint8_t sync_out[kAudioBlockSize];
  // Render two cycles and keep the second, so stateful algorithms settle.
  for (int pass = 0; pass < 2; ++pass) {
    for (int i = 0; i + kAudioBlockSize <= num_samples; i += kAudioBlockSize) {
      // A low note, so the band-limited tables keep all their harmonics.
      if (IsYamWaveform(shape)) {
        yam.Render(shape, 24, phase_increment, no_sync, sync_out, out + i);
      } else {
        oscillator.Render(shape, 24, phase_increment, no_sync, sync_out, out + i);
      }
    }
  }
}

void RenderLfoPreview(uint8_t shape, uint8_t* out, int num_samples) {
  if (shape >= LFO_WAVEFORM_LAST) {
    shape = LFO_WAVEFORM_TRIANGLE;
  }
  // Fixed seed: the S&H preview should not change on every repaint.
  avrlib::Random::Seed(0x5a5a);
  Lfo lfo;
  ZeroFill(&lfo);
  lfo.set_phase_increment(static_cast<uint16_t>(65536 / num_samples));
  // S&H only picks a new value when the phase wraps; give it a few steps.
  for (int i = 0; i < num_samples; ++i) {
    out[i] = lfo.Render(shape);
    if (shape == LFO_WAVEFORM_S_H && (i % (num_samples / 8)) == 0) {
      lfo.set_phase(0xffff);
    }
  }
}

double EnvelopeSegmentSeconds(uint8_t value) {
  if (value > 127) value = 127;
  const double increment = ResourcesManager::Lookup<uint16_t, uint8_t>(
      lut_res_env_portamento_increments, value);
  return increment > 0 ? 65536.0 / increment / kControlRate : 0.0;
}

double LfoRateHz(uint8_t rate) {
  if (rate > 127) rate = 127;
  const double increment = ResourcesManager::Lookup<uint16_t, uint8_t>(
      lut_res_lfo_increments, rate);
  return increment * kControlRate / 65536.0;
}

double FilterResponse(uint8_t cutoff, uint8_t resonance, uint8_t mode,
                      double frequency_hz) {
  // Same CV path as the voicecard at middle C: cutoff * 128 + pitch - 8192,
  // scaled to 8 bits; resonance << 8 scaled to 8 bits.
  const double cutoff_cv = std::clamp(cutoff * 2.0 - 8.0, 0.0, 255.0);
  const double resonance_cv = std::min(resonance * 4.0, 255.0);
  const double max_cutoff = 0.45 * Engine::kNativeSampleRate * Engine::kOversampling;
  const double fc = std::min(AnalogVoice::CutoffToHz(cutoff_cv), max_cutoff);
  const double k = 1.41 - 1.39 * (resonance_cv / 255.0);

  const std::complex<double> s(0.0, frequency_hz / fc);
  const std::complex<double> denominator = s * s + k * s + 1.0;
  std::complex<double> numerator;
  switch (mode) {
    case 1: numerator = s; break;            // BP
    case 2: numerator = s * s; break;        // HP
    case 3: numerator = s * s + 1.0; break;  // Notch
    default: numerator = 1.0; break;         // LP
  }
  return std::abs(numerator / denominator);
}

}  // namespace plugin
}  // namespace ambika
