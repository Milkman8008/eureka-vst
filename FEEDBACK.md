Hey! Just wanted to first say that it's really great to have a virtual Ambika!

I was playing around with it a bit today and saw in the README that all parts currently listen to every MIDI channel—is per-part MIDI channel routing (like on the hardware) something you might consider adding in the future?

### A quick UI suggestion:
- In the "Filter" section, the "Variant" dropdown applies to Ripples and the Surge filter models, while "Ambika SVF" leaves it blank and uses a separate "Mode" dropdown (`LP` / `BP` / `HP`). Since you mentioned in `plugin/README.md` that they use separate patch bytes for backwards compatibility, would it be possible to merge them visually in the UI (so "Variant" displays `LP / BP / HP` when Ambika SVF is selected) just to streamline the panel?

### I also noticed a few possible bugs:
1. It doesn't seem like all the crossmod modifiers work as expected...I might be doing something wrong, need to check against the hardware to confirm:
   - **a.** Work well: `xor`, `ringmod` (though when using `ringmod` I noticed a noticeable level drop)
   - **b.** Very subtle effect when activated, but almost no effect when modulated via the Mod Matrix: `fold`, `bit`
   - **c.** Seem to have no effect: `sum`, `sync`
2. If I use the Mod Matrix to modulate any parameter, the waveform or related display doesn't change (i.e., you don't see the oscillator morph or the filter curve move), whereas controlling parameters via host automation/MIDI gives visual feedback.

Thanks again for making this happen!!

**Opening this as a PR since Issues are disabled on this repo—no need to merge the file!**