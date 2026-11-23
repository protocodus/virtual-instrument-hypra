# JP-8000 hardware model, 2026-11-22

## Sources

| Short | Source | Used for |
| --- | --- | --- |
| Factory sounds | SynthMania's JP-8000 recordings of A33 JP-303 and P14 Feedback Lead, in [the A/B](hardware-ab-2026-11-22.md) | 12 dB low-pass, saw shape at the round end, tone-control corners, filter-envelope shape, feedback howl |
| Black box | JE-8086 running JP-8000 system 1.05, measured as an instrument: [method and results](black-box-je8086-2026-11-22.md) | filter structure, cutoff and resonance laws, saw SHAPE, pulse width, triangle mod, waveform levels |
| Recording | [JP-8080 note by FadedShadows, Freesound 275434, CC0](https://freesound.org/people/FadedShadows/sounds/275434/), measured in [the A/B](hardware-ab-2026-11-22.md) | supersaw high-pass, phase word, drift at Detune 0, output tilt |
| SN-8000 | [Roland JP-8000 Service Notes, Jan 1997](https://archive.org/details/roland_JP-8000_SERVICE_NOTES) | parts, block diagram (p.15), DAC (p.23), panel B (p.27), jack board (p.30) |
| SN-8080 | [Roland JP-8080 Service Notes, Jul 1998](https://archive.org/details/roland_JP-8080_SERVICE_NOTES) | clock (pp.13, 19), output (p.21), volume follower (p.27) |
| OM-8000 | [Roland JP-8000 Owner's Manual, 1996](https://archive.org/details/synthmanual-roland-jp-8000-owners-manual) | parameters, waveform drawings, routing, effects |
| OM-8080 | [Roland JP-8080 Owner's Manual](https://archive.org/details/synthmanual-roland-jp-8080-owners-manual) | unison, LFO 2 |
| Szabo | [A. Szabo, "How to Emulate the Super Saw", KTH 2010](https://www.adamszabo.com/internet/adam_szabo_how_to_emulate_the_super_saw.pdf) | supersaw offsets, detune law, mix laws, high-pass, phases |

Read on 2026-11-22 (PDF SHA-256
`a354c6764a189e20e1c771a253fe4be50281b6f292bc114d307cdf9a1d9f07e2`, from the
owner's link). It was recorded from a JP-8000 and a JP-8080 at 44.1 kHz. The
offsets (Table 1) match those we had. Detune and Mix now use his measured
Tables 2 and 3. His fitted curves miss the detune table by up to 4% and turn
back near the middle, and they put the side level at Mix 0 16% above his own
measurement (0.0444 against 0.0384; the JP-8080 recording here gave 0.041).
His high-pass is "at the first harmonic", with no order given. Phases random
at each note, no band-limiting: both confirmed in the text.

**Deliberately not used:** the 2025-26 emulator built on extracted JP-8000
firmware, its source, and write-ups reproducing its code or tables. Search
summaries of that work were seen while researching (seven naive saws into a
high-pass at 88.2 kHz). No value or code comes from it.

The schematic values were copied by eye from the archive.org page scans.

## Settled by Roland's documents

| Mechanism | Evidence |
| --- | --- |
| 88.2 kHz DSP and DAC word rate (67.7376 MHz / 768) | SN-8080 pp.13, 19; same parts in SN-8000 |
| Four TC170C140 DSPs, uPD63200 DAC strapped to 18 bit | SN-8000 pp.4-5, 23 |
| Output network's component values | SN-8000 pp.23, 27, 30; SN-8080 pp.19, 21, 27 |
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
| Seven offsets; Detune and Mix tables, interpolated | Szabo, Tables 1-3 |
| Pitch-tracked high-pass on the saw stack | Szabo |
| Unrelated phases at each note | Szabo |
| No band-limiting | Szabo |

## Measured here from a recording

| Mechanism | Evidence |
| --- | --- |
| Supersaw high-pass: two identical 12 dB sections at 0.904 f0, Q 0.878 | Recording, amplitude and phase |
| 24-bit phase at the documented 88.2 kHz | Recording: drift in whole steps of 88200 / 2^24 Hz |
| Detune 0 amount 0.0027, floored products | Recording: steps -6, -4, -1, +1, +3, +5 |
| No 1.06 kHz pole in the output | Recording; the JP-8000 file agrees |
| -12 dB low-pass is a two-pole response, k 0.17 at A33's setting | A33, 0.46 dB on held-out notes |
| SHAPE 0 saw is s + 2.056 (s - s^3) | A33 and the JP-8000 note: K = 1.26 and 1.24 |
| Tone-control shelves near 250 Hz and 4.4 kHz | A33 |
| Feedback loop 2 to 3 samples short of a period | P14, two notes |
| Filter envelope decays exponentially (in octaves or in Hz) | A33, 72 notes |

## Inferred here

| Mechanism | Choice | What would settle it |
| --- | --- | --- |
| Triangle SHAPE above 0 | a cusp, then a fold; captured but not yet modelled | a model that fits the captures |
| Supersaw high-pass tracking | the same ratio and Q on every key and detune | a second recorded note |
| Feedback oscillator above the harmonic | a band-passed howl; JE-8086 adds a square's odd harmonics | a model of the loop's limiter |
| Sync | reset on the sample | spectrum at high ratios |
| Release and decay below 10 | the fast part only, not the tail | finer captures |
| Velocity matrix, ring modulation, LFO to X-MOD | old guesses | black-box sweeps |
| Control rate | 2.76 kHz with interpolated cutoff and gain | stepping in a fast sweep |
| Tone control | 12 dB at full travel | swept response |
| Chorus and flanger types | names from the manual, settings chosen here | impulse response per type |
| Delay feedback and taper | 93 % maximum, square law | impulse response |
| Headroom and clipping | hard clip at the end of the DSP | overdriven recording |
| Unison detune spread | even, +/-12 cents by default | a recording |

## The 1.06 kHz question

SN-8000 p.27 marks C28 and C29 10000p across the 22k leg of a 47k/22k divider
after the volume pot: a pole at 1.06 kHz in the main output. SN-8080 p.27 has
the same DSP, DAC and jack filter with a plain follower. Either the JP-8000
pre-emphasises digitally, or the marking is wrong, or the keyboard really is
that much darker. The recordings decided against the last: see
[the A/B](hardware-ab-2026-11-22.md). The flat one is the default.

## What was and was not verified

`Tests/EngineTests.cpp` checks that the code does what is written above: the
offsets land within 1 Hz at 1 kHz, the slopes match, the discrete circuit
follows the analytic one within 0.15 dB, the delay maxima are exact. That is
self-consistency. It is not evidence of likeness to a real JP-8000, except
for the `HardwareRecording` suite, whose expectations come from a recording.
