#!/usr/bin/env python3
"""Synthesize the placeholder sound effects and music loops.

usage: make_audio.py <out_dir>

Writes 16-bit mono 22050 Hz WAV files named as in
platform/common/audio_names.c: one per sound event (SFX_* in core/player.h)
and the music loops music_forest (Am-F-C-G, 120 bpm, 8 bars) and
music_cave (slow minor drone, 90 bpm, 8 bars). Output is deterministic
(fixed noise seed). Placeholder until real audio exists (game design §8).
"""
import math
import os
import random
import struct
import sys
import wave

RATE = 22050


def square(f, t, duty=0.5):
    return 1.0 if (f * t) % 1.0 < duty else -1.0


def tri(f, t):
    p = (f * t) % 1.0
    return 4 * p - 1 if p < 0.5 else 3 - 4 * p


def env(i, n, attack=0.005, release=0.3):
    t = i / RATE
    a = min(1.0, t / attack) if attack else 1.0
    r = min(1.0, (n - i) / (release * n)) if release else 1.0
    return a * r


def sweep(f0, f1, dur, wave_fn=square, vol=0.5, duty=0.5):
    n = int(dur * RATE)
    out, phase = [], 0.0
    for i in range(n):
        f = f0 + (f1 - f0) * i / n
        phase += f / RATE
        v = square(1, phase, duty) if wave_fn is square else math.sin(2 * math.pi * phase)
        out.append(v * vol * env(i, n))
    return out


def noise(dur, vol=0.5, smooth0=0.2, smooth1=0.9, rng=None):
    rng = rng or random.Random(1)
    n = int(dur * RATE)
    out, y = [], 0.0
    for i in range(n):
        k = smooth0 + (smooth1 - smooth0) * i / n      # low-pass amount
        y = y * k + rng.uniform(-1, 1) * (1 - k)
        out.append(y * vol * 3 * env(i, n))
    return out


def notes(seq, dur, vol=0.4, duty=0.25):
    out = []
    for f in seq:
        out += sweep(f, f, dur, square, vol, duty)
    return out


def mix(*tracks):
    n = max(len(t) for t in tracks)
    return [sum(t[i] for t in tracks if i < len(t)) for i in range(n)]


def midi(n):
    return 440.0 * 2 ** ((n - 69) / 12)


SFX = {
    "jump": lambda: sweep(300, 620, 0.12, vol=0.35),
    "djump": lambda: sweep(460, 920, 0.12, vol=0.35, duty=0.25),
    "dash": lambda: noise(0.16, 0.5, 0.1, 0.8),
    "land": lambda: noise(0.06, 0.6, 0.85, 0.95),
    "attack": lambda: noise(0.08, 0.45, 0.0, 0.5),
    "hit": lambda: mix(sweep(220, 160, 0.09, vol=0.4), noise(0.06, 0.3, 0.3, 0.6)),
    "kill": lambda: sweep(620, 90, 0.26, vol=0.4),
    "hurt": lambda: [v * (0.6 + 0.4 * math.sin(i / 120)) for i, v in enumerate(sweep(420, 140, 0.3, vol=0.45))],
    "pogo": lambda: sweep(520, 1040, 0.1, vol=0.35, duty=0.25),
    "pad": lambda: sweep(200, 820, 0.25, wave_fn=None, vol=0.5),
    "pickup": lambda: notes([660, 880, 1320], 0.07, 0.35),
    "save": lambda: notes([midi(72), midi(76), midi(79), midi(84)], 0.12, 0.35, 0.5),
    "shield": lambda: mix(noise(0.2, 0.35, 0.0, 0.4), sweep(1400, 700, 0.2, wave_fn=None, vol=0.25)),
    "menu": lambda: sweep(1000, 1000, 0.04, vol=0.3),
    "room": lambda: noise(0.2, 0.25, 0.9, 0.98),
}


def song(bpm, bars, chords, lead_fn, hats=True, bass_oct=36):
    beat = 60.0 / bpm
    n = int(bars * 4 * beat * RATE)
    out = [0.0] * n
    rng = random.Random(7)
    for i in range(n):
        t = i / RATE
        b = t / beat                        # beats
        bar = int(b // 4) % len(chords)
        root = chords[bar]
        # Bass: triangle on beats, root then fifth.
        bn = root + bass_oct + (7 if int(b) % 2 else 0)
        bt = b % 1.0
        v = 0.30 * tri(midi(bn), t) * max(0.0, 1 - bt * 0.8)
        # Lead / arpeggio.
        v += lead_fn(b, root, t)
        # Hi-hat on off-beats.
        if hats and (b * 2) % 2 >= 1 and (b * 2) % 1 < 0.08:
            v += rng.uniform(-1, 1) * 0.08
        out[i] = v
    return out


def forest_lead(b, root, t):
    arp = [0, 3, 7, 12, 7, 3, 0, 7]         # minor-ish arpeggio over each chord
    step = int(b * 2) % 8
    major = root in (8, 3, 10)              # F, C, G are major chords
    iv = arp[step] + (1 if major and arp[step] in (3,) else 0)
    f = midi(57 + root + iv)
    frac = (b * 2) % 1.0
    return 0.12 * square(f, t, 0.25) * max(0.0, 1 - frac * 1.2)


def cave_lead(b, root, t):
    v = 0.10 * math.sin(2 * math.pi * midi(45 + root) * t) * (0.6 + 0.4 * math.sin(t * 0.8))
    if int(b) % 4 == 0 and b % 1.0 < 0.5:
        v += 0.10 * math.sin(2 * math.pi * midi(69 + root + (7 if int(b // 4) % 2 else 3)) * t) * (1 - (b % 1.0) * 2)
    return v


def write(path, samples):
    peak = max(1e-6, max(abs(s) for s in samples))
    scale = 0.9 / peak if peak > 0.9 else 1.0
    with wave.open(path, "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes(b"".join(struct.pack("<h", int(max(-1, min(1, s * scale)) * 32767)) for s in samples))


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    for name, fn in SFX.items():
        write(os.path.join(out, name + ".wav"), fn())
    # Am F C G (semitones above A)
    write(os.path.join(out, "music_forest.wav"), song(120, 8, [0, 8, 3, 10], forest_lead))
    write(os.path.join(out, "music_cave.wav"), song(90, 8, [0, 0, 5, 3], cave_lead, hats=False, bass_oct=24))
    print(f"wrote {len(SFX) + 2} sounds to {out}")


if __name__ == "__main__":
    main()
