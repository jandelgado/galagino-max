#!/usr/bin/env python3
"""Render ROM bytes as 1 bpp columns (64 bytes each, bottom-up like the kernel)
to spot sprites. usage: gfxdump.py <rom.a26> <bank> <first hex> <end hex> <out.png>"""
import sys, zlib, struct
rom = open(sys.argv[1], 'rb').read()
bank, start, end, out = int(sys.argv[2]), int(sys.argv[3], 16), int(sys.argv[4], 16), sys.argv[5]
data = rom[bank * 4096 + (start & 0xfff): bank * 4096 + ((end - 1) & 0xfff) + 1]
COL = 64
ncol = (len(data) + COL - 1) // COL
W, H = ncol * 12, COL
SC = 4
img = [[40] * (W * SC) for _ in range(H * SC)]
for i, b in enumerate(data):
    c, r = divmod(i, COL)
    for bit in range(8):
        v = 255 if b & (0x80 >> bit) else (0 if (start + i) % 16 else 30)
        for dy in range(SC):
            for dx in range(SC):
                img[(H - 1 - r) * SC + dy][(c * 12 + bit) * SC + dx] = v
def png(w, h, rows):
    raw = b''.join(b'\0' + bytes(r) for r in rows)
    ch = lambda t, d: struct.pack('>I', len(d)) + t + d + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
    return b'\x89PNG\r\n\x1a\n' + ch(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 0, 0, 0, 0)) + ch(b'IDAT', zlib.compress(raw)) + ch(b'IEND', b'')
open(out, 'wb').write(png(W * SC, H * SC, img))
print(ncol, 'columns of', COL, 'bytes from', hex(start))
