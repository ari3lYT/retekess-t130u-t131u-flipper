"""Synthesize a candidate CHS target from the proven CHS→CH18 frame."""

import argparse
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "outputs"))
from retekess_audio_encoder import crc16_aug_ccitt

parser = argparse.ArgumentParser()
parser.add_argument("channel", type=int, choices=range(1, 20))
args = parser.parse_args()
source = ROOT / "outputs/captures/chs18_clean_preamble.bin"
dest = ROOT / f"outputs/captures/chs{args.channel}_candidate_preamble.bin"
bits = "".join(f"{b:08b}" for b in source.read_bytes()[:248])
assert bits[:32] == f"{0x9B1BA5A6:032b}"
assert int(bits[529:545], 2) == 0x1112
assert crc16_aug_ccitt(bits[33:545]) == int(bits[545:561], 2)
target_field = 0x1100 + args.channel
bits = bits[:529] + f"{target_field:016b}" + bits[545:]
bits = bits[:545] + f"{crc16_aug_ccitt(bits[33:545]):016b}" + bits[561:]
assert crc16_aug_ccitt(bits[33:545]) == int(bits[545:561], 2)
packet = bytes(int(bits[i:i+8], 2) for i in range(0, 1984, 8))
dest.write_bytes(packet * 64)
print(dest, dest.stat().st_size, "bytes, candidate field", hex(target_field))
