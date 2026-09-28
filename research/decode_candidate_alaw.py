"""Extract the 62 candidate 8-bit audio codes per recorded CH19 frame.

This does not change the capture. It is an experiment for the G.711 A-law
hypothesis, not a complete packet parser.
"""

from pathlib import Path
import subprocess


ROOT = Path(__file__).resolve().parents[1]
FRAME_BITS = 1984
MARKER = "10011011000110111010010100100101"
OUT = ROOT / "outputs"


def bytes_from_frames(path: Path, offset: int = 33):
    raw = path.read_bytes()
    assert len(raw) % 248 == 0
    output = bytearray()
    for j in range(0, len(raw), 248):
        bits = "".join(f"{b:08b}" for b in raw[j:j + 248])
        assert len(bits) == FRAME_BITS and bits.startswith(MARKER)
        audio = bits[offset:offset + 496]
        output.extend(int(audio[k:k + 8], 2) for k in range(0, 496, 8))
    return bytes(output)


def bytes_from_exact_markers(path: Path, offset: int = 33):
    bits = "".join(f"{b:08b}" for b in path.read_bytes())
    output = bytearray()
    at = 0
    count = 0
    while (at := bits.find(MARKER, at)) >= 0:
        audio = bits[at + offset:at + offset + 496]
        if len(audio) == 496:
            output.extend(int(audio[k:k + 8], 2) for k in range(0, 496, 8))
            count += 1
        at += 1
    return bytes(output), count


def bytes_timed_with_silence(path: Path, offset: int = 33):
    """Preserve approximate frame timing; replace missing frames with A-law zero."""
    bits = "".join(f"{b:08b}" for b in path.read_bytes())
    marks = []
    at = 0
    while (at := bits.find(MARKER, at)) >= 0:
        marks.append(at)
        at += 1
    slots = [round((mark - marks[0]) / 1982) for mark in marks]
    exact = dict(zip(slots, marks))
    output = bytearray()
    counts = {"exact": 0, "near": 0, "missing": 0}
    for slot in range(slots[0], slots[-1] + 1):
        mark = exact.get(slot)
        kind = "exact"
        if mark is None:
            expected = marks[0] + round(slot * 1982.12)
            choices = [(sum(a != b for a, b in zip(bits[p:p + 32], MARKER)), p)
                       for p in range(expected - 12, expected + 13)]
            score, mark = min(choices)
            kind = "near" if score <= 4 else "missing"
        if kind == "missing":
            output.extend([0xD5] * 62)
        else:
            audio = bits[mark + offset:mark + offset + 496]
            output.extend(int(audio[k:k + 8], 2) for k in range(0, 496, 8))
        counts[kind] += 1
    return bytes(output), counts


def decode(data: bytes, name: str):
    raw_path = OUT / f"{name}.alaw"
    wav_path = OUT / f"{name}.wav"
    raw_path.write_bytes(data)
    subprocess.run([
        "ffmpeg", "-hide_banner", "-loglevel", "error", "-y",
        "-f", "alaw", "-ar", "8000", "-ac", "1", "-i", str(raw_path),
        "-acodec", "pcm_s16le", str(wav_path),
    ], check=True)
    print(name, len(data), "bytes", len(data) / 8000, "seconds")


def main():
    voice = bytes_from_frames(OUT / "captures/voice_frames_fullspeed.bin")
    decode(voice, "candidate_g711_alaw_voice_fullspeed")
    baseline, count = bytes_from_exact_markers(OUT / "captures/packet_capture_infinite.bin")
    print("baseline_exact_frames", count)
    decode(baseline, "candidate_g711_alaw_baseline_exact")
    timed, counts = bytes_timed_with_silence(OUT / "captures/packet_capture_voice.bin")
    print("voice_timed_frames", counts)
    decode(timed, "candidate_g711_alaw_voice_timed")


if __name__ == "__main__":
    main()
