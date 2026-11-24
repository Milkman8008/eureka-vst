// Tables taken from the controller firmware. The build extracts them from
// controller/part.cc into controller_tables.cc (see plugin/CMakeLists.txt),
// so the values always match the upstream source.

#ifndef AMBIKA_PLUGIN_CONTROLLER_TABLES_H_
#define AMBIKA_PLUGIN_CONTROLLER_TABLES_H_

#include <avr/pgmspace.h>

namespace ambika {
namespace plugin {

// Indexed by synced LFO rate (0..kNumSyncedLfoRates-1).
extern const prog_uint8_t midi_clock_tick_per_step[15];
extern const prog_uint16_t lfo_phase_increment_per_clock_tick[15];

// Arpeggiator rhythms (controller/resources.cc): bit n set = the arpeggiator
// plays on step n of 16.
static const uint8_t kNumArpeggiatorPatterns = 22;
extern const prog_uint16_t arpeggiator_patterns[kNumArpeggiatorPatterns];

}  // namespace plugin
}  // namespace ambika

#endif  // AMBIKA_PLUGIN_CONTROLLER_TABLES_H_
