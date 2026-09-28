"""Compare CRC-valid CHS payloads across three observed target channels."""

from pathlib import Path
from collections import Counter
import csv
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "outputs"))
from retekess_audio_encoder import crc16_aug_ccitt

files = (
    (16, "chs_carrier_to16_864475_decoded.csv"),
    (18, "chs_carrier_864475_decoded.csv"),
    (19, "chs_carrier_reverse_864475_decoded.csv"),
)
marker = f"{0x9B1BA5A6:032b}"
groups = {}
for target, filename in files:
    frames = []
    with (ROOT / "outputs/captures" / filename).open(newline="") as source:
        for row in csv.DictReader(source):
            bits = row["candidate_bits"]
            at = bits.find(marker)
            while at >= 0:
                frame = bits[at:at+561]
                if len(frame) == 561 and int(frame[529:545], 2) == 0x1100 + target:
                    if crc16_aug_ccitt(frame[33:545]) == int(frame[545:561], 2):
                        frames.append(frame)
                at = bits.find(marker, at+1)
    groups[target] = frames
    print("CH", target, "valid", len(frames))

for left, right in ((33, 529),):
    modal = {}
    for target, frames in groups.items():
        chars = []
        fixed = 0
        for pos in range(left, right):
            counts = Counter(frame[pos] for frame in frames)
            bit, number = counts.most_common(1)[0]
            chars.append(bit)
            fixed += number == len(frames)
        modal[target] = "".join(chars)
        print("CH", target, "payload fixed bits", fixed, "of", right-left,
              "modal first16bytes", bytes(int(modal[target][i:i+8],2) for i in range(0,128,8)).hex())
    for a,b in ((16,18),(18,19),(16,19)):
        print("modal payload bit differences",a,b,sum(x!=y for x,y in zip(modal[a],modal[b])))
    stable_differences = []
    for pos in range(left, right):
        consensus = {}
        for target, frames in groups.items():
            count = Counter(frame[pos] for frame in frames)
            bit, number = count.most_common(1)[0]
            consensus[target] = (bit, number/len(frames))
        if min(value[1] for value in consensus.values()) >= 0.99:
            if len({value[0] for value in consensus.values()}) > 1:
                stable_differences.append((pos, consensus))
    print("high-confidence payload differences across targets", stable_differences)
