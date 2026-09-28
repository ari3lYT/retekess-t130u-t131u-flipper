"""Find a channel's marker suffix and test known service/CRC layout."""

from pathlib import Path
from collections import Counter
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "outputs"))
from retekess_audio_encoder import crc16_aug_ccitt

channel = int(sys.argv[1]) if len(sys.argv) > 1 else 17
data = (ROOT / f"outputs/captures/packet_capture_ch{channel}.bin").read_bytes()
bits = "".join(f"{b:08b}" for b in data)
sync = f"{0x9B1B:016b}"
starts = []
at = 0
while (at := bits.find(sync, at)) >= 0:
    starts.append(at)
    at += 1
print("sync_count",len(starts),"file_bytes",len(data))
suffix = Counter(bits[p+16:p+32] for p in starts if p+561<len(bits))
for value,count in suffix.most_common(12):
    print("suffix",f"0x{int(value,2):04X}",count)
for value,count in suffix.most_common(5):
    valid = []
    fields = Counter()
    for p in starts:
        frame = bits[p:p+561]
        if len(frame)<561 or frame[16:32]!=value:
            continue
        if crc16_aug_ccitt(frame[33:545]) == int(frame[545:561],2):
            valid.append(p)
            fields[int(frame[529:545],2)] += 1
    gaps = Counter(b-a for a,b in zip(valid,valid[1:]))
    print("marker",f"0x{0x9B1B0000|int(value,2):08X}",
          "crc_valid",len(valid),"fields",dict(fields),"gaps",gaps.most_common(5))
