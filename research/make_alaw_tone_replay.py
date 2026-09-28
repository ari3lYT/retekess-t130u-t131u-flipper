"""Put a newly generated 1 kHz G.711 A-law tone into saved CH19 frames.

Experimental replay file: replaces only candidate audio bits 33..528. The
original capture and non-audio bits remain untouched. This may fail if the
receiver checks a payload CRC; that is precisely what the experiment tests.
"""

from pathlib import Path
import math
import struct
import subprocess
import wave


ROOT = Path(__file__).resolve().parents[1]
WORK = ROOT / "work"
OUT = ROOT / "outputs/captures"
SOURCE = OUT / "voice_frames_fullspeed.bin"
DEST = OUT / "synth_alaw_1khz_crc_radio.bin"
MARKER = "10011011000110111010010100100101"
FRAMES = 198
SAMPLES_PER_FRAME = 62


def crc16_ccitt(bits: str) -> int:
    # CRC-16/AUG-CCITT: poly 0x1021, init 0x1D0F, xorout 0.
    reg = 0x1D0F
    for char in bits:
        carry = (reg >> 15) ^ int(char)
        reg = ((reg << 1) & 0xffff) ^ (0x1021 if carry else 0)
    return reg


def make_tone():
    count = FRAMES * SAMPLES_PER_FRAME
    pcm = bytearray()
    for n in range(count):
        sample = round(5000 * math.sin(2 * math.pi * 1000 * n / 8000))
        pcm.extend(struct.pack("<h", sample))
    wav_path = WORK / "synth_1khz_input.wav"
    with wave.open(str(wav_path), "wb") as wav:
        wav.setnchannels(1)
        wav.setsampwidth(2)
        wav.setframerate(8000)
        wav.writeframes(pcm)
    alaw_path = WORK / "synth_1khz.alaw"
    subprocess.run([
        "ffmpeg", "-hide_banner", "-loglevel", "error", "-y",
        "-i", str(wav_path), "-acodec", "pcm_alaw", "-f", "alaw",
        str(alaw_path),
    ], check=True)
    data = alaw_path.read_bytes()
    assert len(data) == count
    return data


def main():
    tone = make_tone()
    frames = SOURCE.read_bytes()
    assert len(frames) == FRAMES * 248
    output = bytearray()
    for k in range(FRAMES):
        frame = frames[k * 248:(k + 1) * 248]
        bits = "".join(f"{b:08b}" for b in frame)
        assert bits.startswith(MARKER)
        audio = "".join(f"{b:08b}" for b in tone[k * 62:(k + 1) * 62])
        bits = bits[:33] + audio + bits[529:]
        crc = crc16_ccitt(bits[33:545])
        bits = bits[:545] + f"{crc:016b}" + bits[561:]
        assert crc16_ccitt(bits[33:545]) == int(bits[545:561], 2)
        output.extend(int(bits[j:j + 8], 2) for j in range(0, 1984, 8))
    DEST.write_bytes(output)
    print(DEST, len(output), "bytes", FRAMES, "frames")


if __name__ == "__main__":
    main()
