# Protect Attacks listening check

The [16-second A/B](demo-protect-ab.wav) repeats the same eight seconds of a PCM clean-guitar stem from the M1 `Something New` Logic folder. First half: Fracture with Protect 0%. Second half: the identical pattern with Protect 70%. The second half has +0.89 dB monitoring gain to match the first half's integrated loudness. Both halves began from the same source sample, so compare picking clarity and how much of the reverse, gate and repeat pattern remains.

The source is `Guitarras Limpias_1.wav`, seconds 72–80, 44.1 kHz stereo PCM. Its status as the current production take has not been confirmed. The renderer applies the same peak-based input trim to both halves, leaving about 6 dB sample-peak headroom. No limiter, mastering or post effect is added.

Offline 120 BPM pattern: steps 1–4 dry, 5–6 reverse, 7–8 gate, 9–10 repeat, 11–12 tape stop, 13–14 shuffle, 15–16 dry. The WAV uses 24-bit PCM. Fracture's 0% half measured −18.04 LUFS integrated and −5.98 dBTP; 70% measured −18.93 LUFS and −5.99 dBTP before the listening gain. The difference signal between the two processed halves is −7.3 dB relative to the 70% signal. That is a measurable change, not a judgment that the sound is musically better.

The renderer and A/B test exercise the engine offline. Logic automation, session reopening, seek behavior, monitor listening and Windows-host loading still need validation before a new release.
