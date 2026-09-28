"""Extract CRC-valid CHS frames and replace only the noisy tail with preamble."""

from pathlib import Path
import csv

ROOT = Path(__file__).resolve().parents[1]
CAP = ROOT / "outputs/captures"
MARKER = f"{0x9B1BA5A6:032b}"


def crc(bits):
    reg = 0x1D0F
    for char in bits:
        carry = (reg >> 15) ^ int(char)
        reg = ((reg << 1) & 0xFFFF) ^ (0x1021 if carry else 0)
    return reg


def extract(path, field):
    choices = []
    with path.open(newline="") as source:
        for row in csv.DictReader(source):
            bits = row["candidate_bits"]
            at = bits.find(MARKER)
            while at >= 0:
                frame = bits[at:at + 561]
                if len(frame) == 561 and int(frame[529:545], 2) == field:
                    if crc(frame[33:545]) == int(frame[545:561], 2):
                        choices.append((int(row["block"]), at, frame))
                at = bits.find(MARKER, at + 1)
    if not choices:
        raise RuntimeError(f"No valid CHS frame for {field:04X}")
    block, pos, frame = choices[len(choices) // 2]
    tail_len = 1984 - 561
    clean = frame + ("10" * ((tail_len + 1) // 2))[:tail_len]
    assert len(clean) == 1984
    packet = bytes(int(clean[i:i+8], 2) for i in range(0, 1984, 8))
    return packet, len(choices), block, pos


for name, input_name, field in (
    ("chs18", "chs_carrier_864475_decoded.csv", 0x1112),
    ("chs19", "chs_carrier_reverse_864475_decoded.csv", 0x1113),
):
    packet, count, block, pos = extract(CAP / input_name, field)
    out = CAP / f"{name}_clean_preamble.bin"
    out.write_bytes(packet * 64)  # 0.508 seconds; FAP repeats 8x = ~4 seconds.
    print(name, "valid candidates", count, "source block", block,
          "marker bit", pos, "output bytes", out.stat().st_size)
