// Mutable Instruments Ripples emulation.
// Copyright (C) 2020 Tyler Coy (circuit model, from VCV Rack's
// AudibleInstruments, src/Ripples/ripples.hpp at commit 597d8a6).
// Plug-in port: VCV Rack's simd::float_4, dsp::TRCFilter and random were
// replaced by plain C++ equivalents, the knob/CV front end was reduced to the
// controls eureka! drives (cutoff in V/oct, resonance 0..1), and the input
// noise uses a local generator. The circuit model itself is unchanged.
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.

#ifndef AMBIKA_PLUGIN_RIPPLES_CORE_H_
#define AMBIKA_PLUGIN_RIPPLES_CORE_H_

#include <stdint.h>

#include <algorithm>
#include <cmath>
#include <complex>

// Four floats processed together (the four filter cells, or the four
// signals the AA filters carry). A Clang vector type: both the macOS build
// and the Windows build (clang-cl) are Clang.
typedef float RipplesFloat4 __attribute__((ext_vector_type(4)));

#include "aafilter.hpp"

namespace ripples {

typedef RipplesFloat4 float4;

static const double kPi = 3.14159265358979323846;

// VCV Rack's dsp::TRCFilter: a one-pole RC filter, bilinear transform.
template <typename T>
struct TRCFilter {
  T c = 0.f;
  T xstate = 0.f;
  T ystate = 0.f;
  void reset() { xstate = 0.f; ystate = 0.f; }
  // f: cutoff frequency / sample rate.
  void setCutoffFreq(T f) { c = 2.f / (2.f * static_cast<float>(kPi) * f); }
  void process(T x) {
    T y = (x + xstate - ystate * (1.f - c)) / (1.f + c);
    xstate = x;
    ystate = y;
  }
  T lowpass() const { return ystate; }
  T highpass() const { return xstate - ystate; }
};

// Frequency CV amplifier
static const float kFreqKnobMax = 20000.f;
static const float kVCAGainConstant = -33e-3f;
static const float kPlus6dB = 20.f * std::log10(2.f);
static const float kFreqAmpGain = kVCAGainConstant * kPlus6dB;
static const float kFreqInputR = 100e3f;
static const float kFreqAmpR = -kFreqAmpGain * kFreqInputR;
static const float kFreqAmpC = 560e-12f;

// Resonance CV amplifier
static const float kResInputR = 22e3f;
static const float kResKnobV = 12.f;
static const float kResKnobR = 62e3f;
static const float kResAmpR = 47e3f;
static const float kResAmpC = 560e-12f;

// Gain CV amplifier (normalled to +12 V: the VCA is fully open)
static const float kGainInputR = 27e3f;
static const float kGainNormalV = 12.f;
static const float kGainNormalR = 15e3f;
static const float kGainAmpR = 47e3f;
static const float kGainAmpC = 560e-12f;

// Filter core
static const float kFilterMaxCutoff = kFreqKnobMax;
static const float kFilterCellR = 33e3f;
static const float kFilterCellRC =
    1.f / (2.f * static_cast<float>(kPi) * kFilterMaxCutoff);
static const float kFilterInputR = 100e3f;
static const float kFilterInputGain = kFilterCellR / kFilterInputR;
static const float kFilterCellSelfModulation = 0.01f;

// Filter core feedback path
static const float kFeedbackRt = 22e3f;
static const float kFeedbackRb = 1e3f;
static const float kFeedbackR = kFeedbackRt + kFeedbackRb;
static const float kFeedbackGain = kFeedbackRb / kFeedbackR;

// Filter core feedforward path
static const float kFeedforwardRt = 300e3f;
static const float kFeedforwardRb = 1e3f;
static const float kFeedforwardR = kFeedforwardRt + kFeedforwardRb;
static const float kFeedforwardGain = kFeedforwardRb / kFeedforwardR;
static const float kFeedforwardC = 220e-9f;

// Filter output amplifiers
static const float kLP2Gain = -100e3f / 39e3f;
static const float kLP4Gain = -100e3f / 33e3f;
static const float kBP2Gain = -100e3f / 39e3f;

// VCA
static const float kVCAInputC = 4.7e-6f;
static const float kVCAInputRt = 100e3f;
static const float kVCAInputRb = 1e3f;
static const float kVCAInputR = kVCAInputRt + kVCAInputRb;
static const float kVCAInputGain = kVCAInputRb / kVCAInputR;
static const float kVCAOutputR = 100e3f;

// Voltage-to-current converters
static const float kVtoICollectorVSat = -10.f;

// Opamp saturation voltage
static const float kOpampSatV = 10.6f;

class RipplesEngine {
 public:
  // Outputs of the module, in volts.
  struct Outputs {
    float bp2, lp2, lp4, lp4vca;
  };

