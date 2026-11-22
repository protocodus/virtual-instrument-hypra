# Working conventions

Hypra is a local proof of concept (working name) that follows the Protocodus
instrument layout: this repository owns the DSP in `Source/DSP/`, and a later
Rack Extension will be a thin adapter over it.

- The owner has not approved a GitHub repository. Do not add a remote, push
  or publish until they do.
- `Source/DSP/` is JUCE-free C++17. No allocation, file I/O, locks or
  exceptions on the audio thread.
- Original code only. Do not read or copy Roland firmware, disassemblies, or
  emulators and write-ups that reproduce firmware code or tables. Manuals,
  service notes, recordings and black-box measurements are the evidence.
- Tag every constant `[settled]`, `[measured]` or `[inferred]` beside the
  code that uses it, and keep `Docs/research/hardware-model-2026-11-22.md`
  in step.
- A change is done when `ctest --test-dir build-dsp` passes in a Release
  build. Never loosen a test to get green.
- A sound decision made by ear goes in `Docs/decisions.md` as chosen by ear.
- Demos render to the ignored `deliverables/`. Never commit them.
