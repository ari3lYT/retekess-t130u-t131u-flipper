"""Synthesize CHS target CH19 from the verified CHS→CH18 packet."""

from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "outputs"))
from retekess_audio_encoder import crc16_aug_ccitt

source = ROOT / "outputs/captures/chs18_clean_preamble.bin"
dest = ROOT / "outputs/captures/chs19_synth_from18_preamble.bin"
raw = source.read_bytes()
assert len(raw) == 64 * 248
bits = "".join(f"{b:08b}" for b in raw[:248])
assert bits[:32] == f"{0x9B1BA5A6:032b}"
assert bits[529:545] == f"{0x1112:016b}"
assert crc16_aug_ccitt(bits[33:545]) == int(bits[545:561], 2)
bits = bits[:529] + f"{0x1113:016b}" + bits[545:]
bits = bits[:545] + f"{crc16_aug_ccitt(bits[33:545]):016b}" + bits[561:]
assert crc16_aug_ccitt(bits[33:545]) == int(bits[545:561], 2)
packet = bytes(int(bits[i:i+8], 2) for i in range(0, 1984, 8))
dest.write_bytes(packet * 64)
print(dest, len(packet * 64), "bytes, field", hex(int(bits[529:545], 2)))
