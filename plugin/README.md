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

## Building (Windows)

Visual Studio 2022 with the "C++ Clang tools for Windows" component, and
CMake. The firmware needs GCC/Clang extensions, so the build uses clang-cl
(the `ClangCL` toolset); configuring with MSVC's own compiler stops with an
error that says so.

```bat
cmake -S plugin -B build -A x64 -T ClangCL
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

Windows builds VST3 and the standalone app (no AU). GitHub Actions does the
same on every push (`.github/workflows/build.yml`) and uploads
`eureka-windows` as a build artifact; a `v*` tag also attaches
`eureka-windows.zip` to that tag's release. Install notes for users are in
`plugin/packaging/INSTALL-Windows.txt`.

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
| Part (notes, voicing modes, LFO 1-3, sync, arpeggiator, sequencer) | `engine/part.*`: the voice-driving subset of `controller/part.cc` |
| Parameter names and ranges | `engine/parameters.*`, from `controller/parameter.cc` |
| DAC, filter, VCA | `engine/analog.*` (new; the hardware is analog here) |
| Ripples filter | `engine/ripples/`: Tyler Coy's Ripples circuit model from VCV Rack's AudibleInstruments (`src/Ripples`, commit 597d8a6), Rack types replaced; `engine/ripples_filter.*` wraps it |
| Sample rate conversion | `engine/resampler.*` (new) |

Desktop compatibility comes from a few build-time shims rather than edits:

- `engine/shim/avr/pgmspace.h`: `PROGMEM` and `pgm_read_*` as plain reads.
- `engine/shim/avrlib/random.h`: the LFSR with `thread_local` state.
- `avrlib/op.h` is copied into the build tree with `USE_OPTIMIZED_OP` (AVR
  assembly) turned off, and with a fix for the portable `U24Sub`, which was
  never compiled upstream and referenced an undefined variable.
- The controller's LFO wavetables, clock tables and arpeggiator rhythms are
  extracted from `controller/resources.cc` and `controller/part.cc` at
  configure time.

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
- **Poly** (the default): each note takes the free card that has rested longest, stealing
  the oldest note when all six are busy.

**Unison** (Cyclic and Poly) plays 1-6 cards per note: with 2, notes take
cards 1+2, then 3+4, then 5+6; with 3, cards 1-3 then 4-6. Unison voices are
detuned by each part's Spread. The sustain pedal holds notes in all modes.

## Filters

Each part chooses its filter on the Synth page:

- **Ambika SVF**: a model of the 2-pole SVF voicecard (LP / BP / HP). The
  Variant menu shows these for the SVF but writes `filter_mode` (the Ambika's
  own patch byte), not `filter_variant`; there is no separate Mode menu.
- **Surge filters** (from Surge XT's sst-filters), in menu order: MULTI
  Xpander, LP Diode Ladder, LP Legacy Ladder, LP Vintage Ladder, FX Comb+,
  FX Comb-, FX Sample and Hold, FX Cutoff Warp AP. **Variant** selects the
  model's subtype (slope, mode, saturation...). The list lives in
  `engine/surge_filter.cc` (`kModels`); sst-filters has more types if wanted.

- **Ripples** (model 9, after the Surge filters so saved model numbers keep
  their meaning): Tyler Coy's circuit model of the Mutable Instruments
  Ripples (four SSM2164 VCA-integrator cells, LM13700 resonance OTA, op-amp
  saturation, solved with RK2 at about 235 kHz behind its own elliptic
  anti-aliasing filters). **Variant** selects the output: LP4, LP2 or BP2.
  The voice's +/-1 level is treated as +/-5 V. The filter display uses the
  circuit's small-signal response (`RipplesEngine::LinearResponse`), since a
  self-oscillating filter has no useful impulse response; a test checks it
  against the simulation (within 1 dB). Six voices cost about 11% of one
  core on an Apple M-series machine.

The model list is `engine/filter_models.*`. Ripples is the default for new
patches (`DefaultPatch()`, so also Init and new instances), and the editor's
Model menu lists Ripples, the Ambika SVF, then the Surge filters
(`FilterModelMenuOrder()`); the stored model numbers and the host
parameter's choice order are unchanged. Content without a stored model keeps
the Ambika SVF: Ambika patches (their padding byte is 0), presets without a
`filter_model` value, and projects whose state has no `filter_model`
parameter (`setStateInformation` adds it as 0).

The firmware's cutoff, resonance, envelope 2 and LFO 2 amounts, key tracking
and modulation matrix drive whichever filter is selected. The model and
variant are stored in two padding bytes of the patch that the voicecard
firmware never reads, so presets and projects from earlier builds load with
the Ambika SVF.

The Surge libraries are submodules in `plugin/third_party`, pinned to the
commits Surge XT uses: `sst-filters`, `sst-basic-blocks` and `simde` (SSE on
Apple Silicon). They require C++20, which the whole plug-in builds with.

## Arpeggiator and sequencer

`Part` runs the controller's arpeggiator, two modulation sequences and note
pattern (`ClockSequencer`, `ClockArpeggiator`, `StepArpeggio` in
`controller/part.cc`). The settings are part parameters (Seq page; IDs
`arp_*`, `seq1_*`, `seq2_*`, `pattern_*`), so presets, projects, undo and
automation include them. Differences from the hardware:

- Sequences have 16 steps each. The hardware allows lengths up to 32, but a
  longer sequence reads the next one's memory; imported lengths are clamped.
- Clock: the engine counts MIDI clock ticks (24 per quarter note) from the
  host tempo. While the host plays, `Engine::SetTransport` places the
  sequencers at the song position (`Part::Start(tick)` derives the step of
  each sequence and of the arpeggiator rhythm) and relocates them after a
  jump or loop of more than one tick. While it is stopped the clock runs
  free, and a part restarts its sequencers on the first key pressed.
- After a (re)start the arpeggio begins on its first note; the firmware steps
  before playing, so it began on the second.
- In the Cyclic and Poly voice modes the engine, not the part, chooses voice
  cards: an arpeggiating part receives the keys, and the notes it generates
  come back through `PartNoteSink` to the engine's allocator, limited to the
  part's own cards (tracked in `note_voices_` under slot 16 + part).
- Changing a part's arp/seq mode stops its notes, so no key is left without
  its note-off.

Seq. 1, seq. 2 and arp feed the modulation matrix in every mode, as on the
hardware. Ambika programs and multis import their sequences and arpeggiator
settings; a `.SEQ` file (PartData bytes 8-79) loads into the edited part
without changing its sound or mode.

## Separate outputs

Besides the main stereo output there are six optional stereo buses, "Voice
1" to "Voice 6" (VST3 and AU; the standalone app has the main output only).
Each voice card has an **Output** parameter (`multi_voiceN_output`: main /
own output). A voice set to its own output whose bus the host has enabled is
rendered without the main mix into its own pair of resamplers, which are
synchronised with the main ones (`Resampler::SyncTo`) when the voice joins,
so all outputs stay sample-aligned; output gain applies to every bus. While
the host has not enabled the bus, the voice stays in the main mix (the
Multi page says so). Pan still works within a voice's own stereo pair.

## Window size

The editor is drawn at 1200 x 802 and scaled. It fits the window in both
dimensions and centres the content, so hosts that resize without asking the
plug-in's constrainer (several on Windows) no longer cut off the bottom.
The scale is saved in the project (`editorScale`) and the last scale chosen
is a user preference (`windowScale` in the settings file). Right-clicking
the window background offers fixed sizes, 60% to 200%.

## YAM oscillators

Seven oscillators from **YAM** ("Yet Another Mutation", the alternative
Ambika voicecard firmware by bjoeri) follow the stock waveform list, so
existing patches are unchanged: `yam_square`, `yam_saw`, `yam_triangle`,
`yam_qpwm`, `yam_fmfb`, `yam_csaw`, `yam_vowel2` (patch waveform numbers
38-44). They are a port of YAM's `voicecard/oscillator.cc` in
`engine/yam_oscillator.*`, kept close to the original 8-bit code; each
voice runs them in a separate `YamOscillator` only when a YAM shape is
selected. YAM's `oldsaw` is left out (it is the stock saw).

Ambika files written by the YAM firmware use different waveform numbers for
saw/square (PolyBLEP versions in the stock slots) and for YAM's extra
shapes, so they import with the stock oscillators in those slots.

## Pan

Each part has a **Pan** control (Voice panel on the Synth page). The
modulation matrix has an extra destination, **pan**, so any source can move a
voice in the stereo field: LFOs for auto-pan, velocity, note number for
keyboard spread, random for a per-note scatter. Panning is per voice; the
output is stereo.

## Font and logo

The UI uses the **Bageo** typeface (160 Studio) and the eureka! logo, both
compiled in from `plugin/resources/` (`fonts/Bageo.ttf`,
`images/eureka-logo.jpg`; the logo is inverted in light mode). Before
distributing builds, check that the Bageo licence allows embedding it in
software, and that the logo artwork may be used commercially.

## Light and dark mode

The sun/moon button at the right of the header switches between dark and
light mode. The choice is a user preference (not saved in projects), stored
in `~/Library/Application Support/eureka/eureka.settings`.

The colour button next to it opens a pop-up that changes the highlight
colour (curves, knob arcs, part buttons): a hue/saturation wheel with a
brightness slider for the edited part (or all parts with **All parts**), and
four random palettes (one colour per part, from a random scheme: spread,
neighbours, three groups or two opposites) that colour all six parts; with
**Shuffle** for new ones. **Reset** goes back to the theme's colours. Highlights are saved in the same settings file and win over
a skin's `partColours`.

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
- **Save multi...** saves a multi (below) with the same form.
- **Init** opens a menu: reset the edited part's sound, or **everything**:
  all six parts plus the voice routing (voice cards back to part 1, voice
  mode Poly, unison 1, full keyboard ranges).
- **Browse** opens the browser: search, filter by tags, double-click to load,
  rename, delete (to the Trash), and open the presets folder.

Presets are JSON files (`.eurekapreset`) in `Documents/eureka/presets`
(created the first time you save, browse or open the folder); sub-folders
are included. They store
values by parameter ID, so a preset missing newer parameters loads them at
their defaults. Each part's preset name is saved with the host project.

A multi (`.eurekamulti`, format `eureka-multi`, in `presets/Multis`) holds
all six parts plus the multi parameters (`multi_voice*_part`,
`multi_voice*_output`, `multi_part*_key_low/high`, `multi_voice_mode`,
`multi_unison`). Each part entry has its engine values by parameter ID plus
its preset name, tags, author, comment and file. The file is left empty for
a part that differed from its preset when the multi was saved; saving also
unlinks that part in the session, so **Save** cannot overwrite the original
preset with the multi's version. `PresetManager::load` loads a multi
whatever the part; `scan` lists multis (`PresetInfo::multi`, `partNames`,
tag "Multi" added on read); `neighbour` skips them. The loaded multi's name
is saved with the project.

## Ambika presets

The 128 factory programs of the Ambika (`controller/data/programs`) are built
in and appear in the browser tagged **Factory** (read only: load one and
**Save as...** to keep a changed copy).

**Import Ambika files...** in the browser reads the Ambika's own formats:
SD-card files (`.PAT` patch, `.PRO` program, `.MUL` multi, `.SEQ` sequence) and SysEx dumps
(`.syx`). It can either load one file into the edited part (a `.MUL` loads all
six parts with their keyboard splits and voice assignment), or add files and
whole folders to the library as eureka! presets in `presets/Imported`
(multis become one preset per part). The patch maps one to one; from the part
settings, volume, octave, tuning, spread, legato, portamento and voicing are
used (chain mode becomes poly). Sequences and arpeggiator settings are
imported too (sequence lengths above 16 are clamped); ragas are ignored.
`.SEQ` files load into the edited part only (they are not sounds, so folder
imports skip them).

## Not in this version

- Ragas/microtuning, CC-to-parameter mapping and NRPN.
- The two 4-pole Ambika filter cards are not modelled (the Ambika SVF and the
  Surge filters are available instead).

## License

GPL-3.0, like the Ambika firmware. The Surge XT filter libraries
(sst-filters, sst-basic-blocks, by the Surge Synth Team) and the Ripples model
(Tyler Coy) are GPL-3.0 as well;
simde is MIT. JUCE 8 is used under its AGPLv3 option, which GPLv3 allows
combining with.
