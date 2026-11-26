Hey! Just wanted to first say that it's really great to have a virtual Ambika!

I was playing around with it a bit today and saw in the README that all parts currently listen to every MIDI channel—is per-part MIDI channel routing (like on the hardware) something you might consider adding in the future?

### A quick UI suggestion:
- In the "Filter" section, the "Variant" dropdown applies to Ripples and the Surge filter models, while "Ambika SVF" leaves it blank and uses a separate "Mode" dropdown (`LP` / `BP` / `HP`). Since you mentioned in `plugin/README.md` that they use separate patch bytes for backwards compatibility, would it be possible to merge them visually in the UI (so "Variant" displays `LP / BP / HP` when Ambika SVF is selected) just to streamline the panel?

### A small visual feedback note:
- When using the internal Mod Matrix to modulate a parameter, the waveform or filter response display doesn't animate/update visually (whereas controlling parameters via host automation/MIDI gives real-time visual feedback).

*(Edit: Removed my earlier notes about crossmod—I re-tested with both oscillators mixed in per the Ambika manual and confirmed `sync`, `fold`, and `bits` all work just like the hardware!)*

Thanks again for making this happen!!

**Opening this as a PR since Issues are disabled on this repo—no need to merge the file!**