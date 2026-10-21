#!/usr/bin/env python3
"""Generate the launcher's UI sounds ("Glass" style: FM bells and a small room).

Usage: make_sfx.py [OUT_DIR]   (default: launcher/sfx next to this repo's art)
Writes OUT_DIR/<event>.ogg. Needs numpy, scipy and ffmpeg with libvorbis.
Deterministic: the noise is seeded, so a rerun gives the same sounds.
"""
import os
import subprocess
import sys

import numpy as np
from scipy.signal import butter, fftconvolve, lfilter, sosfilt

SR = 48000
HERE = os.path.dirname(os.path.abspath(__file__))
OUT = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "..", "..", "sfx")
RNG = np.random.default_rng(1234)


def note(name):
    names = {"C": -9, "C#": -8, "D": -7, "D#": -6, "E": -5, "F": -4, "F#": -3,
             "G": -2, "G#": -1, "A": 0, "A#": 1, "B": 2}
    pitch, octave = name[:-1], int(name[-1])
    return 440.0 * 2 ** ((names[pitch] + 12 * (octave - 4)) / 12)


def t_axis(dur):
    return np.arange(int(dur * SR)) / SR


def env(dur, attack=0.003, decay=0.1, hold=0.0):
    t = t_axis(dur)
    a = np.clip(t / attack, 0, 1)
    d = np.exp(-np.maximum(t - attack - hold, 0) / decay)
    return a * d


def lowpass(x, hz, order=2):
    return sosfilt(butter(order, hz, "low", fs=SR, output="sos"), x)


def highpass(x, hz, order=2):
    return sosfilt(butter(order, hz, "high", fs=SR, output="sos"), x)


def bandpass(x, lo, hi, order=2):
    return sosfilt(butter(order, [lo, hi], "band", fs=SR, output="sos"), x)


