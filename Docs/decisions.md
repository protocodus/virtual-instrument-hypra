# Decisions

## Settled by measurement

### 2026-11-22: the main output's pole

- **A:** the JP-8000 circuit exactly as printed, with C28/C29 = 10000p on the
  volume follower (a pole at 1.06 kHz, -19.5 dB at 10 kHz).
- **B:** the same circuit with the JP-8080's follower, which has no such
  capacitor and is flat. The default, and the recommendation.

**B, by measurement on 2026-11-22, not by ear.** The owner supplied
recordings of a JP-8080 and a JP-8000 (Freesound 275434 and 275439). A fails
on both in amplitude and in phase; see
`Docs/research/hardware-ab-2026-11-22.md`. The JP-8000 file is processed, so
for that model this excludes the printed pole as heard in that file and does
not explain the marking.
