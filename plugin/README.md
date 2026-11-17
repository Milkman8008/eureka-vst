# Ambika firmware as a plug-in

A VST3 / AU / standalone instrument that runs the Mutable Instruments Ambika
voicecard firmware natively, with a model of the analog filter and VCA.

The plug-in is called **eureka!** (`AMBIKA_PLUGIN_NAME` in `CMakeLists.txt`). Emilie
Gillet has asked that derivatives not use the original product names, so the
Ambika name is kept only in credits.

## Building (macOS)

Requires CMake 3.22+ and the Xcode command line tools.

```sh
git submodule update --init   # avrlib, tools, JUCE, sst-filters, sst-basic-blocks, simde
cmake -S plugin -B plugin/build/release -DCMAKE_BUILD_TYPE=Release
cmake --build plugin/build/release -j8
(cd plugin/build/release && ctest --output-on-failure)
```

Each bundle is re-signed (ad-hoc) as the last build step: JUCE adds the
VST3's `moduleinfo.json` after signing, and hosts that check signatures, such
as Ableton Live, reject a bundle whose seal is broken. Set
`AMBIKA_CODESIGN_IDENTITY` to a Developer ID to sign for distribution.

The built plug-ins are in `plugin/build/release/AmbikaPlugin_artefacts/Release/`
(`VST3/`, `AU/`, `Standalone/`). They are not installed automatically; copy the
`.vst3` to `~/Library/Audio/Plug-Ins/VST3/` or the `.component` to
`~/Library/Audio/Plug-Ins/Components/` to use them in a DAW.

To build and test only the engine (no JUCE):
`cmake -S plugin -B plugin/build/engine -DAMBIKA_BUILD_PLUGIN=OFF`.

## How it works

The upstream firmware is compiled as-is wherever possible; nothing outside
`plugin/` is modified.

| Piece | Source |
| --- | --- |
| Oscillators, wavetables, envelopes, LFO class | `voicecard/oscillator.cc`, `voicecard/resources.cc`, `voicecard/envelope.h`, `common/lfo.h` (unchanged) |
| Voice allocator, note stack | `controller/voice_allocator.cc`, `controller/note_stack.h` (unchanged) |
| Voice engine | `engine/voice_card.*`: `voicecard/voice.cc` with its static singletons turned into members (six voices per instance, several instances per process) |
| Part (notes, voicing modes, LFO 1-3, sync) | `engine/part.*`: the voice-driving subset of `controller/part.cc` |
| Parameter names and ranges | `engine/parameters.*`, from `controller/parameter.cc` |
| DAC, filter, VCA | `engine/analog.*` (new; the hardware is analog here) |
| Sample rate conversion | `engine/resampler.*` (new) |

Desktop compatibility comes from a few build-time shims rather than edits:

- `engine/shim/avr/pgmspace.h`: `PROGMEM` and `pgm_read_*` as plain reads.
- `engine/shim/avrlib/random.h`: the LFSR with `thread_local` state.
- `avrlib/op.h` is copied into the build tree with `USE_OPTIMIZED_OP` (AVR
  assembly) turned off, and with a fix for the portable `U24Sub`, which was
  never compiled upstream and referenced an undefined variable.
- The controller's LFO wavetables and clock tables are extracted from
  `controller/resources.cc` and `controller/part.cc` at configure time.

Signal path per voice, as on the hardware: the voicecard renders 40 8-bit
samples per control period at 20 MHz / 510 (about 39.2 kHz). Each sample is
held for `crush` samples like the DAC, then goes through a 2-pole
state-variable filter model and a VCA at 2x oversampling. The six voices are
mixed and resampled to the host rate with a windowed-sinc filter. Cutoff
tracks at 2 CV steps per semitone, matching the firmware's note tracking.

## Parts and voice cards

There are six parts, each with its own patch (and preset), and six voice
cards. On the **Multi** page each card chooses the part it plays; cards on
the same part share its patch, a card set to off is silent. Every part
receives every MIDI channel. Choose the part to edit with the coloured
**Edit part** buttons at the top, or a card's **Edit part** button.

By default all six cards play part 1, which is how earlier builds behaved.
Part 1 keeps its original parameter IDs, so projects saved with earlier
builds load unchanged; parts 2-6 use IDs prefixed `p2_` ... `p6_`.

**Keyboard splits** (Multi page) give each part a lowest and highest key; a
part only plays notes inside its range, in every voice mode (a low key above
the high key plays outside the range). In Cyclic and Poly, a note only goes
to cards whose part covers it.

**Voice mode** decides which cards play a note:

