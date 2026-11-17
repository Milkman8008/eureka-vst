#include "avrlib/random.h"

namespace avrlib {

// Same seed as avrlib/random.cc.
thread_local uint16_t Random::rng_state_ = 0x21;

}  // namespace avrlib
