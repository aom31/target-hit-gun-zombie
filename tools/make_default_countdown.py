"""Generates the default phone countdown sound, web/sounds/countdown.mp3.

Shot-timer style: a short beep on each count (5, 4, 3, 2, 1) and the long,
high start beep exactly 5 s after the tap, when the game clock starts.
Replace web/sounds/countdown.mp3 with your own file of the same length to
customise it (see NOTES.md). Requires ffmpeg on PATH.

    python3 tools/make_default_countdown.py
"""

import math
import shutil
import struct
import subprocess
import tempfile
import wave
from pathlib import Path

RATE = 22050
COUNT_BEEPS_S = [0.0, 1.0, 2.0, 3.0, 4.0]
START_BEEP_S = 5.0
LENGTH_S = 5.8


def tone(samples, start_s, length_s, freq, volume):
    start = int(start_s * RATE)
    n = int(length_s * RATE)
    fade = int(0.005 * RATE)
    for i in range(n):
        env = min(1.0, i / fade, (n - i) / fade)
        # A little third harmonic makes it sound like a shot-timer piezo.
        s = math.sin(2 * math.pi * freq * i / RATE) + 0.25 * math.sin(6 * math.pi * freq * i / RATE)
        samples[start + i] += volume * env * s


def main():
    out = Path(__file__).resolve().parent.parent / "web" / "sounds" / "countdown.mp3"
    out.parent.mkdir(parents=True, exist_ok=True)
    samples = [0.0] * int(LENGTH_S * RATE)
    for t in COUNT_BEEPS_S:
        tone(samples, t, 0.12, 1000, 0.45)
    tone(samples, START_BEEP_S, 0.6, 2700, 0.6)

    with tempfile.TemporaryDirectory() as tmp:
        wav = Path(tmp) / "countdown.wav"
        with wave.open(str(wav), "wb") as w:
            w.setnchannels(1)
            w.setsampwidth(2)
            w.setframerate(RATE)
            w.writeframes(b"".join(struct.pack("<h", int(max(-1, min(1, s)) * 32000)) for s in samples))
        if shutil.which("ffmpeg") is None:
            raise SystemExit("ffmpeg not found")
        subprocess.run(
            ["ffmpeg", "-y", "-loglevel", "error", "-i", str(wav), "-ac", "1", "-b:a", "64k", str(out)],
            check=True,
        )
    print(f"wrote {out} ({out.stat().st_size} bytes)")


if __name__ == "__main__":
    main()
