"""Test hypothesis: post-CRC bits are unsent gap/noise; replace by preamble."""

from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
source = ROOT / "outputs/captures/custom_alaw_radio.bin"
dest = ROOT / "outputs/captures/custom_alaw_preamble_tail.bin"
data = source.read_bytes()
assert len(data) == 198 * 248
result = bytearray()
for index in range(198):
    frame = data[index*248:(index+1)*248]
    bits = "".join(f"{byte:08b}" for byte in frame)
    new_bits = bits[:561] + ("10" * 712)[:1984-561]
    assert len(new_bits) == 1984
    result.extend(int(new_bits[i:i+8], 2) for i in range(0, 1984, 8))
dest.write_bytes(result)
print(dest, len(result), "bytes")
