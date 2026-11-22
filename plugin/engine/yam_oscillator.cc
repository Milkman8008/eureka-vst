// Port of the YAM oscillators (YAM firmware by bjoeri, on Emilie Gillet's
// Ambika firmware; source: github.com/barnabywalters/ambika-yam,
// voicecard/oscillator.cc and resources.cc). GPL-3.0. See yam_oscillator.h.

#include "yam_oscillator.h"

#include "avrlib/random.h"
#include "voicecard/resources.h"
#include "voicecard/voicecard.h"

namespace ambika {
namespace plugin {

namespace {

// YAM's resources.cc: 1/x for the blep position, and the blep itself.
const uint8_t kDivisionTable[] = {
     255,    254,    252,    250,    248,    246,    244,    242,
     240,    239,    237,    235,    234,    232,    230,    229,
     227,    225,    224,    222,    221,    219,    218,    217,
     215,    214,    212,    211,    210,    208,    207,    206,
     204,    203,    202,    201,    199,    198,    197,    196,
     195,    193,    192,    191,    190,    189,    188,    187,
     186,    185,    184,    183,    182,    181,    180,    179,
     178,    177,    176,    175,    174,    173,    172,    171,
     170,    169,    168,    168,    167,    166,    165,    164,
     163,    163,    162,    161,    160,    159,    159,    158,
     157,    156,    156,    155,    154,    153,    153,    152,
     151,    151,    150,    149,    148,    148,    147,    146,
     146,    145,    144,    144,    143,    143,    142,    141,
     141,    140,    140,    139,    138,    138,    137,    137,
     136,    135,    135,    134,    134,    133,    133,    132,
     132,    131,    131,    130,    130,    129,    129,    128,
};

const uint8_t kBlepTable[] = {
       0,      0,      0,      0,      0,      0,      0,      0,
       1,      1,      1,      1,      1,      1,      2,      2,
       2,      2,      3,      3,      3,      3,      4,      4,
       5,      5,      5,      6,      6,      7,      7,      8,
       8,      9,      9,     10,     10,     11,     11,     12,
      13,     13,     14,     15,     15,     16,     17,     17,
      18,     19,     20,     20,     21,     22,     23,     24,
      25,     26,     26,     27,     28,     29,     30,     31,
      32,     33,     34,     35,     36,     37,     39,     40,
      41,     42,     43,     44,     45,     47,     48,     49,
      50,     52,     53,     54,     56,     57,     58,     60,
      61,     62,     64,     65,     67,     68,     70,     71,
      73,     74,     76,     77,     79,     80,     82,     84,
      85,     87,     88,     90,     92,     94,     95,     97,
      99,    101,    102,    104,    106,    108,    110,    112,
     113,    115,    117,    119,    121,    123,    125,    127,
};

inline uint8_t Blep(uint16_t index) {
  return kBlepTable[index & 127];
}

}  // namespace

// The sample loop macros of voicecard/oscillator.cc.
#define UPDATE_PHASE \
  if (*sync_input_++) { \
    phase.integral = 0; \
    phase.fractional = 0; \
  } \
  phase = U24AddC(phase, phase_increment_int); \
  *sync_output_++ = phase.carry; \

#define BEGIN_SAMPLE_LOOP \
  uint24c_t phase; \
  uint24_t phase_increment_int; \
  phase_increment_int.integral = phase_increment_.integral; \
  phase_increment_int.fractional = phase_increment_.fractional; \
  phase.integral = phase_.integral; \
  phase.fractional = phase_.fractional; \
  uint8_t size = kAudioBlockSize; \
  while (size--) {

#define END_SAMPLE_LOOP \
  } \
  phase_.integral = phase.integral; \
  phase_.fractional = phase.fractional;

// 1 / increment, as a mantissa (128..255 -> quotient) and a shift count.
#define CALCULATE_DIVISION_FACTOR(divisor, result_quotient, result_quotient_shifts) \
  uint16_t div_table_index = divisor; \
  int8_t result_quotient_shifts = 0; \
  while (div_table_index > 255) { \
    div_table_index >>= 1; \
    --result_quotient_shifts; \
  } \
  while (div_table_index < 128) { \
    div_table_index <<= 1; \
    ++result_quotient_shifts; \
  } \
  div_table_index -= 128; \
  uint8_t result_quotient = kDivisionTable[div_table_index];

// Position of the discontinuity within the sample, as a blep table index.
#define CALCULATE_BLEP_INDEX(increment, quotient, quotient_shifts, result_blep_index) \
  uint16_t result_blep_index = increment; \
  int8_t shifts = quotient_shifts; \
  while (shifts < 0) { \
    result_blep_index >>= 1; \
    ++shifts; \
  } \
  while (shifts > 0) { \
    result_blep_index <<= 1; \
    --shifts; \
  } \
  result_blep_index = U16U8MulShift8(result_blep_index, quotient);

void YamOscillator::Render(uint8_t shape, uint8_t note, uint24_t increment,
                           uint8_t* sync_input, uint8_t* sync_output,
                           uint8_t* buffer) {
  shape_ = shape;
  note_ = note;
  phase_increment_ = increment;
  sync_input_ = sync_input;
  sync_output_ = sync_output;
  switch (shape - kFirstYamWaveform) {
    case YAM_SQUARE: RenderPolyBlepPwm(buffer); break;
    case YAM_SAW: RenderPolyBlepSaw(buffer); break;
    case YAM_TRIANGLE: RenderNewTriangle(buffer); break;
    case YAM_QUAD_PWM: RenderQuadPwm(buffer); break;
    case YAM_FM_FB: RenderFmFeedback(buffer); break;
    case YAM_CSAW: RenderPolyBlepCSaw(buffer); break;
    case YAM_VOWEL_2: RenderVowel2(buffer); break;
    default:
      for (uint8_t i = 0; i < kAudioBlockSize; ++i) {
        buffer[i] = 128;
        sync_output[i] = 0;
      }
      break;
  }
}

// ------- Polyblep Pwm ------------------------------------------------------
void YamOscillator::RenderPolyBlepPwm(uint8_t* buffer) {
  // calculate (1/increment) for later multiplication with current phase
  CALCULATE_DIVISION_FACTOR(phase_increment_.integral, quotient, quotient_shifts)

  // Revert to pure saw (=single blep) to avoid cpu overload for high notes
  uint8_t revert_to_saw = note_ > 107;

  // PWM modulation (constrained to extend over at least one increment)
  uint8_t pwm_limit = 127 - (phase_increment_.integral >> 8);
  uint16_t pwm_phase =
    (parameter_ < pwm_limit) ? /* prevent dual bleps at same increment */
    static_cast<uint16_t>(static_cast<uint16_t>(127 + parameter_) << 8) :
    static_cast<uint16_t>(static_cast<uint16_t>(127 + pwm_limit) << 8);
  uint8_t high = phase_.integral >= pwm_phase;

  uint8_t next_sample = data_.output_sample;
  BEGIN_SAMPLE_LOOP
    UPDATE_PHASE
    uint8_t this_sample = next_sample;

    // Compute naive waveform
    next_sample = revert_to_saw ?
      (phase.integral >> 8) : (phase.integral < pwm_phase ? 0 : 255);

    if (phase.carry) {
      high = false;
      CALCULATE_BLEP_INDEX(phase.integral, quotient, quotient_shifts, blep_index)
      this_sample -= Blep(blep_index);
      next_sample += Blep(127 - blep_index);
    }
    else if (!revert_to_saw && /* no positive edge for pure saw */
      phase.integral >= pwm_phase && !high) {
      high = true;
      CALCULATE_BLEP_INDEX(phase.integral - pwm_phase, quotient, quotient_shifts, blep_index)
      this_sample += Blep(blep_index);
      next_sample -= Blep(127 - blep_index);
    }

    *buffer++ = this_sample;
  END_SAMPLE_LOOP

  data_.output_sample = next_sample;
}

// ------- Polyblep Saw ------------------------------------------------------
void YamOscillator::RenderPolyBlepSaw(uint8_t* buffer) {
  CALCULATE_DIVISION_FACTOR(phase_increment_.integral, quotient, quotient_shifts)

  // Revert to pure saw (=single blep) to avoid cpu overload for high notes
  uint8_t mod_parameter = note_ > 107 ? 0 : parameter_;
  uint8_t high = phase_.integral >= 0x8000;

  uint8_t next_sample = data_.output_sample;
  BEGIN_SAMPLE_LOOP
    UPDATE_PHASE
    uint8_t this_sample = next_sample;

    // Compute naive waveform
    next_sample = (phase.integral < 0x8000) ?
      (phase.integral >> 8) :
      (phase.integral >> 8) - mod_parameter;

    if (phase.carry) {
      high = false;
      CALCULATE_BLEP_INDEX(phase.integral, quotient, quotient_shifts, blep_index)
      this_sample -= U8U8MulShift8(Blep(blep_index),
        255 - mod_parameter /* scale blep to size of edge */);
      next_sample += U8U8MulShift8(Blep(127 - blep_index),
        255 - mod_parameter /* scale blep to size of edge */);
    }
    else if (mod_parameter && !high && phase.integral >= 0x8000) {
      high = true;
      CALCULATE_BLEP_INDEX(phase.integral - 0x8000, quotient, quotient_shifts, blep_index)
      this_sample -= U8U8MulShift8(Blep(blep_index),
        mod_parameter /* scale blep to size of edge */);
      next_sample += U8U8MulShift8(Blep(127 - blep_index),
        mod_parameter /* scale blep to size of edge */);
    }

    *buffer++ = this_sample;
  END_SAMPLE_LOOP

  data_.output_sample = next_sample;
}

// ------- Polyblep CS-80 Saw ------------------------------------------------
void YamOscillator::RenderPolyBlepCSaw(uint8_t* buffer) {
  CALCULATE_DIVISION_FACTOR(phase_increment_.integral, quotient, quotient_shifts)

  // Revert to pure saw (=single blep) to avoid cpu overload for high notes
  uint8_t revert_to_saw = note_ > 107;

  // PWM modulation (constrained to extend over at least one increment)
  uint8_t pwm_limit = phase_increment_.integral >> 8;
  uint16_t pwm_phase =
    (parameter_ > 0 && parameter_ < pwm_limit) ?
    static_cast<uint16_t>(static_cast<uint16_t>(pwm_limit) << 8) :
    static_cast<uint16_t>(static_cast<uint16_t>(parameter_) << 8);
  uint8_t high = phase_.integral >= pwm_phase;

  uint8_t next_sample = data_.output_sample;
  BEGIN_SAMPLE_LOOP
    UPDATE_PHASE
    uint8_t this_sample = next_sample;

    // Compute naive waveform
    next_sample = (revert_to_saw || phase.integral >= pwm_phase) ?
      (phase.integral >> 8) : 0;

    if (phase.carry) {
      high = false;
      CALCULATE_BLEP_INDEX(phase.integral, quotient, quotient_shifts, blep_index)
      this_sample -= Blep(blep_index);
      next_sample += Blep(127 - blep_index);
    }
    else if (!revert_to_saw && /* no positive edge for pure saw */
      phase.integral >= pwm_phase && !high) {
      high = true;
      CALCULATE_BLEP_INDEX(phase.integral - pwm_phase, quotient, quotient_shifts, blep_index)
      this_sample += U8U8MulShift8(Blep(blep_index),
        parameter_ /* scale blep to size of edge */);
      next_sample -= U8U8MulShift8(Blep(127 - blep_index),
        parameter_ /* scale blep to size of edge */);
    }

    *buffer++ = this_sample;
  END_SAMPLE_LOOP

  data_.output_sample = next_sample;
}

// ------- New Triangle (Non-band-limited and with different waveshaping) ----
void YamOscillator::RenderNewTriangle(uint8_t* buffer) {
  BEGIN_SAMPLE_LOOP
    UPDATE_PHASE
    uint8_t tri = phase.integral >> 7;
    uint8_t v = phase.integral & 0x8000 ? tri : ~tri;
    if (v < parameter_) { // fold triangle
      v = (parameter_ << 1) - v;
    }
    *buffer++ = v;
  END_SAMPLE_LOOP
}

// ------- Quad pwm (mit aliasing) -------------------------------------------
void YamOscillator::RenderQuadPwm(uint8_t* buffer) {
  uint16_t phase_spread = (
      static_cast<uint32_t>(phase_increment_.integral) * parameter_) >> 13;
  ++phase_spread;
  uint16_t phase_increment = phase_increment_.integral;
  uint16_t increments[3];
  for (uint8_t i = 0; i < 3; ++i) {
    phase_increment += phase_spread;
    increments[i] = phase_increment;
  }

  uint8_t pwm_phase = 127 + parameter_;
  BEGIN_SAMPLE_LOOP
    UPDATE_PHASE
    data_.qs.phase[0] += increments[0];
    data_.qs.phase[1] += increments[1];
    data_.qs.phase[2] += increments[2];
    // The high bytes of the three phases (little-endian, as on the AVR).
    uint8_t value = phase.integral < (pwm_phase << 8) ? 0 : 63;
    if ((data_.qs.phase[0] >> 8) >= pwm_phase) value += 63;
    if ((data_.qs.phase[1] >> 8) >= pwm_phase) value += 63;
    if ((data_.qs.phase[2] >> 8) >= pwm_phase) value += 63;
    *buffer++ = value;
  END_SAMPLE_LOOP
}

// ------- FM with feedback --------------------------------------------------
void YamOscillator::RenderFmFeedback(uint8_t* buffer) {
  uint8_t offset = fm_parameter_ < 24 ? 0 :
    (fm_parameter_ > 48 ? 24 : fm_parameter_ - 24);
  uint16_t multiplier = ResourcesManager::Lookup<uint16_t, uint8_t>(
      lut_res_fm_frequency_ratios, offset);
  uint16_t increment = (
      static_cast<int32_t>(phase_increment_.integral) * multiplier) >> 8;
  parameter_ <<= 1;

  uint16_t phase_2 = data_.secondary_phase;
  uint8_t last_output = data_.output_sample;
  uint8_t fb_phase_mod = parameter_ < 128 ? 0 : parameter_ - 128;
  BEGIN_SAMPLE_LOOP
    UPDATE_PHASE
    phase_2 += increment;
    uint8_t modulator = ReadSample(wav_res_sine,
        static_cast<uint16_t>(phase_2 + fb_phase_mod * last_output));
    uint16_t modulation = modulator * parameter_;
    last_output = InterpolateSample(wav_res_sine,
        static_cast<uint16_t>(phase.integral + modulation));
    *buffer++ = last_output;
  END_SAMPLE_LOOP
  data_.secondary_phase = phase_2;
  data_.output_sample = last_output;
}

// ------- Vowel without phase noise -----------------------------------------
void YamOscillator::RenderVowel2(uint8_t* buffer) {
  data_.vw.update = (data_.vw.update + 1) & 3;
  if (!data_.vw.update) {
    uint8_t offset_1 = U8ShiftRight4(parameter_);
    offset_1 = U8U8Mul(offset_1, 7);
    uint8_t offset_2 = offset_1 + 7;
    uint8_t balance = parameter_ & 15;

    // Interpolate formant frequencies.
    for (uint8_t i = 0; i < 3; ++i) {
      data_.vw.formant_increment[i] = U8U4MixU12(
          ResourcesManager::Lookup<uint8_t, uint8_t>(
              wav_res_vowel_data, offset_1 + i),
          ResourcesManager::Lookup<uint8_t, uint8_t>(
              wav_res_vowel_data, offset_2 + i),
          balance);
      data_.vw.formant_increment[i] <<= 3;
    }

    // Interpolate formant amplitudes (the 4th is noise_modulation, unused).
    for (uint8_t i = 0; i < 3; ++i) {
      uint8_t amplitude_a = ResourcesManager::Lookup<uint8_t, uint8_t>(
          wav_res_vowel_data, offset_1 + 3 + i);
      uint8_t amplitude_b = ResourcesManager::Lookup<uint8_t, uint8_t>(
          wav_res_vowel_data, offset_2 + 3 + i);
      data_.vw.formant_amplitude[i] = U8U4MixU8(
          amplitude_a, amplitude_b, balance);
    }
  }

  BEGIN_SAMPLE_LOOP
    int8_t result = 0;
    uint8_t phaselet;

    data_.vw.formant_phase[0] += data_.vw.formant_increment[0];
    phaselet = (data_.vw.formant_phase[0] >> 8) & 0xf0;
    result = ResourcesManager::Lookup<uint8_t, uint8_t>(
        wav_res_formant_sine,
        phaselet | data_.vw.formant_amplitude[0]);

    data_.vw.formant_phase[1] += data_.vw.formant_increment[1];
    phaselet = (data_.vw.formant_phase[1] >> 8) & 0xf0;
    result += ResourcesManager::Lookup<uint8_t, uint8_t>(
        wav_res_formant_sine,
        phaselet | data_.vw.formant_amplitude[1]);

    data_.vw.formant_phase[2] += data_.vw.formant_increment[2];
    phaselet = (data_.vw.formant_phase[2] >> 8) & 0xf0;
    result += ResourcesManager::Lookup<uint8_t, uint8_t>(
        wav_res_formant_square,
        phaselet | data_.vw.formant_amplitude[2]);

    result = S8U8MulShift8(result, phase.integral >> 8);
    phase.integral -= phase_increment_int.integral;
    if (phase.integral < phase_increment_int.integral) {
      data_.vw.formant_phase[0] = 0;
      data_.vw.formant_phase[1] = 0;
      data_.vw.formant_phase[2] = 0;
    }
    uint8_t x = S16ClipS8(4 * result) + 128;
    *buffer++ = x;
    *buffer++ = x;
    *sync_output_++ = 0;
    *sync_output_++ = 0;
    size--;
  END_SAMPLE_LOOP
}

}  // namespace plugin
}  // namespace ambika
