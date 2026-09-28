"""Make a CH19 All Off command by changing a normal frame and recomputing CRC."""

from pathlib import Path
from make_alaw_tone_replay import crc16_ccitt


ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "outputs/captures/packet_capture_infinite.bin"
OUT = ROOT / "outputs/captures/synth_alloff_crc_radio.bin"
MARKER = "10011011000110111010010100100101"
bits = "".join(f"{b:08b}" for b in SRC.read_bytes())
marks = []
at = 0
while (at := bits.find(MARKER, at)) >= 0:
    marks.append(at)
    at += 1

for first, second in zip(marks, marks[1:]):
    if not 1981 <= second - first <= 1983:
        continue
    frame = bits[first:second]
    if crc16_ccitt(frame[33:545]) != int(frame[545:561], 2):
        continue
    assert frame[529:545] == "0" * 16
    frame = frame[:529] + f"{0x7777:016b}" + frame[545:]
    frame = frame[:545] + f"{crc16_ccitt(frame[33:545]):016b}" + frame[561:]
    while len(frame) < 1984:
        frame += "1" if frame[-1] == "0" else "0"
    assert len(frame) == 1984
    assert crc16_ccitt(frame[33:545]) == int(frame[545:561], 2)
    payload = frame * 64
    data = bytes(int(payload[j:j + 8], 2) for j in range(0, len(payload), 8))
    OUT.write_bytes(data)
    print(OUT, len(data), "bytes", "source marker bit", first)
    break
else:
    raise RuntimeError("no valid source frame")
