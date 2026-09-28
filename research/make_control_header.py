"""Embed only independently tested 1984-bit command frames in RX/control FAP."""

from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "outputs"))
from retekess_audio_encoder import crc16_aug_ccitt

cases = (
    ("alloff_frame", "synth_alloff_crc_radio.bin", 0x9B1BA525, 0x7777),
    ("chs18_frame", "chs18_clean_preamble.bin", 0x9B1BA5A6, 0x1112),
    ("chs19_frame", "chs19_synth_from18_preamble.bin", 0x9B1BA5A6, 0x1113),
    ("chs1_frame", "chs1_candidate_preamble.bin", 0x9B1BA5A6, 0x1101),
)
lines = ["// Generated from frames validated by the user's T131U.", "#pragma once", "#include <stdint.h>", "#define FRAME_BITS 1984u"]
for name, filename, marker, field in cases:
    packet = (ROOT / "outputs/captures" / filename).read_bytes()[:248]
    bits = "".join(f"{byte:08b}" for byte in packet)
    assert len(bits) == 1984
    assert int(bits[:32], 2) == marker
    assert int(bits[529:545], 2) == field
    assert crc16_aug_ccitt(bits[33:545]) == int(bits[545:561], 2)
    lines.append(f"static const uint8_t {name}[248] = {{")
    for i in range(0, 248, 16):
        lines.append("    " + ", ".join(f"0x{x:02X}" for x in packet[i:i+16]) + ",")
    lines.append("};")
dest = ROOT / "work/rx_control/frame_generated.h"
dest.write_text("\n".join(lines) + "\n")
print(dest)
