#!/usr/bin/env python3
"""Crop regions out of a simulator PPM and blow them up, for judging glyphs.

    zoom.py frame.ppm out.png 8 0,0,284,12 196,12,84,26

Each region is x,y,w,h in panel pixels. Regions are stacked vertically with a
gutter, scaled by the given factor.
"""
import struct
import sys
import zlib

BG = (24, 24, 28)
GUTTER = 10


def read_ppm(path):
    data = open(path, "rb").read()
    parts = data.split(b"\n", 3)
    w, h = (int(v) for v in parts[1].split())
    return w, h, parts[3]


def write_png(path, w, h, rows):
    raw = b"".join(b"\x00" + r for r in rows)

    def chunk(tag, payload):
        c = tag + payload
        return struct.pack(">I", len(payload)) + c + struct.pack(">I", zlib.crc32(c))

    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(raw, 9))
    png += chunk(b"IEND", b"")
    open(path, "wb").write(png)


def main():
    src, out, scale = sys.argv[1], sys.argv[2], int(sys.argv[3])
    regions = []
    for spec in sys.argv[4:]:
        regions.append(tuple(int(v) for v in spec.split(",")))

    pw, ph, px = read_ppm(src)
    total_w = max(r[2] for r in regions) * scale + 2 * GUTTER
    total_h = sum(r[3] * scale + GUTTER for r in regions) + GUTTER

    rows = []
    y = 0
    band = []  # (start_row, region)
    for r in regions:
        band.append((GUTTER + y, r))
        y += r[3] * scale + GUTTER

    for oy in range(total_h):
        row = bytearray(bytes(BG) * total_w)
        for start, (rx, ry, rw, rh) in band:
            if start <= oy < start + rh * scale:
                sy = ry + (oy - start) // scale
                for sx in range(rw):
                    off = (sy * pw + rx + sx) * 3
                    rgb = px[off:off + 3]
                    for k in range(scale):
                        d = (GUTTER + sx * scale + k) * 3
                        row[d:d + 3] = rgb
        rows.append(bytes(row))

    write_png(out, total_w, total_h, rows)
    print("wrote", out, total_w, "x", total_h)


main()
