# A/B against hardware recordings, 2026-11-22

The owner supplied both files. They are not committed.

| File | SHA-256 | What it turned out to be |
| --- | --- | --- |
| [Freesound 275434](https://freesound.org/people/FadedShadows/sounds/275434/), JP-8080, CC0, 3.459 s | `2e5ebfddd21682ea9bda512dc53fc310a1004056dae1222b890bdf153cb41fd7` | SUPER SAW at Detune 0 / Mix 0, G2 + 15.4 cents (98.874 Hz). Lossy before it became a WAV (18.6 kHz cut-off switching with the blocks, like MP3 at 192k). |
| [Freesound 275439](https://freesound.org/people/FadedShadows/sounds/275439/), JP-8000, CC0, 0.895 s | `664a7f21c7ace4cd48c727dbce2bd45c9835f10609f2ff098339a0400096e97c` | A single saw at C3 with a rounded shape. Exported from Logic, peak at -0.1 dBFS, lossy (16 kHz), rumble under the note at -16 dB of the energy, treble rolled off without phase shift. Processed, settings unknown. |

"Initialized" is the uploader's word. That both came from the hardware is
also the uploader's claim.

## Method

Each harmonic's complex amplitude, divided by an ideal saw's, gives the
response. For the JP-8080, transforming those back over the cycle shows each
saw's step as a moving line; all seven phases, speeds and levels were then
fitted jointly with a candidate filter and the solved output circuit.

Control: the engine rendered through MP3 at the same bitrate, then through
the same analysis. Version 0.1.0 (two poles at f0) read back as two poles at
1.023 f0, side level 0.0443 (set 0.04447) and integer drift steps within
0.01. Version 0.1.1 read back as 0.915 f0, Q 0.879, beside the recording's
0.904 and 0.878.

## Findings

**Output stage.** Residual of the best fit with stage B: -17.1 dB. With A,
the JP-8000 schematic as printed: -4.0 dB. On the JP-8000 file, even with
free shape and treble terms, A leaves 5.0 dB and 28 degrees RMS against B's
0.2 dB and 1.7 degrees. B stands. An exact inverse EQ in both recording
chains would be the only way round this.

**Phase word.** The six side saws drift from the centre at -6.00, -4.01,
-1.03, +0.97, +3.01 and +5.00 times 88200 / 2^24 Hz. With the 88.2 kHz
clock from the service notes, the phase word is 24 bits.

**Detune at 0.** Those integers follow from Szabo's offsets with floored
products and an amount between 0.00267 and 0.00272. His polynomial's 0.00301
does not give them.

**High-pass.** Over the first 24 harmonics, error of the averaged response
(output stage B included):

| Candidate | Error |
| --- | ---: |
| two poles at f0, as 0.1.0 | -13.2 dB |
| two poles, free frequency and Q | -17.6 dB |
| three poles, Butterworth | -20.0 dB |
| four poles, Butterworth | -25.8 dB |
| second-order all-pass | -29.0 dB |
| **two identical sections, 0.904 f0, Q 0.878** | **-32.6 dB** |
| two free sections | -32.7 dB |

**Side level at Mix 0.** 0.041 +/- 0.002 of the centre, where Szabo's law
gives 0.0445. Left alone: one lossy recording is thinner evidence than his
series.

## Result

Third-octave harmonic levels, 99 Hz to 12.7 kHz, mean offset removed, both
through the same codec, median of 16 starting-phase seeds:

| JP-8080 supersaw | RMS gap | Largest | At the fundamental |
| --- | ---: | ---: | ---: |
| 0.1.0 | 0.72 dB | 2.20 dB | -2.22 dB |
| 0.1.1 | 0.50 dB | 1.27 dB | -0.25 dB |

What is left is mostly between 400 and 800 Hz, where each note's random
phases move harmonics by about half a decibel.

The JP-8000 file against Trancera's nearest saw (SHAPE 10): 2.1 dB RMS, 5.8 dB
at worst, at the top. Its harmonics follow (1 + 1.24 / n^2) to 0.23 dB under
a treble roll-off. Nothing was fitted to it, because its processing and
settings are unknown.

## What this does not show

Other keys, detune and mix settings, the other six waveforms, filter,
envelopes and effects. None of those appears in these recordings.

## Round 2: five factory-sound recordings

The owner approved these for private benchmarking. They are not committed.

| File (source) | SHA-256 | Use |
| --- | --- | --- |
| A33 JP-303 (SynthMania) | `5b70b3fcaf19a4585e3d4c9626ec88c9db2b26e622247796a883c06dd3b32dd1` | Calibration: dry, mono, 203 notes |
| P14 Feedback Lead (SynthMania) | `761a3a9ce11ced13c3be71e19d2f3ae82dd71701d0e5c33dc75b2e7d23e4e3df` | Howl tuning only: has ribbon and effects |
| JP-8000 Super Saw demo (SynthMania) | `cdd2290e285acbe8a6e3b0f61990812db4ae6566938748e967cff107a71a0596` | Not used: wide stereo effects |
| A34 Rave Time (SynthMania) | `d755fc865a7387005dabf3274685042da370e1da3819e7225c4de82c812723bf` | Not used: noise-led, effects |
| Supersaw Arp 1 (Wikimedia Commons, CC BY-SA 3.0) | `8d9d846010f0b11f18d204c34bd7d5b8921a020deca8c776ac022fa5926eae96` | Not used: processed, hardware origin unclear |

All are lossy.

**A33, 12 dB low-pass.** 203 notes transcribed from onsets (136.5 BPM
sixteenths, A = 440). Each clean 14-30 ms frame gave harmonic amplitudes.
The filter, the waveform and a fixed EQ were fitted jointly on the first 11 s
and tested on the rest, refitting only each frame's level and cutoff:

| Filter | Fitted half | Held-out half |
| --- | ---: | ---: |
| 2-pole, one k | 0.32 dB | 0.46 dB |
| 2-pole, k varying with cutoff | 0.32 dB | 0.42 dB |
| 4-pole ladder | 2.35 dB | 2.65 dB |
| two 2-pole sections, free | 3.22 dB | 3.48 dB |

So the patch's -12 dB setting really is two poles (k 0.17).

**Saw shape.** The same fit wants harmonics (1 + K / n^2) / n, K = 1.26,
better than a boosted fundamental (0.56 dB held out) or a plain saw (1.09 dB).
The JP-8000 note gives K = 1.24 to 1.27, zero phase. That is s + 2.056 (s - s^3),
the round end of SHAPE. Whether A33 and the note are both at SHAPE 0 is an
inference.

**Tone control.** The fitted EQ is a +4.5 dB shelf at 251 Hz and +12 dB at
4.4 kHz, beside the 250 Hz and 4 kHz first guessed.

**Filter envelope.** Over 72 notes, exponential decay of the cutoff, in
octaves or in Hz, fits to 0.016 octave; a straight line in octaves to 0.045.

**Feedback howl.** P14 holds howls at 3.054 x B4 and 4.031 x E4, with their own
harmonics and 27 Hz beats against the note's. A loop 3.1 and 2.0 samples
short of the period at 88.2 kHz gives both. Two notes, with a ribbon on the
patch: tentative.

**Rebuilt phrase.** A33 played back by Trancera from the transcription with each
note's fitted cutoff curve, level-matched per note and put through MP3:
third-octave gap 3.0 dB on the fitted half, 3.7 dB held out. Most of the gap
is where the cutoff curves could not be read (four notes in ten) and were
filled in from neighbours, so that number measures the transcription as much
as the engine.
