#!/usr/bin/env python3
"""Convert a raw frame written by scripts/rt.c (16-byte header + XRGB8888 rows)
to PNG using only the standard library.  Usage: bgra2png.py in.bgra out.png
"""
import struct
import sys
import zlib


def main():
    raw = open(sys.argv[1], "rb").read()
    w, h, pitch, _ = struct.unpack("<IIII", raw[:16])
    body = raw[16:]
    rows = []
    for y in range(h):
        row = body[y * w * 4:(y + 1) * w * 4]
        rgb = bytearray(w * 3)
        rgb[0::3] = row[2::4]
        rgb[1::3] = row[1::4]
        rgb[2::3] = row[0::4]
        rows.append(b"\x00" + bytes(rgb))

    def chunk(t, d):
        return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xffffffff)

    png = (b"\x89PNG\r\n\x1a\n"
           + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
           + chunk(b"IDAT", zlib.compress(b"".join(rows)))
           + chunk(b"IEND", b""))
    open(sys.argv[2], "wb").write(png)
    print("wrote", sys.argv[2], w, h)


if __name__ == "__main__":
    main()
