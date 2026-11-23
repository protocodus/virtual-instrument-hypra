# Handoff: matching Trancera to the JP-8000 through firmware analysis

Written 2026-11-23 for the next agent (human or LLM) who picks this up.
Read `CLAUDE.md`, `README.md`, `Docs/research/black-box-je8086-2026-11-22.md`
and `Docs/research/hardware-model-2026-11-22.md` first.

## Goal and permission

Make Trancera (this repo, a JUCE-free C++17 JP-8000 engine, 0.6.0) match the
Roland JP-8000 as closely as possible, by changing Trancera's own DSP model.
Do not use samples or recordings inside Trancera.

On 2026-11-23 the owner accepted the legal risk and asked for the JE-8086
emulator (gearmulator, GPL) and the JP-8000 firmware it runs to be
analysed. The repo is therefore no longer clean-room. The rules in
`CLAUDE.md` still apply:

- Keep gearmulator source, ROM images, DSP dumps and disassembly listings
  **outside** this repository.
- Write Trancera's own code from what the analysis shows. Never paste
  gearmulator code here; that would bring the GPL with it.
- Tag every constant taken from the firmware `[firmware]`, naming the chip,
  core and words (or routine) it came from.

## Where Trancera stands

Every law already fitted to JE-8086 black-box captures is listed in
`Docs/research/black-box-je8086-2026-11-22.md` (rounds 1 to 5) with its
measured gap. Most behaviour matches within 1-2 %. The open gaps, in order
of how much they matter to the sound:

1. **Triangle SHAPE above 0.** The emulator skews the wave (its peaks move
   closer together, 19/48 of a cycle at 32) as well as sharpening it, and
   folds it above 40. Two candidate models failed (sine of a cubic; sine of
   a powered unipolar triangle). Captures: `tri_000` ... `tri_127` in
   `cap2.npz` (see below).
2. **Moving chorus and flanger types** (SUPER CHORUS SLW/MID/FST/CLR,
   FLANGER x3, DEEP/JET PHASING, TWISTING). Rates, depths, tap counts and
   feedback are guesses (`Source/DSP/Effects.h`, `presets`). Two black-box
   methods failed; they were validated against Trancera's own known rates and
   did not recover them.
3. **Feedback oscillator above the howl.** The harmonic and loop gain match;
   the emulator's howl carries a square's odd harmonics that Trancera's
   band-passed loop does not.
4. Smaller: release and decay below 10 (two-stage in the emulator), the
   velocity matrix, LFO 1 to X-MOD/OSC 2, PWM, sync, the delay's tone, and
   the noise filter's damping at RESONANCE 0.

## Tools in this repo

- `Tools/blackbox/`: the black-box harness used for rounds 1-5.
  - `jh.py` hosts `/Library/Audio/Plug-Ins/VST3/JE8086.vst3` with pedalboard,
    opens its editor (parameter edits only apply while its UI loop runs)
    and plays it in real time. `capture(jobs, path, ccs)` saves the left
    channel, or stereo for job names starting `st_`. Set `jh.FORCE = True`
    so every control in a job is re-sent (see pitfalls).
  - `run.sh script.py` runs a script under uv with pinned packages. Run it
    unsandboxed: it needs the plugin, the ROM folder and pypi.
  - `an.py`, `an3.py`: harmonic, pitch, envelope, cutoff-meter helpers.
  - `cap*.py`: the capture plans of each round. `cmp3.py`, `cmp4.py`:
    model-against-capture checks. `ab*.py`: A/B file builders.
  - Captures (`cap*.npz`) are not in the repo. They lived in a session
    scratchpad and may be gone; re-run the `cap*.py` plans (minutes each).
- `Tools/RenderEvents.cpp` (`build-dsp/TranceraRenderEvents`): renders Trancera
  from a timed event list; parameters, `mod`, `node digital`, `stereo`.
- `Tools/firmware-analysis/`:
  - `jeProbe.cpp`, `CMakeLists.txt`: a tool that links gearmulator's
    `jeLib`, boots the JP-8000 from the ROM (about 3 s), runs a script and
    dumps the DSP programs.
  - `espdis.py`: disassembler for those dumps.
  - `example-control-map.txt`: the script that produced the control map
    below.

