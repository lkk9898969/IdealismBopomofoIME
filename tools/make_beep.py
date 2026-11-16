# Generates src/tip/beep.wav, the error beep (no such syllable): a short 440 Hz sine tone, which is
# what Microsoft Bopomofo's beep measures as (pure A4, about 42 ms, no overtones). Synthesized here
# rather than taken from Windows, so the file is ours to distribute.
# Usage: py -3 tools/make_beep.py
import math
import pathlib
import struct
import wave

RATE = 44100
FREQ = 440.0
LENGTH = 0.042   # seconds
FADE = 0.0015    # seconds of raised-cosine fade at each end (avoids clicks)
LEVEL = 0.4      # peak amplitude (full scale = 1.0)

out = pathlib.Path(__file__).resolve().parent.parent / 'src' / 'tip' / 'beep.wav'
n = int(RATE * LENGTH)
fade = int(RATE * FADE)
samples = []
for i in range(n):
    gain = 1.0
    if i < fade:
        gain = 0.5 - 0.5 * math.cos(math.pi * i / fade)
    elif i >= n - fade:
        gain = 0.5 - 0.5 * math.cos(math.pi * (n - 1 - i) / fade)
    v = LEVEL * gain * math.sin(2 * math.pi * FREQ * i / RATE)
    samples.append(int(round(v * 32767)))

with wave.open(str(out), 'wb') as w:
    w.setnchannels(1)
    w.setsampwidth(2)
    w.setframerate(RATE)
    w.writeframes(struct.pack('<%dh' % n, *samples))
print('wrote', out, n, 'samples')
