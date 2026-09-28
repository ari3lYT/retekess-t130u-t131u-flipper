#!/usr/bin/env python3
"""Encode up to 1.57 s of CH19 A-law audio without a captured template.

The generated frame layout is protocol-derived. This privacy-safe version is
not yet validated end-to-end with T131U; use the preview app accordingly.
"""
from pathlib import Path
import argparse
import subprocess
from make_silent_control_frames import frame

FRAMES = 198
SAMPLES_PER_FRAME = 62

def encode(path):
    result=subprocess.run(['ffmpeg','-hide_banner','-loglevel','error','-i',str(path),'-ac','1','-ar','8000','-c:a','pcm_alaw','-f','alaw','-'],check=True,capture_output=True)
    audio=result.stdout[:FRAMES*SAMPLES_PER_FRAME].ljust(FRAMES*SAMPLES_PER_FRAME,b'\xD5')
    return b''.join(frame(0x9B1BA525,0,audio[i*62:(i+1)*62]) for i in range(FRAMES))

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('audio',type=Path)
    parser.add_argument('--output',type=Path,default=Path('custom_alaw_radio.bin'))
    args=parser.parse_args()
    args.output.write_bytes(encode(args.audio))
    print(args.output, args.output.stat().st_size, 'bytes')