## Rebuilding the firmware setup

The previous run lives in `~/Dev/je8086-analysis` (src, build, work,
tools); rebuild it if it is gone.

1. `git clone --depth 1 https://github.com/dsp56300/gearmulator.git src`
   (analysed at commit `00e1b17fe32711c25c4417eea6400c34b32c4ab7`).
2. `git submodule update --init --recursive --depth 1 source/cpu/dsp56300
   source/cpu/mc68k source/3rdparty/freetype source/3rdparty/lunasvg
   source/3rdparty/RmlUi source/3rdparty/cpp-terminal
   source/3rdparty/portmidi-latest` (JUCE is not needed).
3. Copy `Tools/firmware-analysis/jeProbe.cpp` and `CMakeLists.txt` to
   `src/source/ronaldo/je8086/jeProbe/` and add `add_subdirectory(jeProbe)`
   to `src/source/ronaldo/je8086/CMakeLists.txt`.
4. Configure without the plugins:
   `cmake -S src -B build -DCMAKE_BUILD_TYPE=Release
   -Dgearmulator_BUILD_JUCEPLUGIN=OFF -Dgearmulator_BUILD_JUCEPLUGIN_CLAP=OFF
   -Dgearmulator_SYNTH_OSIRUS=OFF -Dgearmulator_SYNTH_OSTIRUS=OFF
   -Dgearmulator_SYNTH_VAVRA=OFF -Dgearmulator_SYNTH_XENIA=OFF
   -Dgearmulator_SYNTH_NODALRED2X=OFF -Dgearmulator_SYNTH_88EMU=OFF
   -Dgearmulator_COMPONENT_DSPBRIDGE=OFF -Dgearmulator_BUILD_INSTALLER=OFF`,
   then `cmake --build build -j10 --target jeProbe`.
5. ROM: the owner's eight JP-8000 v1.05 update files `_00001.MID` ...
   `_00008.MID` are in `~/Documents/The Usual Suspects/JE8086/roms/`. Copy
   them to a work folder and run the probe from there; it finds the ROM in
   its working directory.
6. `jeProbe script.txt outdir`. Script lines: `wait SECONDS`,
   `patch HEXOFFSET VALUE` (upper patch, SysEx DT1 to 01 00 40 xx),
   `perf HEXOFFSET VALUE` (performance common, 01 00 00 xx), `on NOTE VEL`,
   `off NOTE`, `dump NAME`. Start with `wait 8`, `perf 10 0` (SINGLE) and
   `perf 17 0` (arpeggiator off). Patch offsets are in gearmulator's
   `source/ronaldo/je8086/jeLib/jemiditypes.h` (`enum class Patch`).
7. A dump is 4 chips x 2 cores x 1024 little-endian 32-bit program words.
   `python3 espdis.py dump.bin CHIP CORE [from] [to]` lists them.

## What the analysis found

- The sound runs on four Roland ESP (TC170C140) DSPs, two cores each, at
  768 clocks a sample (88.2 kHz). The H8 main CPU loads the microcode from
  the ROM and then rewrites coefficient bytes inside program words as
  controls move. Every host write lands in program words (modes: whole
  word, 10-bit coefficient, ERAM field).
- Word format (from `custom_chips/esp/esp_common.h`): opcode = bits 22:18,
  operand m = bits 17:10 (1-4 select constants 0x10, 0x400, 0x10000,
  0x400000), shift select = bits 9:8 (shifts 7, 6, 5, 3), signed coefficient
  = bits 7:0, ERAM field = bits 27:23 on core 1. A coefficient is
  `coef * 2^(7 - shift) / 128`. A `mac` followed by `spec m=c6/e6 sub=6`
  is a double-precision pair: the second word's byte holds the low bits.
