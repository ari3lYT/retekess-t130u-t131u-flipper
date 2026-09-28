"""Compare exact CRC-valid packet intervals in the existing CH19 FIFO capture."""

from pathlib import Path
from collections import Counter
import math

ROOT = Path(__file__).resolve().parents[1]
bits = "".join(f"{byte:08b}" for byte in (ROOT / "outputs/captures/packet_capture_infinite.bin").read_bytes())
marker = f"{0x9B1BA525:032b}"


def crc(payload):
    reg = 0x1D0F
    for bit in payload:
        carry = (reg >> 15) ^ int(bit)
        reg = ((reg << 1) & 0xFFFF) ^ (0x1021 if carry else 0)
    return reg


starts = []
at = 0
while (at := bits.find(marker, at)) >= 0:
    starts.append(at)
    at += 1

frames = []
widths = Counter()
for a, b in zip(starts, starts[1:]):
    width = b - a
    if 1980 <= width <= 1984 and crc(bits[a+33:a+545]) == int(bits[a+545:a+561], 2):
        frames.append(bits[a:b])
        widths[width] += 1
print("exact intervals", len(frames), "widths", dict(widths))
for left, right in [(0, 32), (32, 33), (33, 529), (529, 545), (545, 561),
                    (561, 625), (625, 753), (753, 1017), (1017, 1529),
                    (1529, 1825), (1825, 1940), (1940, 1980)]:
    positions = []
    for i in range(left, right):
        values = [int(f[i]) for f in frames if len(f) > i]
        if not values:
            continue
        p = sum(values)/len(values)
        entropy = 0 if p in (0, 1) else -p*math.log2(p)-(1-p)*math.log2(1-p)
        positions.append((p, entropy))
    print(left, right, "ones", round(sum(v[0] for v in positions)/len(positions), 3),
          "entropy", round(sum(v[1] for v in positions)/len(positions), 3))

for index in (0, 1, 2):
    f = frames[index]
    print("frame", index, "start", f[:80], "after_crc", f[561:641],
          "end", f[-80:])