def place(parts, total=None):
    """Mix (offset_seconds, signal) pairs into one buffer."""
    end = max(int(o * SR) + len(s) for o, s in parts)
    out = np.zeros(max(end, int((total or 0) * SR)))
    for o, s in parts:
        i = int(o * SR)
        s = s.copy()
        f = min(int(0.015 * SR), len(s) // 4)
        s[-f:] *= np.linspace(1, 0, f)  # notes never end on a hard cut
        out[i:i + len(s)] += s
    return out


_IR = {}


def reverb(x, mix=0.15, length=0.5, tone=5000):
    """Small room: convolve with decaying filtered noise, separate L/R tails."""
    key = (length, tone)
    if key not in _IR:
        n = int(length * SR)
        t = np.arange(n) / SR
        irs = []
        for _ in range(2):
            ir = RNG.standard_normal(n) * np.exp(-t / (length / 5))
            ir = lowpass(ir, tone)
            ir[: int(0.008 * SR)] = 0  # pre-delay
            irs.append(ir / np.sqrt(np.sum(ir ** 2)))
        _IR[key] = irs
    pad = np.concatenate([x, np.zeros(int(length * SR))])
    wet = [fftconvolve(pad, ir)[: len(pad)] for ir in _IR[key]]
    return np.stack([pad + mix * wet[0], pad + mix * wet[1]], axis=1)


def finish(x, peak_db, name, mix=0.15, room=0.5):
    if x.ndim == 1:
        x = reverb(x, mix, room) if mix > 0 else np.stack([x, x], axis=1)
    x = x - np.mean(x, axis=0)  # remove DC
    # Trim silent tail, then short fades so nothing clicks.
    level = np.max(np.abs(x), axis=1)
    last = np.nonzero(level > np.max(level) * 10 ** (-60 / 20))[0][-1]
    x = x[: last + 1]
    fi, fo = int(0.001 * SR), min(int(0.02 * SR), len(x) // 3)
    x[:fi] *= np.linspace(0, 1, fi)[:, None]
    x[-fo:] *= np.linspace(1, 0, fo)[:, None]
    x *= 10 ** (peak_db / 20) / np.max(np.abs(x))
    os.makedirs(OUT, exist_ok=True)
    pcm = (x * 32767).astype(np.int16).tobytes()
    subprocess.run(["ffmpeg", "-loglevel", "error", "-y", "-f", "s16le", "-ar", str(SR), "-ac", "2",
                    "-i", "pipe:0", "-c:a", "libvorbis", "-q:a", "5", "-map_metadata", "-1",
                    "-fflags", "+bitexact", "-flags:a", "+bitexact", f"{OUT}/{name}.ogg"],
                   input=pcm, check=True)


def whoosh(dur, lo, hi, rise=True, amp=0.3):
    """Filtered noise whose band sweeps up (or down) over its length."""
    n = int(dur * SR)
    noise = RNG.standard_normal(n)
    out = np.zeros(n)
    steps = 24
    for k in range(steps):
        f = k / (steps - 1)
        f = f if rise else 1 - f
        c = lo * (hi / lo) ** f
        seg = slice(k * n // steps, (k + 1) * n // steps)
        out[seg] = bandpass(noise, c * 0.7, min(c * 1.4, SR / 2 - 100))[seg]
    shape = np.sin(np.linspace(0, np.pi, n)) ** 2
    return amp * out * shape


# ---------------------------------------------------------------- glass (FM bell)
def bell(freq, dur=0.4, decay=0.15, ratio=3.5, index=1.2, idecay=0.05, amp=1.0):
    t = t_axis(dur)
    i_env = index * np.exp(-t / idecay)
    mod = i_env * np.sin(2 * np.pi * freq * ratio * t)
    body = np.sin(2 * np.pi * freq * t + mod)
    body += 0.25 * np.sin(2 * np.pi * freq * 2.001 * t) * np.exp(-t / (decay * 0.5))
    return amp * body * env(dur, 0.002, decay)


def glass():
    finish(lowpass(place([(0, bell(note("E6"), 0.12, 0.025, 3.0, 0.8, 0.01))]), 7000), -16, "move", 0.08)
    finish(place([(0, bell(note("E5"), 0.4, 0.12)), (0.055, bell(note("B5"), 0.5, 0.18))]),
           -8, "select")
    finish(lowpass(place([(0, bell(note("B5"), 0.3, 0.08, index=0.7)),
                          (0.05, bell(note("E5"), 0.4, 0.12, index=0.7))]), 4000), -10, "back")
    finish(place([(0, whoosh(0.18, 800, 5000, amp=0.15)),
                  (0.02, bell(note("E5"), 0.3, 0.08, amp=0.7)),
                  (0.06, bell(note("G#5"), 0.3, 0.08, amp=0.7)),
                  (0.10, bell(note("B5"), 0.5, 0.16, amp=0.8))]), -9, "open")
    finish(lowpass(place([(0, whoosh(0.15, 4000, 700, rise=False, amp=0.15)),
                          (0.0, bell(note("B5"), 0.25, 0.06, amp=0.7)),
                          (0.05, bell(note("E5"), 0.35, 0.10, amp=0.8))]), 5000), -11, "close")
    err = lambda: (bell(note("A3"), 0.18, 0.06, 1.41, 3.0, 0.04)
                   + bell(note("D#4"), 0.18, 0.06, 1.41, 3.0, 0.04))
    finish(lowpass(place([(0, err()), (0.11, err())]), 3000), -10, "error", 0.06)
    chord = [note(n) for n in ("E4", "B4", "E5", "G#5", "B5")]
    parts = [(0, whoosh(0.45, 300, 6000, amp=0.25))]
    parts += [(0.30 + 0.025 * k, bell(f, 1.6, 0.6, 3.5, 1.0, 0.08, 0.6)) for k, f in enumerate(chord)]
    finish(place(parts), -6, "launch", 0.25, 1.2)
    finish(place([(0, bell(note("A5"), 0.2, 0.05, index=0.6))]), -13, "toggle", 0.08)


if __name__ == "__main__":
    glass()