  RipplesEngine() { setSampleRate(48000.f); }

  void setSampleRate(float sample_rate) {
    sample_time_ = 1.f / sample_rate;
    aa_filter_.Init(sample_rate);
    const float oversample_rate =
        sample_rate * static_cast<float>(aa_filter_.GetOversamplingFactor());
    const float k2Pi = 2.f * static_cast<float>(kPi);
    const float freq_cut = 1.f / (k2Pi * kFreqAmpR * kFreqAmpC);
    const float res_cut = 1.f / (k2Pi * kResAmpR * kResAmpC);
    const float gain_cut = 1.f / (k2Pi * kGainAmpR * kGainAmpC);
    const float ff_cut = 1.f / (k2Pi * kFeedforwardR * kFeedforwardC);
    rc_filters_.setCutoffFreq(float4{ ff_cut, freq_cut, res_cut, gain_cut } / oversample_rate);
    vca_hpf_.setCutoffFreq(1.f / (k2Pi * kVCAInputR * kVCAInputC) / oversample_rate);
    reset();
  }

  void reset() {
    cell_voltage_ = 0.f;
    rc_filters_.reset();
    vca_hpf_.reset();
    aa_filter_.Init(1.f / sample_time_);
  }

  // Noise added to the input so the filter can start self-oscillating on
  // its own (as in the original). Off for response measurements.
  void set_noise(bool enabled) { noise_ = enabled; }

  // Small-signal (linearised) response of the circuit at frequency_hz, for
  // displays: the four cells are -w/(s+w) each, the OTA's gain around zero
  // is i_reso / (2 Vt), and the feedforward path is the input high-passed by
  // its RC. output: 0 = LP4, 1 = LP2, 2 = BP2. Sign as on the module.
  static std::complex<double> LinearResponse(double frequency_hz, float v_oct,
                                             float resonance, int output) {
    v_oct = std::min(v_oct, 0.f);
    const double i_reso = VtoIConverter(kResAmpR, 0.f, kResInputR,
                                        resonance * kResKnobV, kResKnobR);
    const double kVt = 8.617333262145e-5 * (40.0 + 273.15);
    const double ota = kFilterCellR * i_reso / (2.0 * kVt);
    const double k2Pi = 2.0 * kPi;
    const std::complex<double> s(0.0, k2Pi * frequency_hz);
    const double w = std::exp2(static_cast<double>(v_oct)) / kFilterCellRC;
    const double w_ff = 1.0 / (kFeedforwardR * kFeedforwardC);
    const std::complex<double> cell = -w / (s + w);
    const std::complex<double> hp_ff = s / (s + w_ff);
    // u = a*in - b*v3, v3 = cell^4 * u.
    const std::complex<double> a = static_cast<double>(kFilterInputGain) + ota * kFeedforwardGain * hp_ff;
    const double b = ota * kFeedbackGain;
    const std::complex<double> c2 = cell * cell;
    const std::complex<double> u = a / (1.0 + b * c2 * c2);
    switch (output) {
      case 1: return static_cast<double>(kLP2Gain) * c2 * u;
      case 2: return static_cast<double>(kBP2Gain) * (cell + c2) * u;
      default: return static_cast<double>(kLP4Gain) * c2 * c2 * u;
    }
  }

  // input in volts; v_oct = log2(cutoff / 20 kHz) (<= 0);
  // resonance = the Res knob, 0..1.
  Outputs process(float input, float v_oct, float resonance) {
    v_oct = std::min(v_oct, 0.f);
    const float i_reso = VtoIConverter(kResAmpR, 0.f, kResInputR,
                                       resonance * kResKnobV, kResKnobR);
    const float i_vca = VtoIConverter(kGainAmpR, kGainNormalV,
                                      kGainInputR + kGainNormalR);

    const int factor = aa_filter_.GetOversamplingFactor();
    const float timestep = sample_time_ / static_cast<float>(factor);
    if (noise_) {
      noise_state_ = noise_state_ * 1664525u + 1013904223u;
      input += 1e-6f * (static_cast<float>(noise_state_ >> 8) * (1.f / 16777216.f) - 0.5f);
    }
    float4 inputs = float4{ input, v_oct, i_reso, i_vca } * static_cast<float>(factor);
    float4 outputs = 0.f;
    for (int i = 0; i < factor; ++i) {
      float4 up = aa_filter_.ProcessUp(i == 0 ? inputs : float4(0.f));
      outputs = aa_filter_.ProcessDown(CoreProcess(up, timestep));
    }
    return { outputs[0], outputs[1], outputs[2], outputs[3] };
  }