- **Per part** (as on the hardware): every part plays every note on its own
  cards, using its Voicing on the Synth page (poly, mono, 2x unison, cyclic).
  Parts that own cards layer.
- **Cyclic**: notes step through cards 1, 2 ... 6 in order, whatever part each
  card plays, so successive notes can cycle through different patches.
- **Poly**: each note takes the free card that has rested longest, stealing
  the oldest note when all six are busy.

**Unison** (Cyclic and Poly) plays 1-6 cards per note: with 2, notes take
cards 1+2, then 3+4, then 5+6; with 3, cards 1-3 then 4-6. Unison voices are
detuned by each part's Spread. The sustain pedal holds notes in all modes.

## Filters

Each part chooses its filter on the Synth page:

- **Ambika SVF**: a model of the 2-pole SVF voicecard (LP / BP / HP).
- **Surge filters** (from Surge XT's sst-filters), in menu order: MULTI
  Xpander, LP Diode Ladder, LP Legacy Ladder, LP Vintage Ladder, FX Comb+,
  FX Comb-, FX Sample and Hold, FX Cutoff Warp AP. **Variant** selects the
  model's subtype (slope, mode, saturation...). The list lives in
  `engine/surge_filter.cc` (`kModels`); sst-filters has more types if wanted.

The firmware's cutoff, resonance, envelope 2 and LFO 2 amounts, key tracking
and modulation matrix drive whichever filter is selected. The model and
variant are stored in two padding bytes of the patch that the voicecard
firmware never reads, so presets and projects from earlier builds load with
the Ambika SVF.

The Surge libraries are submodules in `plugin/third_party`, pinned to the
commits Surge XT uses: `sst-filters`, `sst-basic-blocks` and `simde` (SSE on
Apple Silicon). They require C++20, which the whole plug-in builds with.

## Pan

Each part has a **Pan** control (Voice panel on the Synth page). The
modulation matrix has an extra destination, **pan**, so any source can move a
voice in the stereo field: LFOs for auto-pan, velocity, note number for
keyboard spread, random for a per-note scatter. Panning is per voice; the
output is stereo.

## Light and dark mode

The sun/moon button at the right of the header switches between dark and
light mode. The choice is a user preference (not saved in projects), stored
in `~/Library/Application Support/eureka/eureka.settings`.

## Skins

The look can be changed with image files, no code needed. Generate the
template pack (already in `plugin/skin-template/`, see its README.txt):

```sh
plugin/build/release/ambika_editor_snapshot_artefacts/Release/ambika_editor_snapshot \
    --skin-template "$PWD/plugin/skin-template"
```

Put edited files in `Documents/eureka/skin/` and reopen the plug-in window to
see them (set `EUREKA_SKIN_DIR` to use another folder). When the skin is
done, copy the files into `plugin/skin/` and rebuild: they are then compiled
into the plug-in, so everyone gets the same look. Supported files:
`background.png` (or per page `background-synth/mod/multi/presets.png`),
`logo.png`, `knob.png` (filmstrip) and `skin.json` (panel/header/display
opacity, knob value ring, the six part colours).

## Presets

A preset is the sound of one part: the whole patch plus the part's voicing,
volume, octave, tune, spread, legato and glide. The multi setup (channels,
key ranges, voice assignment) is not part of a preset; the host project
saves it.

The bar under the header shows the edited part's preset; `*` means it has
been changed since it was loaded or saved.

- **< / >** load the previous / next preset into the edited part.
- **Save** writes over the current preset (or asks for a name if there is none).
- **Save as...** names and tags a new preset (suggested tags plus your own),
  with optional author and comment. Saving over an existing name asks first.
- **Init** (click twice) resets the edited part to the initial patch.
- **Browse** opens the browser: search, filter by tags, double-click to load,
  rename, delete (to the Trash), and open the presets folder.

Presets are JSON files (`.eurekapreset`) in `Documents/eureka/presets`
(created the first time you save, browse or open the folder); sub-folders
are included. They store
values by parameter ID, so a preset missing newer parameters loads them at
their defaults. Each part's preset name is saved with the host project.

## Not in this version

- Arpeggiator and step sequencer (the Seq. 1/2 and Arp modulation sources stay
  at 0), ragas/microtuning, CC-to-parameter mapping and NRPN.
- The two 4-pole Ambika filter cards are not modelled (the Ambika SVF and the
  Surge filters are available instead).
- One stereo output; no separate outputs per part or voice.

## License

GPL-3.0, like the Ambika firmware. The Surge XT filter libraries
(sst-filters, sst-basic-blocks, by the Surge Synth Team) are GPL-3.0 as well;
simde is MIT. JUCE 8 is used under its AGPLv3 option, which GPLv3 allows
combining with.
