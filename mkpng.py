#!/usr/bin/env python3
from pathlib import Path
from PIL import Image
import struct

INPUT = Path('tsk_girl.jpg')
OUTPUT = Path('tsk_girl.jr32._hid_')
MAX_W = 160
MAX_H = 120


def main() -> None:
    if not INPUT.exists():
        raise SystemExit(f'missing {INPUT}')

    img = Image.open(INPUT).convert('RGB')
    width, height = img.size
    if width > MAX_W or height > MAX_H:
        raise SystemExit(f'JPEG too large: {width}x{height}, max {MAX_W}x{MAX_H}')

    header = bytearray()
    header.extend(b'JR32')
    header.append(1)
    header.extend(struct.pack('<H', width))
    header.extend(struct.pack('<H', height))
    header.extend(b'\x00\x00\x00')

    pixels = bytearray()
    for r, g, b in img.getdata():
        pixels.extend((r, g, b, 255))

    data = header + pixels
    OUTPUT.write_bytes(data)


if __name__ == '__main__':
    main()
