"""Check whether bytes after the CH19 command field are a simple CRC-16.

Compares CRC differences between authentic frames, so the result does not
depend on an unknown initial register or final xor. Negative results only rule
out the tested locations/algorithms.
"""

from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
MARKER = "10011011000110111010010100100101"
bits = "".join(f"{b:08b}" for b in (ROOT / "outputs/captures/packet_capture_voice.bin").read_bytes())
frames = []
at = 0
while (at := bits.find(MARKER, at)) >= 0:
    if at + 600 < len(bits):
        frames.append(bits[at:at + 600])
    at += 1
frames = frames[:30]


def crc16(data: str, poly: int, reflected: bool):
    reg = 0
    for char in data:
        bit = int(char)
        if reflected:
            carry = reg & 1
            reg = (reg >> 1) ^ (poly if carry ^ bit else 0)
        else:
            carry = reg >> 15
            reg = ((reg << 1) & 0xffff) ^ (poly if carry ^ bit else 0)
    return reg


def rev16(x):
    return int(f"{x:016b}"[::-1], 2)


results = []
for end in (529, 545, 561):
    for poly, reflected in ((0x1021, False), (0x8408, True), (0x8005, False), (0xA001, True)):
        values = [crc16(f[33:end], poly, reflected) for f in frames]
        for target in range(545, 580):
            observed = [int(f[target:target + 16], 2) for f in frames]
            for flip in (False, True):
                expected = [rev16(v) if flip else v for v in values]
                score = sum((e ^ expected[0]) == (o ^ observed[0])
                            for e, o in zip(expected, observed))
                results.append((score, end, hex(poly), reflected, target, flip))

print("authentic_frames", len(frames))
for result in sorted(results, reverse=True)[:12]:
    print(result)