 private:
  // inputs: (input, v_oct, i_reso, i_vca); returns (bp2, lp2, lp4, lp4vca).
  float4 CoreProcess(float4 inputs, float timestep) {
    rc_filters_.process(inputs);
    const float4 control = rc_filters_.lowpass();
    const float v_oct = control[1];
    const float i_reso = control[2];
    const float i_vca = control[3];
    const float feedforward = rc_filters_.highpass()[0];
    const float input = inputs[0];

    // dvout/dt = -A/(RC) * (vin + vout) for each VCA-integrator cell.
    const float rad_per_s = -std::exp2(v_oct) / kFilterCellRC;
    auto derivative = [&](float4 vout) {
      // vin = (in, v0, v1, v2): each cell is fed by the previous one.
      const float vp = feedforward * kFeedforwardGain;
      const float vn = vout[3] * kFeedbackGain;
      const float res = kFilterCellR * OTAVCA(vp, vn, i_reso);
      float4 vin = __builtin_shufflevector(vout, vout, 3, 0, 1, 2);
      vin[0] = input * kFilterInputGain + res;
      const float4 vsum = vin + vout;
      float4 dvout = rad_per_s * vsum;
      // Even-order harmonics via self-modulation.
      dvout *= 1.f + vsum * kFilterCellSelfModulation;
      return dvout;
    };
    // Second-order Runge-Kutta.
    const float4 k1 = derivative(cell_voltage_);
    const float4 k2 = derivative(cell_voltage_ + k1 * (timestep / 2.f));
    cell_voltage_ = cell_voltage_ + timestep * k2;
    for (int i = 0; i < 4; ++i) {
      cell_voltage_[i] = std::clamp(static_cast<float>(cell_voltage_[i]), -kOpampSatV, kOpampSatV);
    }

    const float lp1 = cell_voltage_[0];
    float lp2 = cell_voltage_[1];
    float lp4 = cell_voltage_[3];
    const float bp2 = (lp1 + lp2) * kBP2Gain;
    vca_hpf_.process(lp4);
    float lp4vca = vca_hpf_.highpass();
    lp4vca = -kVCAOutputR * OTAVCA(0.f, lp4vca * kVCAInputGain, i_vca);
    lp2 *= kLP2Gain;
    lp4 *= kLP4Gain;
    return float4{ bp2, lp2, lp4, lp4vca };
  }

  // Ripples' nonlinear CV voltage-to-current converters.
  static float VtoIConverter(float rfb, float vc, float rc,
                             float vp = 0.f, float rp = 1e12f) {
    const float vnom = -(vc * rfb / rc + vp * rfb / rp);
    const float vout = std::max(vnom, kVtoICollectorVSat);
    const float nrc = rp * rfb;
    const float nrp = rc * rfb;
    const float nrfb = rc * rp;
    const float vneg = (vc * nrc + vp * nrp + vout * nrfb) / (nrc + nrp + nrfb);
    const float iout = (vneg - vout) / rfb;
    return std::max(iout, 0.f);
  }

  // LM13700 OTA VCA, neglecting linearizing diodes:
  // i_out = i_abc * tanh(vi / (2 vt)), with a Pade approximant of tanh.
  static float OTAVCA(float vp, float vn, float i_abc) {
    const float kTemperature = 40.f;
    const float kKoverQ = 8.617333262145e-5f;
    const float kKelvin = 273.15f;
    const float kVt = kKoverQ * (kTemperature + kKelvin);
    const float kZlim = 2.f * std::sqrt(3.f);
    const float vi = vp - vn;
    const float z = std::clamp(vi / (2 * kVt), -kZlim, kZlim);
    const float z2 = z * z;
    const float q = 12.f + z2;
    const float p = 12.f * z * q / (36.f * z2 + q * q);
    return i_abc * p;
  }

  float sample_time_ = 1.f / 48000.f;
  float4 cell_voltage_ = 0.f;
  AAFilter<float4> aa_filter_;
  TRCFilter<float4> rc_filters_;
  TRCFilter<float> vca_hpf_;
  bool noise_ = true;
  uint32_t noise_state_ = 0x12345678u;
};

}  // namespace ripples

#endif  // AMBIKA_PLUGIN_RIPPLES_CORE_H_
