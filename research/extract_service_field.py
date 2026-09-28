"""Extract the empirically located 16-bit field from candidate GDO0 bits.

The alignment marker and 1984-bit spacing were measured from raw RX samples.
The field name is deliberately descriptive; a packet format and CRC are not
yet established.
"""

import csv
import pathlib
import sys

import numpy as np


DEFAULT_MARKER = "10011011000110111010010100100101"
PERIOD = 1984
FIELD_START = 529
FIELD_END = 545


def extract(row: dict[str, str], marker: str) -> dict[str, str]:
    bits = row["candidate_bits"]
    marker_index = bits.find(marker)
    field = ""
    if marker_index >= 0:
        positions = (np.arange(len(bits)) - marker_index) % PERIOD
        _, first = np.unique(positions, return_index=True)
        aligned = np.full(PERIOD, "?", dtype="<U1")
        aligned[positions[first]] = np.array(list(bits))[first]
        field = "".join(aligned[FIELD_START:FIELD_END])
    return {
        "block": row["block"],
        "elapsed_ms": row.get("elapsed_ms", ""),
        "actual_hz": row["actual_hz"],
        "rssi_dbm": row["rssi_dbm"],
        "clock_coherence": row["clock_coherence"],
        "marker_index_in_block": str(marker_index),
        "field_bits_529_to_544": field,
        "field_hex": f"{int(field, 2):04X}" if "?" not in field and field else "",
    }


def main() -> None:
    source = pathlib.Path(sys.argv[1])
    destination = pathlib.Path(sys.argv[2])
    marker = f"{int(sys.argv[3], 16):032b}" if len(sys.argv) > 3 else DEFAULT_MARKER
    with source.open(newline="") as file:
        rows = [extract(row, marker) for row in csv.DictReader(file)]
    with destination.open("w", newline="") as file:
        writer = csv.DictWriter(file, fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)
    print(destination, len(rows))


if __name__ == "__main__":
    main()
