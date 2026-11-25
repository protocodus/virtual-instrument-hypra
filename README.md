# Trancera

Trancera is a native C++17 virtual-analog synthesizer modelled on the Roland JP-8000 (1997) and JP-8080 (1998). The owner named it Trancera on 2026-11-23 (it was Hypra until then). It is an independent original implementation, not affiliated with or licensed by Roland, and holds no firmware, ROM data, samples, captured audio or factory patch data. Since 2026-11-23 it is not clean-room: at the owner's risk, constants may be taken from an analysis of the JP-8000 firmware (tagged `[firmware]`).

**Status: 0.9.6.** Its Reason Rack Extension is `Examples/Trancera` in `protocodus/reason-rack-extensions` (with a 9U 5× HD Cobalt Studio panel, 160 categorized factory presets plus 16 root highlights, 70 automatable panel controls plus `Mono`/`Unison`, `DAC` circuitry toggle, 14-bit `Mod Wheel`, `Pitch Wheel`, `Sustain`, `Channel Aftertouch`, and a 12-input / 4-output rear CV modulation matrix alongside auto-routed Note/Gate CV), accompanied by a canonical C++17 JP-8000 / JP-8080 SysEx codec, the `TranceraSysEx` CLI converter, and an offline browser converter (`Tools/sysex-converter/index.html`) for `.syx`/`.mid` ↔ `.repatch` exchange. There is no standalone VST3/AU/CLAP plug-in wrapper yet, and no Split key mode, Control Assign matrix, or Arp Beat Patterns yet.

In a 20-patch JE-8086 A/B pass on `0.9.2` (Part A, `SINGLE`, velocity matrix active, digital node), **18 of 20 factory patches** match within ≤ 2.0 dB across raw level and all three spectral bands (`< 300 Hz`, `0.3–3 kHz`, `> 3 kHz`), and **19 of 20** match within ≤ 2.2 dB.

