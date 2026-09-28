"""Retune verified synthetic CH19 voice frames to the measured CH1 marker."""

from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "outputs"))
from retekess_audio_encoder import crc16_aug_ccitt

source = ROOT / "outputs/captures/custom_alaw_voice_preamble.bin"
dest = ROOT / "outputs/captures/custom_alaw_voice_ch1.bin"
raw = source.read_bytes()
assert len(raw) == 198 * 248
result = bytearray()
for i in range(198):
    frame = raw[i*248:(i+1)*248]
    bits = "".join(f"{b:08b}" for b in frame)
    assert bits[:32] == f"{0x9B1BA525:032b}"
    assert crc16_aug_ccitt(bits[33:545]) == int(bits[545:561],2)
    result.extend((0x9B1B9C1C).to_bytes(4,"big") + frame[4:])
dest.write_bytes(result)
print(dest, len(result), "bytes")
