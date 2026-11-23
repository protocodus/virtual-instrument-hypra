# Working conventions

Trancera (named by the owner on 2026-11-23; Hypra before) is a proof of
concept that follows the Protocodus instrument layout: this repository owns the
DSP in `Source/DSP/`, and the Rack Extension (`Examples/Trancera` in the Rack
repository) is a thin adapter over it.

- The owner asked for a push on 2026-11-23. The remote is
  `protocodus/virtual-instrument-trancera`, public on GitHub like the
  portfolio's other instrument repositories (Hypra until 2026-11-23). Push
  `main` only, never other branches.
- `Source/DSP/` is JUCE-free C++17. No allocation, file I/O, locks or
  exceptions on the audio thread.
- Not clean-room since 2026-11-23: the owner accepted the risk and asked
  for the JE-8086 emulator (GPL) and the JP-8000 firmware it runs to be
  analysed. Keep gearmulator source, ROM images, firmware dumps and
  disassemblies outside this repository; write Trancera's own code from what
  they show, and tag such constants `[firmware]` with the routine they
  came from. Anything copied verbatim from gearmulator brings the GPL with
  it, so do not paste its code here.
- Tag every constant `[settled]`, `[measured]`, `[firmware]` or `[inferred]` beside the
  code that uses it, and keep `Docs/research/hardware-model-2026-11-22.md`
  in step.
- A change is done when `ctest --test-dir build-dsp` passes in a Release
  build. Never loosen a test to get green.
- A sound decision made by ear goes in `Docs/decisions.md` as chosen by ear.
- Demos render to the ignored `deliverables/`. Never commit them.
