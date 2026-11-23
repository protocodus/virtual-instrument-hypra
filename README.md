# Hypra

Hypra is a proof of concept of an eight-voice virtual-analog synthesizer
modelling the Roland JP-8000 (1997), meant to become a Reason Rack Extension
once its sound is approved. "Hypra" is a working name. It is an independent
original implementation, not affiliated with or licensed by Roland, and holds
no firmware, ROM data, samples, captured audio or factory patch data.

**Status: 0.4.0, for listening.** The engine is complete enough to judge the
sound. There is no plug-in, Rack wrapper, preset bank, arpeggiator, split
mode or motion control. The saw, pulse and triangle-mod waveforms and the
filter are fitted to an emulator running the JP-8000's own system software,
played as a black box, and agree with it within 0.2 dB. So are the
envelopes, both LFOs, FADE, the modulation depths, pan, OSC BALANCE, key
follow, portamento, cross mod and the noise oscillator (1-1.5 % median
gaps). The supersaw is fitted to a recording of a JP-8080. The triangle's
SHAPE, the feedback oscillator, ring modulation, the velocity matrix and
the effects are still estimates.

![Front-panel draft](Docs/screenshots/front-panel-draft-2x.png)

The panel is a concept, not a built device.

---

## How it works

The JP-8000 is a digital instrument: four Toshiba TC170C140 DSPs compute the
voices and effects, and only the stage from the DAC to the jacks is analogue.
So the model has two halves.

- **The DSP, as behaviour.** 88.2 kHz inside whatever the host rate, naive
  oscillators that alias, 7-bit controls, an 18-bit DAC and, as inferences,
  24-bit phase, uncorrected sync and hard clipping at full scale. There is no
  invented analogue drift, because the hardware has none.
- **The output stage, as a circuit.** The network from the DAC to the line
  jack, solved from the 1997 service notes' component values and discretised
  at 705.6 kHz. Left out: the volume pot's loading, mute transistors,
  ferrites, a 100 ohm / 100 pF RF filter, op-amp limits and noise.

The signal path is `OSC 1 + OSC 2 (sync, ring, X-mod) -> filter -> amp ->
tone control -> delay -> chorus -> pan -> DAC -> output circuit`, for two
parts sharing eight voices. Unison comes from the JP-8080; the JP-8000 has no
unison switch.

[The hardware model](Docs/research/hardware-model-2026-11-22.md) says for
each mechanism whether it is settled by a document, measured by someone else,
or inferred here.

## Known gaps

- Only the supersaw at Detune 0 / Mix 0 on one note, the saw at its round
  end, the 12 dB low-pass, the tone control, the feedback howl's tuning and
  the output stage's tilt have been checked against the hardware
  ([measurements](Docs/research/hardware-ab-2026-11-22.md)). Every
  `[inferred]` law is a placeholder until a recording tests it.
- The feedback oscillator, the effects' modulation settings, the filter
  algorithm and all envelope, LFO and cutoff tapers are inferred.
- The JP-8000's service notes mark a 1.06 kHz pole in the main output which
  the JP-8080's do not have; see `Docs/decisions.md`.
- Split mode, arpeggiator, RPS, motion control, ribbon, velocity assign,
  tempo sync and SysEx are not implemented.
- About 4 % of one Apple silicon core with eight voices at 48 kHz, 2.7 % of
  it the fixed output stage. Not measured in Reason or under phdsp64.

## Version history

| Version | Change |
| --- | --- |
| 0.4.0 | Envelope segments start late (25 ms from 30), short attacks are faster, FADE ramps in decibels, LFO 2 has its own rate, amp LFO, pan, OSC BALANCE (constant power), X-MOD (FM in Hz) and the noise oscillator (the main filter on the key) follow JE-8086. Velocity is off by default, as there. |
| 0.3.0 | Envelopes, LFO 1 rate and depths, pitch and filter envelope depths, key follow and portamento follow black-box measurements of JE-8086: one exponential time constant per value for attack, decay and release, a glide of fixed time, key follow at half strength below zero. The demo patches keep their old timing under the new tables. |
| 0.2.0 | The filter, saw SHAPE, pulse width and triangle mod follow black-box measurements of JE-8086: a different filter structure, cutoff a semitone per step, a measured resonance curve, -24 dB as two identical stages. |
| 0.1.3 | Detune and Mix follow Szabo's measured tables instead of his fitted curves: the sides are 14 % quieter at Mix 0. |
| 0.1.2 | The saw's round end and the feedback howl's tuning follow factory-patch recordings. |
| 0.1.1 | The supersaw follows a recorded JP-8080: a four-pole tracked high-pass, which restores its fundamental, and the recorded drift at Detune 0. |
| 0.1.0 | First proof of concept. |

## Build

```sh
cmake -S . -B build-dsp -DCMAKE_BUILD_TYPE=Release
cmake --build build-dsp --parallel 2
ctest --test-dir build-dsp --output-on-failure
./build-dsp/HypraRenderDemos deliverables/demos
python3 Tools/panel/render_front.py
```

The panel script needs Pillow, the macOS Arial fonts and a
`reason-rack-extension` checkout beside this one for the stock widgets.

## Licensing

Source-available under the [PolyForm Noncommercial licence](LICENSE). Roland,
JP-8000 and JP-8080 are trademarks of Roland Corporation, used only to name
the modelled instrument.
