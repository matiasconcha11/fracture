# Fracture

**A step sequencer of glitch effects.** Paint effects onto a 16-step grid and Fracture plays them in time with
your DAW: stutters, reverses, tape stops, gates, bit crushing, filter sweeps, shuffled slices, echoes and
pitch drops. Hit Randomize for instant chaos, turn up Mutate and let the pattern evolve by itself, or use each
lane's Chance knob so it never repeats exactly. A live waveform shows what every effect did to your audio.

AU · VST3 · Standalone — macOS (Windows VST3 builds in CI) — built with [JUCE](https://juce.com)

![Fracture](docs/screenshot.png)

## Features

- **Nine effect lanes**, one effect per step:
  - **Repeat** — retrigger the start of the step 2 to 16 times, optionally dropping in pitch each time
  - **Reverse** — the previous step, backwards
  - **Tape Stop** — slows to a halt within the step
  - **Gate** — rhythmic chopping with adjustable pulses and length
  - **Crush** — bit depth and sample-rate reduction
  - **Filter** — a resonant sweep that closes or opens across the step
  - **Shuffle** — an earlier step plays in place of this one
  - **Echo** — a feedback delay that throws the step's audio
  - **Pitch** — shifts the step up or down by up to two octaves
- **Four patterns (A–D) and Chain** — chain plays every pattern that has steps in it, one loop each
- **Chance per lane** — how often that lane's steps actually fire
- **Mutate** — each loop, a step or two may change by themselves so the pattern evolves
- **Patterns, Randomize, Undo/Redo** — 14 starting patterns, random ones with adjustable density, copy between slots
- **Timing** — steps of 1/8, 1/16, 1/32 or triplets, swing, and pattern length 1–16 for odd-length loops;
  locked to your DAW's transport (it only glitches while playing)
- **Live view** — a waveform of the output across the pattern, coloured by the effect that shaped each moment,
  plus a playhead and a sweep across the step that's firing
- **Mix, Output and click-free bypass**; every step edge is faded so switching effects never clicks

## Install

### Download

Get the latest zip for your system from [Releases](https://github.com/matiasconcha11/fracture/releases/latest).

**macOS** — unzip and move the plugin into place:
- AU (Logic, GarageBand): `Fracture.component` → `~/Library/Audio/Plug-Ins/Components/`
- VST3 (Ableton, Reaper, etc.): `Fracture.vst3` → `~/Library/Audio/Plug-Ins/VST3/`

The builds aren't notarized by Apple yet, so macOS blocks them at first. Clear the download flag in Terminal:
```
xattr -dr com.apple.quarantine ~/Library/Audio/Plug-Ins/Components/Fracture.component ~/Library/Audio/Plug-Ins/VST3/Fracture.vst3
```
Then restart your DAW and look for **Matias Concha → Fracture**.

**Windows** — unzip and move `Fracture.vst3` into `C:\Program Files\Common Files\VST3\`. (The Windows build is
compiled and unit-tested automatically but hasn't been tried in a DAW yet.)

### Build from source

1. Install Xcode (or its Command Line Tools) and the build tools:
   ```
   xcode-select --install
   brew install cmake ninja
   ```
2. Build:
   ```
   git clone https://github.com/matiasconcha11/fracture.git
   cd fracture
   make
   ```

The build installs the plugin into `~/Library/Audio/Plug-Ins/`.

## Using it

- Click a cell to place that lane's effect on that step; click it again to clear it. Drag along a lane to paint
  or erase several steps.
- A step holds one effect at a time: placing one replaces whatever was on that step.
- Hover a small knob to see what it does; drag it to change the value.
- Effects that replay audio (Repeat, Reverse, Tape Stop, Shuffle) use what the track played a moment ago, so they
  work best on parts that are already playing (drums, loops, vocals), not on the very first step after you press play.

## Development

```
make            # Release build, installed into ~/Library/Audio/Plug-Ins/
make test       # unit tests (Catch2)
make validate   # pluginval at strictness 10 on the VST3 and AU
make check      # test + validate

./build/FractureTests "[.snapshot]"   # render the editor to /tmp/fracture.png
```

CI builds and tests on macOS and Windows on every push; pushing a version tag builds release zips.

## Status

Fracture is new: it passes pluginval and Apple's auval, and real-world testing in Logic Pro is under way.
Bug reports and ideas are welcome in [Issues](https://github.com/matiasconcha11/fracture/issues).

Also by the same author: **[Contour](https://github.com/matiasconcha11/contour)** (curve shaper) and
**[Duck](https://github.com/matiasconcha11/duck)** (one-knob sidechain).

## License

Fracture is free software under the [GNU Affero General Public License v3.0](LICENSE): you can use, study, change
and share it, as long as anything you distribute that's built on it is released under the same license.
It's built on [JUCE](https://juce.com), which is used here under the same AGPLv3 terms.
