Hey! Just wanted to first say that it's really great to have a virtual Ambika!

I was playing around with it a bit today and was wondering if there's currently a way to assign each part in a Multi to its own MIDI channel like in the hardware? If not, is that something you might consider adding in the future?

### A few questions:
1. I noticed in the "Filter" section that the "Variant" dropdown applies to the other filter models, while the "Ambika SVF" leaves it blank and uses a separate "Mode" dropdown instead. I was just curious if there's a specific reason for keeping them separate?
2. I'm not sure if this has been implemented as well, but the original hardware has a 12-bit DAC (*"The resulting signal is converted to the analog domain by a 12-bit DAC and fed into an analog VCF and VCA"*). Has this been approximated as well?

### I also noticed a few possible bugs:
1. It doesn't seem like all the crossmod modifiers work:
   - **a.** Work well: `xor`, `ringmod` (though when using `ringmod` I noticed a level drop)
   - **b.** Very subtle effect when activated, but almost no effect when modulated: `fold`, `bit`
   - **c.** Seem to have no effect: `sum`, `sync`
2. If I use the Mod Matrix to modulate any parameter, the waveform or related display doesn't change (i.e., you don't see the oscillator change forms, or the filter move), but when you use MIDI CC there is visual feedback of the parameter changing.

Thanks again for making this happen!!
