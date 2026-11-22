#!/usr/bin/env python3
"""A/B a recording of the hardware against a Hypra render of the same phrase.

  python3 -I Tools/compare_reference.py reference.{wav,mp3,ogg} model.wav outdir

Writes A.wav (the recording) and B.wav (Hypra) at 48 kHz / 24 bit, mono,
aligned within 0.5 s and matched on RMS by turning the louder one down, with
report.json holding the trims and the third-octave differences.

Level and long-term spectrum only: a small difference is necessary for a
likeness and does not prove one. A lossy reference says nothing reliable
above about 15 kHz. References and pairs are never committed.
"""
import json
import subprocess
import sys
from pathlib import Path

import numpy as np

RATE, FFT = 48000, 16384


def load(path):
    raw = subprocess.run(
        ['ffmpeg', '-v', 'error', '-protocol_whitelist', 'file,pipe', '-i', str(Path(path).resolve()),
         '-ac', '1', '-ar', str(RATE), '-f', 'f32le', '-'],
        check=True, capture_output=True).stdout
    return np.frombuffer(raw, '<f4').astype(float)


def align(a, b):
    """Trim both to where their 5 ms loudness envelopes line up best."""
    hop = RATE // 200
    def envelope(x):
        n = len(x) // hop
        e = np.sqrt((x[:n * hop].reshape(n, hop) ** 2).mean(1))
        return e - e.mean()
    score = np.correlate(envelope(a), envelope(b), 'full')
    lags = np.arange(len(score)) - (len(envelope(b)) - 1)
    near = abs(lags) <= 100
    lag = int(lags[near][np.argmax(score[near])]) * hop
    a, b = a[max(0, lag):], b[max(0, -lag):]
    n = min(len(a), len(b))
    return a[:n], b[:n], lag


def bands(x):
    if len(x) < FFT:
        x = np.pad(x, (0, FFT - len(x)))
    window = np.hanning(FFT)
    power = sum(abs(np.fft.rfft(x[i:i + FFT] * window)) ** 2 for i in range(0, len(x) - FFT + 1, FFT // 2))
    hz = np.fft.rfftfreq(FFT, 1 / RATE)
    out = {}
    for centre in 1000 * 2 ** (np.arange(-12, 14) / 3):
        chosen = (hz >= centre * 2 ** (-1 / 6)) & (hz < centre * 2 ** (1 / 6))
        out[round(float(centre))] = 10 * np.log10(power[chosen].sum() + 1e-20)
    return out


def main():
    if len(sys.argv) != 4:
        sys.exit(__doc__)
    reference, model, lag = align(load(sys.argv[1]), load(sys.argv[2]))
    levels = [np.sqrt(np.mean(x ** 2)) for x in (reference, model)]
    target = min(levels)
    trims = [target / level for level in levels]
    pair = [x * trim for x, trim in zip((reference, model), trims)]
    out = Path(sys.argv[3])
    out.mkdir(parents=True, exist_ok=True)
    for letter, x in zip('AB', pair):
        subprocess.run(
            ['ffmpeg', '-v', 'error', '-y', '-f', 'f32le', '-ar', str(RATE), '-ac', '1', '-i', '-',
             '-c:a', 'pcm_s24le', str((out / f'{letter}.wav').resolve())],
            input=x.astype('<f4').tobytes(), check=True)
    a, b = bands(pair[0]), bands(pair[1])
    top = max(a.values())
    heard = [k for k in a if a[k] > top - 60]
    report = {'A': 'hardware recording', 'B': 'Hypra', 'seconds': len(reference) / RATE,
              'lag_samples': lag, 'trim_db': [round(20 * float(np.log10(t)), 2) for t in trims],
              'b_minus_a_db': {k: round(float(b[k] - a[k]), 2) for k in heard}}
    (out / 'report.json').write_text(json.dumps(report, indent=1) + '\n')
    print(json.dumps(report))


if __name__ == '__main__':
    main()
