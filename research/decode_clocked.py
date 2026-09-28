"""Derive candidate 250-kbaud bits from a raw Flipper GDO0 CSV.

The phase is fitted independently for every 4-ms block. This is not a packet
decoder. Low-coherence rows are retained and marked unreliable so the raw
measurement and analysis can be audited together.
"""

import csv
import pathlib
import sys

import numpy as np


def longest_period8_run(bits: np.ndarray) -> int:
    matches = bits[:-8] == bits[8:]
    longest = current = 0
    for match in matches:
        current = current + 1 if match else 0
        longest = max(longest, current)
    return longest + 8


def decode_row(row: dict[str, str]) -> dict[str, str | int | float]:
    samples = np.unpackbits(np.frombuffer(bytes.fromhex(row["packed_gpio"]), dtype=np.uint8))
    transitions = np.flatnonzero(np.diff(samples.astype(np.int8))) + 1
    # 250,000 baud, 500 ns per input sample => 8 samples per transmitted bit.
    phasor = np.exp(2j * np.pi * transitions / 8).mean()
    coherence = float(abs(phasor))
    edge_phase = float((np.angle(phasor) * 8 / (2 * np.pi)) % 8)
    indexes = np.rint(edge_phase + 4 + 8 * np.arange(len(samples) // 8)).astype(int)
    indexes = indexes[(indexes >= 0) & (indexes < len(samples))]
    bits = samples[indexes]
    return {
        "mode": row["mode"],
        "elapsed_ms": row.get("elapsed_ms", ""),
        "actual_hz": row["actual_hz"],
        "block": row["block"],
        "rssi_dbm": int(row["rssi_dbm_x10"]) / 10,
        "sample_period_ns": row["sample_period_ns"],
        "late_samples": row["late_samples"],
        "clock_coherence": round(coherence, 6),
        "coherent_over_0_2": int(coherence > 0.2),
        "edge_phase_samples": round(edge_phase, 4),
        "bit_count": len(bits),
        "longest_period8_run_bits": longest_period8_run(bits),
        "candidate_bits": "".join(map(str, bits.tolist())),
    }


def main() -> None:
    source = pathlib.Path(sys.argv[1])
    destination = pathlib.Path(sys.argv[2])
    with source.open(newline="") as file:
        decoded = [decode_row(row) for row in csv.DictReader(file)]
    with destination.open("w", newline="") as file:
        writer = csv.DictWriter(file, fieldnames=list(decoded[0]))
        writer.writeheader()
        writer.writerows(decoded)
    print(
        f"{destination}: {len(decoded)} blocks, "
        f"{sum(int(row['coherent_over_0_2']) for row in decoded)} coherent"
    )


if __name__ == "__main__":
    main()
