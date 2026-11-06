"""Generates placeholder "zombie" hit sounds for the mannequin DFPlayer cards.

Writes sd-card/01/001.mp3 .. 003.mp3: short synthesized growls (no third-party
audio, so they are safe to ship and test with). Replace them with real sound
effects any time; the rules for names and length are in sd-card/README.md.
Requires ffmpeg on PATH.

    python3 tools/make_test_hit_sounds.py
"""

import math
import random
import shutil
import struct
import subprocess
import tempfile
import wave
from pathlib import Path

RATE = 44100

# (length s, start pitch Hz, end pitch Hz, rasp Hz, noise amount, seed)
VARIANTS = [
    (0.85, 115, 70, 31, 0.25, 1),   # short groan, falling
    (1.10, 85, 120, 24, 0.35, 2),   # rising "uuurgh"
    (0.60, 140, 90, 38, 0.45, 3),   # quick snarl
]


def growl(length_s, f_start, f_end, rasp_hz, noise, seed):
    rnd = random.Random(seed)
    n = int(length_s * RATE)
    attack = int(0.025 * RATE)
    out = []
    phase = 0.0
    jitter = 0.0
    lp = 0.0
    for i in range(n):
        t = i / n
        # Pitch glides between the two ends with a slow random wobble (vocal jitter).
        jitter = 0.995 * jitter + 0.005 * rnd.uniform(-1, 1)
        freq = (f_start + (f_end - f_start) * t) * (1 + 0.08 * jitter * 20)
        phase = (phase + freq / RATE) % 1.0
        saw = 2 * phase - 1
        # Amplitude flutter at rasp_hz gives the throaty "vocal fry" texture.
        flutter = 0.65 + 0.35 * math.sin(2 * math.pi * rasp_hz * i / RATE + rnd.uniform(-0.3, 0.3))
        breath = rnd.uniform(-1, 1) * noise
        # One-pole low-pass keeps it dark, like a voice through a closed mouth.
        lp += 0.18 * ((saw * flutter + breath) - lp)
        env = min(1.0, i / attack) * (1 - t) ** 1.4
        out.append(lp * env)
    peak = max(abs(s) for s in out) or 1.0
    return [s / peak * 0.9 for s in out]


def main():
    if shutil.which("ffmpeg") is None:
        raise SystemExit("ffmpeg not found")
    folder = Path(__file__).resolve().parent.parent / "sd-card" / "01"
    folder.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory() as tmp:
        for index, variant in enumerate(VARIANTS, start=1):
            wav = Path(tmp) / f"{index:03d}.wav"
            with wave.open(str(wav), "wb") as w:
                w.setnchannels(1)
                w.setsampwidth(2)
                w.setframerate(RATE)
                w.writeframes(b"".join(struct.pack("<h", int(s * 32000)) for s in growl(*variant)))
            out = folder / f"{index:03d}.mp3"
            subprocess.run(
                [
                    "ffmpeg", "-y", "-loglevel", "error", "-i", str(wav),
                    # Formant-ish band and gentle drive so it reads as a voice on a small speaker.
                    "-af", "highpass=f=90,equalizer=f=650:t=q:w=1.2:g=8,equalizer=f=1800:t=q:w=2:g=4,"
                           "acompressor=threshold=-18dB:ratio=4:attack=5:release=60,volume=6dB,alimiter=limit=0.95",
                    "-ac", "1", "-ar", "44100", "-b:a", "128k", str(out),
                ],
                check=True,
            )
            print(f"wrote {out.relative_to(folder.parent.parent)} ({out.stat().st_size} bytes)")


if __name__ == "__main__":
    main()
