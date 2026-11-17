// Replacement for avrlib/random.h in the plug-in build.
//
// Same Galois LFSR as the original, but the state is thread_local instead of a
// single global: a DAW can run several plug-in instances on different audio
// threads, and they must not race on one shared variable. The shim directory
// comes before the repository root on the include path, so every
// #include "avrlib/random.h" in the upstream sources resolves here.

#ifndef AVRLIB_RANDOM_H_
#define AVRLIB_RANDOM_H_

#include "avrlib/base.h"

namespace avrlib {

class Random {
 public:
  static void Update() {
    // Galois LFSR with feedback polynomial = x^16 + x^14 + x^13 + x^11.
    // Period: 65535.
    rng_state_ = (rng_state_ >> 1) ^ (-(rng_state_ & 1) & 0xb400);
  }

  static inline uint16_t state() { return rng_state_; }

  static inline void Seed(uint16_t seed) { rng_state_ = seed; }

  static inline uint8_t state_msb() {
    return static_cast<uint8_t>(rng_state_ >> 8);
  }

  static inline uint8_t GetByte() {
    Update();
    return state_msb();
  }

  static inline uint16_t GetWord() {
    Update();
    return state();
  }

 private:
  static thread_local uint16_t rng_state_;

  DISALLOW_COPY_AND_ASSIGN(Random);
};

}  // namespace avrlib

#endif  // AVRLIB_RANDOM_H_
