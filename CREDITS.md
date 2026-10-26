# Credits

## Glitch 2 Library presets

The presets under **Glitch 2 Library** in Fracture's preset menu are converted from the factory banks that ship
with [Glitch 2](https://illformed.com/glitch/) by Illformed. The pattern designs are the work of:

| Designer | Banks | Website |
|---|---|---|
| Illformed | Bank 1 | https://illformed.com |
| Alex Shore | Bank 1 | http://alex-shore.com |
| Rick Christy, Soundgasm Design | A, B, Distortion, Gater, Lofi, Retrigger, Reverser, Stretcher, Tape | http://www.soundgasmdesign.com · http://www.rickchristy.com |
| Gotthard Ortner, Supremeja Music | Delay, Distortion, Gater, LoFi, Random, Retrigger, Reverser, Stretcher, Tape Stop, Combi 1–6 | http://www.supremeja.com |
| WaveShaper / Sink | Banks 1–3 | http://waveshaper.org · http://sinkmusic.com |

What was converted: each scene's sequencer pattern and timing (beats and divisions per beat). Glitch's effect
modules map onto Fracture's lanes (Retrigger → Repeat, Reverser → Reverse, Stretcher → Stretch, Gater → Gate,
Lofi → Crush, Distortion → Distort, Delay → Echo, Shuffler → Shuffle, Modulator → Ring Mod, Randomizer → Random,
Tape Stop → Tape Stop). The modules' own settings don't carry over, so these presets use Fracture's lane settings
and will sound different from the originals. Where Glitch stacked several effects on one step, the most recently
started one is kept.

The conversion is done by `scripts/convert_glitch_presets.py` and the result lives in `Source/GlitchLibrary.inc`.
Glitch and Glitch 2 are products of Illformed; Fracture isn't affiliated with or endorsed by Illformed.

## Libraries

- [JUCE](https://juce.com) (AGPLv3)
- [Catch2](https://github.com/catchorg/Catch2) (Boost Software License) for tests
