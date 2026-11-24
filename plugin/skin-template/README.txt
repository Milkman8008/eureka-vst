eureka! skin template
=====================

Copy the files you change into Documents/eureka/skin/ and reopen the plug-in window.
To ship a skin inside the plug-in, put them in plugin/skin/ and rebuild.
Any file left out falls back to the built-in look.

background.png        Whole window. Design size 1200 x 802; make it 2400 x 1604 for sharp
                      Retina display. Used on every page unless a page has its own:
background-synth.png, background-mod.png, background-seq.png, background-multi.png,
                      background-presets.png
                      Start from background-template.png (empty window) and put
                      guide-<page>.png (the whole UI) on a layer above to line things up.
                      Panels, labels, knobs and the header controls are drawn by the
                      plug-in on top of the background.
                      To let the background show through the panels, lower
                      panelOpacity in skin.json (0 = no panel boxes at all).

logo.png              Header wordmark, fitted into 230 x 52 (here 460 x 104 for Retina).

knob.png              Knob filmstrip: square frames stacked vertically, first frame =
                      minimum, last = maximum. Any frame size and count (this one: 128
                      frames of 120 x 120). With knobValueArc true the plug-in draws the
                      coloured value ring around it; set it false if your strip draws
                      its own ring.

skin.json             panelOpacity / headerOpacity / displayOpacity (0..1),
                      knobValueArc (true/false), partColours (six #rrggbb colours).
