#!/usr/bin/env python3
"""Stitch the simulator's PPM frames into one scaled PNG contact sheet."""
import struct
import sys
import zlib

SCALE = 3
GUTTER = 12
BG = (24, 24, 28)


def read_ppm(path):
    data = open(path, "rb").read()
    # header: P6\n<w> <h>\n255\n
    parts = data.split(b"\n", 3)
    w, h = (int(v) for v in parts[1].split())
    px = parts[3]
    return w, h, px


def write_png(path, w, h, rows):
    raw = b"".join(b"\x00" + r for r in rows)
    comp = zlib.compress(raw, 9)

    def chunk(tag, payload):
        c = tag + payload
        return struct.pack(">I", len(payload)) + c + struct.pack(">I", zlib.crc32(c))

    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", comp)
    png += chunk(b"IEND", b"")
    open(path, "wb").write(png)


def main():
    paths = sys.argv[1:-1]
    out = sys.argv[-1]
    frames = [read_ppm(p) for p in paths]
    fw, fh, _ = frames[0]

    sw, sh = fw * SCALE, fh * SCALE
    # wide frames read better stacked; tall ones side by side
    stack = fw > fh
    if stack:
        total_w = sw + 2 * GUTTER
        total_h = len(frames) * sh + (len(frames) + 1) * GUTTER
    else:
        total_w = len(frames) * sw + (len(frames) + 1) * GUTTER
        total_h = sh + 2 * GUTTER

    rows = []
    for y in range(total_h):
        row = bytearray(bytes(BG) * total_w)
        if stack:
            band = (y - GUTTER) // (sh + GUTTER)
            off_y = (y - GUTTER) % (sh + GUTTER)
            fy = off_y // SCALE
            if 0 <= band < len(frames) and off_y < sh:
                w, h, px = frames[band]
                for fx in range(w):
                    off = (fy * w + fx) * 3
                    rgb = px[off:off + 3]
                    for s in range(SCALE):
                        d = (GUTTER + fx * SCALE + s) * 3
                        row[d:d + 3] = rgb
        else:
            fy = (y - GUTTER) // SCALE
            if 0 <= fy < fh:
                for i, (w, h, px) in enumerate(frames):
                    x0 = GUTTER + i * (sw + GUTTER)
                    for fx in range(w):
                        off = (fy * w + fx) * 3
                        rgb = px[off:off + 3]
                        for s in range(SCALE):
                            d = (x0 + fx * SCALE + s) * 3
                            row[d:d + 3] = rgb
        rows.append(bytes(row))

    write_png(out, total_w, total_h, rows)
    print("wrote", out, total_w, "x", total_h)


main()
