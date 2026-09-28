from pathlib import Path
import math
import struct
import wave

dest = Path(__file__).resolve().parent / "test_three_notes.wav"
rate = 8000
samples = []
for frequency in (523.25, 659.25, 783.99):
    length = int(0.48 * rate)
    for n in range(length):
        ramp = min(1.0, n / 80, (length - n - 1) / 80)
        value = int(7500 * max(ramp, 0) * math.sin(2 * math.pi * frequency * n / rate))
        samples.append(value)
with wave.open(str(dest), "wb") as output:
    output.setnchannels(1)
    output.setsampwidth(2)
    output.setframerate(rate)
    output.writeframes(b"".join(struct.pack("<h", value) for value in samples))
print(dest, len(samples) / rate, "seconds")