![The Rack Extension's front panel](Docs/screenshots/trancera-rack-front.png)

The Rack Extension's front panel (`0.9.6f4`), rendered by `Design/render_panels.py`.

## Audio demos

Eight curated factory presets (one per category) rendered through the real `CTrancera` Rack adapter in the SDK-free test host at `48 kHz / 24-bit`, using each phrase's tempo, song settings at defaults, and zero external effects or normalisation.

<!-- audio-demos-begin -->

| # | Preset | Demo | Length | Peak |
| --- | --- | --- | --- | --- |
| 20 | Unison Trance Sub | [020-unison-trance-sub.wav](Docs/audio/020-unison-trance-sub.wav) | 9.9 s | -14.6 dBFS |
| 40 | Wide Unison Strings | [040-wide-unison-strings.wav](Docs/audio/040-wide-unison-strings.wav) | 11.0 s | -10.4 dBFS |
| 58 | Tine FM Electric | [058-tine-fm-electric.wav](Docs/audio/058-tine-fm-electric.wav) | 11.4 s | -5.8 dBFS |
| 75 | Super Saw Lead | [075-super-saw-lead.wav](Docs/audio/075-super-saw-lead.wav) | 9.4 s | -11.9 dBFS |
| 85 | Evolving Unison Horizon | [085-evolving-unison-horizon.wav](Docs/audio/085-evolving-unison-horizon.wav) | 11.2 s | -10.7 dBFS |
| 105 | Euphoric 16th Trance Arp | [105-euphoric-16th-trance-arp.wav](Docs/audio/105-euphoric-16th-trance-arp.wav) | 9.9 s | -10.4 dBFS |
| 129 | Hard Sync Tear | [129-hard-sync-tear.wav](Docs/audio/129-hard-sync-tear.wav) | 9.0 s | -14.6 dBFS |
| 147 | Desert Storm Saw | [147-desert-storm-saw.wav](Docs/audio/147-desert-storm-saw.wav) | 10.5 s | -9.4 dBFS |

<!-- audio-demos-end -->

Browse [all 160 factory preset demos (MP3)](https://github.com/protocodus/reason-rack-extensions/blob/main/Examples/Trancera/Docs/audio/presets/README.md) in `protocodus/reason-rack-extensions`.

---

## How it works

The JP-8000 is a digital instrument: four Toshiba TC170C140 DSPs compute the voices and effects, and only the stage from the DAC to the jacks is analogue. So the model has two halves:

- **The DSP, in Q23 fixed point at 88.2 kHz.** Inside whatever host rate is active (with exact rational polyphase resampling, zero-resampler-latency passthrough at 88.2 kHz, and automatic true-idle gating), voices run in saturating 24-bit Q23 fixed point (`sat24`), 15-bit split coefficients (`mulSplit15`), 24-bit wrapping phase accumulators, and 16-bit companded delay RAM.
  - **Oscillators & modulation:** 7-saw Super Saw with tracked dual-stage HPF and measured detune curve, Saw SHAPE, Pulse/PWM, Triangle SHAPE (cubic phase skew + Chebyshev T3 wavefolder), Triangle Mod, chaotic-recurrence pitched Noise, monophonic 2-tap Feedback Osc, 4-wave OSC 2 (`Pulse`, `Saw`, `Triangle`, and JP-8080 `Noise`), anti-aliased hard sync, ring mod, and linear through-zero X-Mod.
  - **Filter & Envelopes:** 2-stage saturating Chamberlin SVF (-12 / -24 dB LPF, BPF, HPF), 100 Hz stepped exponential envelopes with 12-level dynamic 88.2 kHz smoothing, dual LFOs with FADE plus host tempo-synced `LFO 1 Sync` (15 divisions `4/1..1/32`) and `LFO 1 Retrig`, 40-destination JP-8080 Velocity Matrix, Filter Velocity, and MIDI Channel Aftertouch (`aftertouch_cutoff`, `aftertouch_lfo2`).
  - **Unison, Arpeggiator & Effects:** JP-8080-style **Polyphonic Unison** (`Mono = Off, Unison = On`, 2 paired voices per note = 4-note polyphony) and **Mono 8-voice Unison** (`Mono = On, Unison = On`) with live detune and equal-power **Stereo Unison Spread**, tempo-synced **Arpeggiator** (`Up`, `Down`, `Up/Down`, `Random`, `As Played`; `1–4` octaves; `1/4–1/32` host-synced rates; `Hold`), saturating Q23 Bass/Treble **Tone Control**, 5 **Delay** types with optional host **Delay Sync** (`Free` and `1/32–1/2` up to 2.97 s), and all 13 **Multi-FX** types (12 dual-tap **Chorus/Flanger/Phaser/Freeze** modes plus JP-8080 **Distortion**).
  - **Glitch-free automation:** Sample-rate parameter smoothing and 256-sample crossfades (with clean mid-fade reversals) across waves, filter modes, effects, pan, DAC bypass, and stereo unison spread (`28–62` samples control pickup at 48 kHz).
- **The output stage, as a circuit (`705.6 kHz`, 8×) or pure digital bypass (`DAC`).** When `DAC` is enabled (default), the output models the 18-bit uPD63200GS truncation, deterministic R-2R ladder DNL (`0.75 LSB`), four 0.16 Hz coupling caps, IC22A buffer pole/zero, Sallen-Key 28.2 kHz low-pass + ~3.6 dB bass shelf, flat JP-8080 Rack follower, and `1.35×` op-amp rail saturation with silent zero gating. When `DAC` is disabled (`Engine::setDacEnabled(false)`), the entire DAC and analog circuit is bypassed cleanly at matched 37-sample latency (`88.2 kHz`) for a flat 24-bit Q23 digital signal (like JE-8086).
- **JP-8000 / JP-8080 SysEx (`Source/DSP/TranceraSysEx.{h,cpp}`).** Roland Model `00 06` DT1 codec supporting single 239B (JP-8000) and 248B flat or 242+6 split (JP-8080) patch dumps, 128-patch `U:A11–U:B88` User Banks, Performance Common / Part Arpeggio + Delay Sync overlays, 40-destination `VelocityMatrix` round-trip, and Standard MIDI File (`.mid`) unwrapping. Use `./build-dsp/TranceraSysEx` or open [`Tools/sysex-converter/index.html`](Tools/sysex-converter/index.html) offline to convert `.syx`/`.mid` &harr; Reason `.repatch`.

The signal path is `OSC 1 + OSC 2 (sync, ring, X-mod) -> filter -> amp -> tone control -> delay -> chorus -> pan -> [DAC -> output circuit | latency-matched digital bypass]`, for up to two parts sharing eight voices (4-voice `Int32x4` / `Double2` SIMD vectorized). `Source/DSP/TranceraControls.h` exposes 72 append-only panel controls.

[The hardware model](Docs/research/hardware-model-2026-11-22.md) documents what is settled by schematic/manual, measured on hardware/black-box, or inferred.

## Known gaps

- Direct real-hardware recordings only verify the supersaw at Detune 0 / Mix 0, the saw at its round end, 12 dB low-pass on `A33 JP-303`, tone EQ tilt, feedback howl tuning, and `P14 Feedback Lead` ([measurements](Docs/research/hardware-ab-2026-11-22.md)). Most fine behaviour is fitted/verified against JE-8086 rather than a physical unit (`AnalogColor` DNL `0.75 LSB` and `1.35×` rail are datasheet estimates).
- Remaining minor black-box deltas vs JE-8086: `Sine Lead` `<300 Hz` (`+3.8 dB` on 100 Hz-aligned pure tone), `Juno Bass Vel` `<300 Hz` (`-2.2 dB`), noise headroom at maximum resonance (`CTRL 2 = 127`), `-24 dB` SVF at resonance 127, and 100 Hz tick phase latency (~11 ms vs ~3 ms) on ultra-fast attacks.
- **Features not yet implemented / exposed:** `KeyMode::Dual` UI and `VelocityMatrix` UI on the Rack front panel, `Split` key mode, MIDI Control Assign matrix, Arp Beat Patterns, RPS / Motion Control, External Audio In / Vocoder, 10-voice option, and native VST3/AU/CLAP targets.

## Version history

| Version | Change |
| --- | --- |
| 0.9.6 | Exposed live modulation output getters on `Engine` (`lfo1Output()`, `lfo2Output()`, `filterEnvOutput()`, `ampEnvOutput()`) powering rear-panel CV outputs and an expanded 12-input / 4-output Rack CV modulation matrix. |
| 0.9.5 | Added JP-8080 OSC 2 `Noise` (`Wave2::Noise`, native `0x21 = 3` in SysEx without fallback warnings), 15-division host tempo-synced `LFO 1 Sync` and `LFO 1 Retrig` (`72` controls, `70` automatable), plus dynamic OSC 1 control labels and machined Cobalt Studio chassis polish on the Rack panel. |
| 0.9.4 | Added JP-8080 Multi-FX `Distortion` (`ChorusType::Distortion`, index `12`, 13-step `chorus_type`) with asymmetric overdrive/pre-HPF/post-LPF/DC blocker, click-free 256-sample crossfades, and native JP-8080 `.syx`/`.mid` ↔ `.repatch` round-trip (`0x3D = 12`). |
| 0.9.3 | Renamed `DAC Colour` to `DAC` with full analog+DAC digital bypass when Off (latency-matched at 37 samples @ 88.2 kHz with clickless 40-tick warmup & 256-sample crossfades), and added canonical JP-8000 / JP-8080 DT1 SysEx codec (`Source/DSP/TranceraSysEx.{h,cpp}`), `TranceraSysEx` CLI, and single-file offline HTML converter (`Tools/sysex-converter/index.html`) for `.syx`/`.mid` ↔ `.repatch` exchange. 30/30 Release & ASan/UBSan test suites passing. |
| 0.9.2 | Tier 1 upgrades: exposed `filter_velocity` (`-64..63`), added MIDI Channel Aftertouch (`aftertouch_cutoff`, `aftertouch_lfo2`), split Unison into JP-8080 Poly Unison (2 paired voices/note) vs Mono 8-voice Unison with live equal-power `unison_spread` (`0..100%`), added host tempo-synced `delay_sync` (`Free`, `1/32..1/2` with 262,144-sample buffer & dynamic idle threshold), updated A/B (18/20 within ≤ 2 dB, 19/20 within ≤ 2.2 dB), 70 controls, 28/28 Release & ASan/UBSan test suites passing. |
| 0.9.1d / 69c39be | Q23 fixed-point synthesis/filter/effects paths (`0a8801f`), 4-voice SIMD + rational polyphase resampler + idle gating (`8e310e6`), glitch-free sample-rate smoothing & 256-sample crossfades across all automatable controls (`09c3364`), Unison + live detune and tempo-synced Arpeggiator (`69c39be`), plus solved Triangle SHAPE wavefolder and all 12 Multi-FX Chorus algorithms. |
| 0.9.1 | `AnalogColor`'s DAC truncation treats a word under one DAC step as zero, so a release tail ends in exact silence instead of a -1 step held through the coupling caps for about 20 s. |
| 0.9.0 | Against fresh JE-8086 captures, 19 of 20 factory patches as played within 3 dB per region (was 8): JP-8080 velocity matrix on firmware curve, LEGATO polyphonic fix, noise -6.5 dB, switchable `AnalogColor` R-2R DAC + op-amp rail. |
| 0.8.0 | Renamed from Hypra to Trancera (owner, 2026-11-23): `trancera` namespace, `Source/DSP/Trancera*`, `TranceraDSP`. |
| 0.7.1 | Compile-time triangle-mod sine table (zero runtime static initialisers for Rack phdsp64). |
| 0.7.0 | Added `Source/DSP/TranceraControls.h` flat control table and `setTune`. |
| 0.6.0–0.6.2 | Measured Q23 Tone Control shelves, delay timing/LPF/DC blocker, Freeze combs, square-law amp sustain & FX levels, Key Follow after `OSC SHIFT`, X-Mod modulation depth. |
| 0.1.0–0.5.0 | Initial proof of concept through full JE-8086 black-box & hardware Super Saw / filter / oscillator / envelope / LFO fitting. |

## Build

```sh
cmake -S . -B build-dsp -DCMAKE_BUILD_TYPE=Release
cmake --build build-dsp --parallel 4
ctest --test-dir build-dsp --output-on-failure
./build-dsp/TranceraRenderDemos deliverables/demos
python3 Tools/panel/render_front.py
```

## Licensing

Source-available under the [PolyForm Noncommercial licence](LICENSE). Roland, JP-8000 and JP-8080 are trademarks of Roland Corporation, used only to name the modelled instrument.
