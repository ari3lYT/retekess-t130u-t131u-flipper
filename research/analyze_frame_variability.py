"""Compare bit variability after exact CH19 markers in saved RX FIFO streams."""

from collections import Counter
from math import log2
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
MARKER = "10011011000110111010010100100101"
WIDTH = 1968


def read_frames(path: Path):
    bits = "".join(f"{byte:08b}" for byte in path.read_bytes())
    marks = []
    at = 0
    while (at := bits.find(MARKER, at)) >= 0:
        marks.append(at)
        at += 1
    # Filter isolated accidental matches; genuine marks repeat at ~1982 bits.
    good = [m for i, m in enumerate(marks) if any(
        1978 <= abs(m - n) <= 1986 for n in marks[max(0, i - 1):i] + marks[i + 1:i + 2]
    )]
    return [bits[m:m + WIDTH] for m in good if m + WIDTH <= len(bits)], Counter(
        b - a for a, b in zip(marks, marks[1:])
    )


def entropy(p):
    return 0 if p in (0, 1) else -p * log2(p) - (1 - p) * log2(1 - p)


def stats(frames):
    return [sum(f[i] == "1" for f in frames) / len(frames) for i in range(WIDTH)]


base, base_dist = read_frames(ROOT / "outputs/captures/packet_capture_infinite.bin")
voice, voice_dist = read_frames(ROOT / "outputs/captures/packet_capture_voice.bin")
p_base, p_voice = stats(base), stats(voice)
print("frames", len(base), len(voice))
print("distances_baseline", base_dist.most_common(8))
print("distances_voice", voice_dist.most_common(8))
print("columns: bits_after_marker, baseline_entropy, voice_entropy, delta_entropy, baseline_majority, voice_majority, opposite_majorities")
for start in range(0, WIDTH, 64):
    end = min(start + 64, WIDTH)
    eb = sum(entropy(p) for p in p_base[start:end]) / (end - start)
    ev = sum(entropy(p) for p in p_voice[start:end]) / (end - start)
    const_b = sum(max(p, 1 - p) >= .95 for p in p_base[start:end])
    const_v = sum(max(p, 1 - p) >= .95 for p in p_voice[start:end])
    opposite = sum((p_base[i] > .8 and p_voice[i] < .2) or (p_base[i] < .2 and p_voice[i] > .8) for i in range(start, end))
    print(f"{start:4}-{end-1:4} {eb:.3f} {ev:.3f} {ev-eb:+.3f} {const_b:2} {const_v:2} {opposite:2}")
