#!/usr/bin/env python3
from pathlib import Path
from PIL import Image
import struct
import sys

INPUT = Path('tsk_girl.jpg')
OUTPUT = Path('tsk_girl.jr32._hid_')
MAX_W = 160
MAX_H = 120


def main() -> None:
    input_path = Path(sys.argv[1]) if len(sys.argv) > 1 else INPUT
    output_path = Path(sys.argv[2]) if len(sys.argv) > 2 else OUTPUT

    if not input_path.exists():
        raise SystemExit(f'missing {input_path}')

    img = Image.open(input_path).convert('RGB')
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
    output_path.write_bytes(data)


if __name__ == '__main__':
    main()
