"""Generate privacy-safe Retekess control frames with G.711 A-law silence.

The field and CRC are protocol-derived; the generated frames still require
receiver validation, unlike the original capture-derived templates.
"""
from pathlib import Path

FRAME_BITS = 1984

def crc(bits):
    value = 0x1D0F
    for bit in bits:
        carry = ((value >> 15) ^ int(bit)) & 1
        value = ((value << 1) & 0xFFFF) ^ (0x1021 if carry else 0)
    return value

def frame(marker, field, audio=bytes([0xD5])*62):
    bits = f'{marker:032b}' + '1' + ''.join(f'{x:08b}' for x in audio) + f'{field:016b}'
    bits += f'{crc(bits[33:545]):016b}'
    bits += ('10' * 992)[:FRAME_BITS-len(bits)]
    assert len(bits) == FRAME_BITS
    return bytes(int(bits[i:i+8],2) for i in range(0,FRAME_BITS,8))

cases = [
    ('alloff_frame',0x9B1BA525,0x7777),
    ('chs18_frame',0x9B1BA5A6,0x1112),
    ('chs19_frame',0x9B1BA5A6,0x1113),
    ('chs1_frame',0x9B1BA5A6,0x1101),
]
def write_header():
    lines = ['// Generated with A-law silence; no captured human audio.', '#pragma once', '#include <stdint.h>', '#define FRAME_BITS 1984u']
    for name,marker,field in cases:
        packet=frame(marker,field)
        lines.append(f'static const uint8_t {name}[248] = {{')
        for i in range(0,len(packet),16):
            lines.append('    '+', '.join(f'0x{x:02X}' for x in packet[i:i+16])+',')
        lines.append('};')
    Path(__file__).resolve().parents[1].joinpath('app/frame_generated.h').write_text('\n'.join(lines)+'\n')

if __name__ == '__main__':
    write_header()
