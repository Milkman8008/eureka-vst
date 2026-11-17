// Editable parameters of the synth, with the ranges and value names of the
// Ambika controller UI (controller/parameter.cc and controller/resources.cc).
//
// This table is the single source of truth: the engine clamps incoming patch
// bytes with it, and the plug-in builds its host parameters from it.

#ifndef AMBIKA_PLUGIN_PARAMETERS_H_
#define AMBIKA_PLUGIN_PARAMETERS_H_

#include <stdint.h>

#include "common/patch.h"
#include "part.h"

namespace ambika {
namespace plugin {

// Patch padding bytes used by the plug-in (the voicecard ignores them).
static const uint8_t kPatchFilterModel = 104;    // 0 = Ambika SVF, 1.. Surge models
static const uint8_t kPatchFilterVariant = 105;  // Surge filter subtype
static const int kMaxFilterVariants = 15;        // Largest Surge subtype count

enum ParameterTarget {
  TARGET_PATCH,  // address = byte offset into struct Patch
  TARGET_PART,   // address = PartField
};

enum PartField {
  PART_VOLUME,
  PART_OCTAVE,
  PART_TUNING,
  PART_SPREAD,
  PART_LEGATO,
  PART_PORTAMENTO,
  PART_POLYPHONY_MODE,
  PART_PAN,
};

struct ParameterInfo {
  const char* id;     // Stable identifier, used in saved host state.
  const char* name;   // Display name.
  const char* group;  // Section of the UI.
  ParameterTarget target;
  uint8_t address;
  int16_t min_value;  // Signed parameters store two's complement bytes.
  int16_t max_value;
  int16_t default_value;
  // Value names for enumerated parameters (max - min + 1 entries), or null.
  const char* const* choices;
};

int NumParameters();
const ParameterInfo& GetParameter(int index);

// Clamps a patch byte to the range of the parameter stored at that address.
// Bytes that no parameter uses (padding, the unused filter 2) are passed
// through unchanged; the voicecard never reads them.
uint8_t ClampPatchByte(uint8_t address, uint8_t value);

// The controller's default patch (init_patch in controller/part.cc).
const Patch& DefaultPatch();

// Reads/writes a part setting by field.
int GetPartField(const PartSettings& settings, uint8_t field);
void SetPartField(PartSettings* settings, uint8_t field, int value);

}  // namespace plugin
}  // namespace ambika

#endif  // AMBIKA_PLUGIN_PARAMETERS_H_
