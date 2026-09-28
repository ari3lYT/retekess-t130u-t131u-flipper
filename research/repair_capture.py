"""Reproduce two experimental frame-aligned variants of a packet-mode capture.

These are diagnostic transformations, not a decoded audio codec.  They align to
the empirical CH19 marker and only alter data near inferred frame boundaries.
"""

from __future__ import annotations

import argparse
from collections import Counter
import hashlib
from pathlib import Path


MARKER = "10011011000110111010010100100101"
FRAME_BITS = 1984


def find_all(data: str, needle: str) -> list[int]:
    result = []
    at = 0
    while (at := data.find(needle, at)) >= 0:
        result.append(at)
        at += 1
    return result


def pack(frames: list[str]) -> bytes:
    assert all(len(frame) == FRAME_BITS for frame in frames)
    bits = "".join(frames)
    return int(bits, 2).to_bytes(len(bits) // 8, "big")


def pad_preamble(frame: str) -> str:
    if len(frame) > FRAME_BITS:
        frame = frame[:FRAME_BITS]
    while len(frame) < FRAME_BITS:
        frame += "1" if frame[-1] == "0" else "0"
    return frame


def linear_fit(xs: list[int], ys: list[int]) -> tuple[float, float]:
    mean_x = sum(xs) / len(xs)
    mean_y = sum(ys) / len(ys)
    slope = sum((x - mean_x) * (y - mean_y) for x, y in zip(xs, ys)) / sum(
        (x - mean_x) ** 2 for x in xs
    )
    return slope, mean_y - slope * mean_x


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("source", type=Path)
    parser.add_argument("short_output", type=Path)
    parser.add_argument("full_output", type=Path)
    args = parser.parse_args()

    bits = "".join(f"{byte:08b}" for byte in args.source.read_bytes())
    marks = find_all(bits, MARKER)
    assert len(marks) > 10

    short_frames = []
    widths = Counter()
    for start, end in zip(marks, marks[1:]):
        if 1980 <= end - start <= 1984:
            short_frames.append(pad_preamble(bits[start:end]))
            widths[end - start] += 1

    slots = [round((mark - marks[0]) / 1982) for mark in marks]
    slope, intercept = linear_fit(slots, marks)
    exact = dict(zip(slots, marks))
    positions = {}
    scores = {}
    for slot in range(slots[0], slots[-1] + 1):
        if slot in exact:
            positions[slot], scores[slot] = exact[slot], 0
            continue
        predicted = round(slope * slot + intercept)
        options = []
        for position in range(predicted - 8, predicted + 9):
            distance = sum(a != b for a, b in zip(bits[position : position + 32], MARKER))
            options.append((distance, abs(position - predicted), position))
        distance, _, position = min(options)
        positions[slot], scores[slot] = position, distance

    full_frames = []
    last_good = None
    counts = Counter()
    for slot in range(slots[0], slots[-1]):
        if scores[slot] <= 4:
            frame = bits[positions[slot] : positions[slot + 1]]
            assert 1970 <= len(frame) <= 1990
            frame = pad_preamble(MARKER + frame[32:])
            last_good = frame
            counts["exact" if scores[slot] == 0 else "header_repaired"] += 1
        else:
            assert last_good is not None
            frame = last_good
            counts["held"] += 1
        full_frames.append(frame)

    for destination, frames in [
        (args.short_output, short_frames),
        (args.full_output, full_frames),
    ]:
        data = pack(frames)
        destination.parent.mkdir(parents=True, exist_ok=True)
        destination.write_bytes(data)
        print(destination, len(frames), len(data), hashlib.sha256(data).hexdigest())
    print("period_fit_bits", slope, "short_widths", dict(widths), "full_counts", dict(counts))


if __name__ == "__main__":
    main()
