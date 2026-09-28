"""Align read-only GDO0 candidate bits to an empirical repeated marker.

This summarizes observed bit probabilities. It does not assign packet fields,
check a CRC, or assert that these bits were decoded without error.
"""

import csv
import pathlib
import sys

import numpy as np


FRAME_BITS = 1984
MARKER = "10011011000110111010010100100101"  # observed 0x9B1BA525
WINDOWS = {"pre": (2, 4), "mute": (9, 14), "post": (19, 24)}


def profile(rows: list[dict[str, str]], start_s: int, end_s: int):
    ones = np.zeros(FRAME_BITS, dtype=np.int64)
    counts = np.zeros(FRAME_BITS, dtype=np.int64)
    accepted = 0
    for row in rows:
        seconds = int(row["elapsed_ms"]) / 1000
        if not start_s <= seconds < end_s:
            continue
        bits = row["candidate_bits"]
        anchor = bits.find(MARKER)
        if anchor < 0:
            continue
        accepted += 1
        positions = (np.arange(len(bits)) - anchor) % FRAME_BITS
        _, first = np.unique(positions, return_index=True)
        selected = positions[first]
        values = np.fromiter((ord(bits[index]) - 48 for index in first), dtype=np.int64)
        np.add.at(counts, selected, 1)
        np.add.at(ones, selected, values)
    return ones, counts, accepted


def main() -> None:
    source = pathlib.Path(sys.argv[1])
    destination = pathlib.Path(sys.argv[2])
    with source.open(newline="") as file:
        rows = list(csv.DictReader(file))
    profiles = {name: profile(rows, *window) for name, window in WINDOWS.items()}
    with destination.open("w", newline="") as file:
        writer = csv.writer(file)
        writer.writerow(
            [
                "bit_position_after_9B1BA525_start",
                "pre_mute_off_frames",
                "pre_p_one",
                "mute_on_frames",
                "mute_p_one",
                "post_mute_off_frames",
                "post_p_one",
                "off_on_off_score",
            ]
        )
        for i in range(FRAME_BITS):
            fractions = {
                name: float(ones[i] / counts[i]) if counts[i] else float("nan")
                for name, (ones, counts, _) in profiles.items()
            }
            pre = fractions["pre"]
            mute = fractions["mute"]
            post = fractions["post"]
            score = min(abs(mute - pre), abs(mute - post)) - abs(pre - post)
            writer.writerow(
                [
                    i,
                    profiles["pre"][1][i],
                    round(pre, 6),
                    profiles["mute"][1][i],
                    round(mute, 6),
                    profiles["post"][1][i],
                    round(post, 6),
                    round(score, 6),
                ]
            )
    print(destination, {name: entry[2] for name, entry in profiles.items()})


if __name__ == "__main__":
    main()