- Control map (mono, one held note, values 20 and 100). Chip.core.word:
  - X-MOD: 0.1.0-1. Balance: chips 0-2, words 48-69 (one pair per voice).
    OSC LFO1 depth: 0.1.28-30. OSC2 fine: 0.0.25-26.
  - Filter: CUTOFF 2.1.0-1; RESONANCE 2.1.0, 3, 4; filter LFO1 and env
    depth 2.1.0-4. Voices sit 48 words apart on chip 2 core 1. The H8
    combines cutoff, key follow, envelope and LFO into one final value per
    voice. Over CUTOFF 0..127 the pair 2.1.0-1 reads 0x0012/0x00 at 64 and
    saturates at 0x7f/0x7f from about 112; the mapping from it to the
    filter's real coefficient is not decoded yet.
  - Amp level, amp LFO1, amp env attack/decay, resonance: 3.0.138-139.
  - Tone: BASS 3.1.10-13, TREBLE 3.1.5, 7, 14-17. Delay time 3.1.27-36,
    feedback 3.1.21-22, level 3.1.24-25, effects level 3.1.141-142.
  - Always moving (LFO phases or similar): 0.1.119-120, 3.0.300-301,
    3.0.315-316.
  - Oscillator SHAPE, PWM, waveform, LFO rates, envelope times and
    portamento did **not** change any program word between values 20 and
    100 in this setup. Either they act through IRAM/GRAM/ERAM data the H8
    reaches another way, or they are computed on the H8 and only reach the
    DSP as live per-voice values, or the test missed them (see pitfalls).
  - MULTI-FX TYPE (0x3d) has only 13 values; the 20/100 test clamped both
    and showed nothing. Re-test with 0..12.

## Suggested plan

1. **Confirm the oscillator path.** Instrument `esp::Base::hostWrite` (in
   your gearmulator checkout, not here) to log chip, address, mode and value
   with a timestamp. Sweep OSC 1 CTRL 1 with one held mono note and see
   what the H8 writes. Also dump `runtime().core[c].iram`, GRAM and ERAM
   next to the program words.
2. **Signal flow.** Each chip's audio goes to the next through GRAM routes
   (`je8086devices.h`, `m_routes`); the output is the last chip's GRAM
   0xe8/0xec. Disassemble chip by chip, starting from the output, and label
   the IRAM slots each section reads and writes.
3. **Triangle SHAPE** (gap 1). Find the oscillator code on chips 0-1 that
   reads the SHAPE value (once step 1 shows where it lands) and port its
   arithmetic into `osc::shapedTriangle` in `Source/DSP/Oscillators.h`.
   Check it against the `tri_*` captures with `Tools/blackbox`.
4. **Chorus types** (gap 2). Dump chip 3 core 1 for each MULTI-FX TYPE 0..12,
   diff the words, and read the modulation section (delay taps, LFO step
   coefficients, feedback). Replace the guesses in `Effects.h` `presets`.
5. **Cutoff pair decoding**. Optional: the black-box law already matches
   within 1 %. Decode it only to confirm.
6. After each change: tests first (`Tests/EngineTests.cpp`, add a suite to
   the `foreach` in `CMakeLists.txt`), `ctest --test-dir build-dsp`, a
   sanitizer and an x86_64 build (see the commands in the black-box doc
   history), re-render demos to `deliverables/demos-<version>`, build an A/B
   file with `Tools/blackbox/ab*.py`, bump the version in `CMakeLists.txt`
   and `README.md`, and record the finding in the black-box doc.

## Pitfalls already hit

- JE-8086 ignores host parameter edits unless its editor is open and the
  host runs in real time; `jh.session` handles both.
- An OSC 1 control sent in the same patch change as a new waveform can
  land on the old waveform. Use `jh.FORCE = True`.
- The first note after a patch with SUSTAIN 0 is sometimes dropped; play a
  throwaway note first.
- Patch defaults matter: Trancera's oscillator SHAPE default is 0, as on
  JE-8086. Always set `osc1.ctrl1` explicitly in A/B renders.
- `TranceraRenderEvents` needs events sorted by time.
- In the probe, voices rotate unless MONO is on (`patch 47 1`); use two
  dumps of an unchanged state to find the always-moving words.
- Enumerated controls clamp out-of-range values; test them with their real
  range.
- The machine's disk is nearly full (about 18 GB free when this was
  written); the gearmulator clone and build take about 1 GB.
