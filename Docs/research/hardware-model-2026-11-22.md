# JP-8000 hardware model, 2026-11-22

## Sources

| Short | Source | Used for |
| --- | --- | --- |
| SN-8000 | [Roland JP-8000 Service Notes, Jan 1997](https://archive.org/details/roland_JP-8000_SERVICE_NOTES) | parts, block diagram (p.15), DAC (p.23), panel B (p.27), jack board (p.30) |
| SN-8080 | [Roland JP-8080 Service Notes, Jul 1998](https://archive.org/details/roland_JP-8080_SERVICE_NOTES) | clock (pp.13, 19), output (p.21), volume follower (p.27) |
| OM-8000 | [Roland JP-8000 Owner's Manual, 1996](https://archive.org/details/synthmanual-roland-jp-8000-owners-manual) | parameters, waveform drawings, routing, effects |
| OM-8080 | [Roland JP-8080 Owner's Manual](https://archive.org/details/synthmanual-roland-jp-8080-owners-manual) | unison, LFO 2 |
| Szabo | [A. Szabo, "How to Emulate the Super Saw", KTH 2010](https://www.adamszabo.com/internet/adam_szabo_how_to_emulate_the_super_saw.pdf) | supersaw offsets, detune law, mix laws, high-pass, phases |

Szabo's numbers are taken from Septum's engine, where they were transcribed
earlier. The PDF was not re-read for this work, so they have not been checked
against the thesis again.

**Deliberately not used:** the 2025-26 emulator built on extracted JP-8000
firmware, its source, and write-ups reproducing its code or tables. Search
summaries of that work were seen (seven naive saws into a high-pass at
88.2 kHz), and say nothing that the sources above do not.

The schematics were read from 600 dpi scans and the values copied by eye.

## Settled by Roland's documents

| Mechanism | Evidence |
| --- | --- |
| 88.2 kHz DSP and DAC word rate (67.7376 MHz / 768) | SN-8080 pp.13, 19; same parts in SN-8000 |
| Four TC170C140 DSPs, uPD63200 DAC strapped to 18 bit | SN-8000 pp.4-5, 23 |
| Output network, every value | SN-8000 pp.23, 27, 30; SN-8080 pp.19, 21, 27 |
| Eight voices, two parts, last-note priority | OM-8000 pp.48, 129 |
| Seven OSC 1 waves, three OSC 2 waves, what both controls do | OM-8000 pp.55-57 |
| Feedback oscillator is monophonic | OM-8000 p.56 |
| X-mod 2 to 1, sync 1 to 2, ring in OSC 2's place in the balance | OM-8000 p.59 diagram |
| No X-mod with supersaw or noise | OM-8000 p.58 |
| LP, BP, HP at 12 and 24 dB, oscillating; key follow +32 = 100 % | OM-8000 pp.59-61 |
| LFO 1 waves, saw falls; fade does not affect auto pan | OM-8000 pp.62-63 |
| LFO 2 is a triangle, only under the modulation lever | OM-8080 p.87 |
| Tone, delay, chorus, pan order and stereo wiring | OM-8000 p.65 diagram |
| Delay maxima 1250, 625 and 156 ms | OM-8000 p.65 |
| Unison uses all the part's voices, never with feedback osc | OM-8080 |

The output circuit works out to a bass shelf of about 3.6 dB in all
(+3 dB at 20 Hz against 1 kHz) from R41/R38/C24, a second-order Sallen-Key
low-pass at 28.2 kHz, four 0.16 Hz coupling high-passes, and small RC poles
at 34 and 73 kHz.

## Measured by others

| Mechanism | Evidence |
| --- | --- |
| Seven offsets, the 11th-order detune law, both mix laws | Szabo |
| Pitch-tracked high-pass on the saw stack | Szabo |
| Unrelated phases at each note | Szabo |
| No band-limiting | Szabo |

## Inferred here

| Mechanism | Choice | What would settle it |
| --- | --- | --- |
| Supersaw high-pass order | two poles, Butterworth | spectrum of a high note |
| Phase word | 24 bit | pitch error of very low notes |
| Triangle Mod | triangle times two, offset, wrapped at the rails | one cycle at three offsets |
| Saw and triangle SHAPE | morphs matching OM-8000 p.56's drawings | one cycle per slider step |
| Noise cutoff | key-following, 11 octaves, two poles | swept spectra |
| Feedback oscillator | one-period limited loop with a harmonic-selecting band-pass | recordings at several settings |
| X-mod | linear through-zero FM, square-law depth | sidebands against depth |
| Sync | reset on the sample | spectrum at high ratios |
| Filter | trapezoidal SVF, cascaded, amplitude-dependent damping | swept responses per resonance step |
| Cutoff, envelope, LFO, portamento tapers | exponential maps in `Mapping.h` | timed recordings |
| Pitch and filter modulation spans | 24 semitones, 10 and 4 octaves | recordings at full depth |
| Control rate | 2.76 kHz with interpolated cutoff and gain | stepping in a fast sweep |
| Tone control | 12 dB shelves at 250 Hz and 4 kHz | swept response |
| Chorus and flanger types | names from the manual, settings chosen here | impulse response per type |
| Delay feedback and taper | 93 % maximum, square law | impulse response |
| Headroom and clipping | hard clip at the end of the DSP | overdriven recording |
| Unison detune spread | even, +/-12 cents by default | a recording |

## The 1.06 kHz question

SN-8000 p.27 marks C28 and C29 10000p across the 22k leg of a 47k/22k divider
after the volume pot: a pole at 1.06 kHz in the main output. SN-8080 p.27 has
the same DSP, DAC and jack filter with a plain follower. Either the JP-8000
pre-emphasises digitally, or the marking is wrong, or the keyboard really is
that much darker. Nothing read here decides it, so both are rendered for
listening and the flat one is the default.

## What was and was not verified

`Tests/EngineTests.cpp` checks that the code does what is written above: the
offsets land within 1 Hz at 1 kHz, the slopes match, the discrete circuit
follows the analytic one within 0.15 dB, the delay maxima are exact. That is
self-consistency. It is not evidence of likeness to a real JP-8000.
