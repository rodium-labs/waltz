#!/usr/bin/env python3
"""Turn a run of simulator PPM frames into an animated GIF.

    gif.py out.gif 3 4 frame-00.ppm frame-01.ppm ...

Arguments are the output path, an integer scale, the frame delay in hundredths
of a second, then the frames in order. Pure standard library: median-cut down to
256 colours, then GIF89a with LZW.
"""
import sys


def read_ppm(path):
    data = open(path, "rb").read()
    parts = data.split(b"\n", 3)
    w, h = (int(v) for v in parts[1].split())
    return w, h, parts[3]


def median_cut(colors, want):
    """Split the colour cloud until there are `want` boxes, average each."""
    boxes = [list(colors)]
    while len(boxes) < want:
        target, spread = -1, -1
        for i, box in enumerate(boxes):
            if len(box) < 2:
                continue
            for ch in range(3):
                lo = min(c[ch] for c in box)
                hi = max(c[ch] for c in box)
                if hi - lo > spread:
                    spread, target = hi - lo, i
        if target < 0:
            break
        box = boxes.pop(target)
        ch = max(range(3),
                 key=lambda c: max(p[c] for p in box) - min(p[c] for p in box))
        box.sort(key=lambda p: p[ch])
        mid = len(box) // 2
        boxes.append(box[:mid])
        boxes.append(box[mid:])

    out = []
    for box in boxes:
        n = len(box)
        out.append(tuple(sum(p[c] for p in box) // n for c in range(3)))
    return out


class BitWriter:
    def __init__(self):
        self.data = bytearray()
        self.acc = 0
        self.n = 0

    def write(self, code, size):
        self.acc |= code << self.n
        self.n += size
        while self.n >= 8:
            self.data.append(self.acc & 0xFF)
            self.acc >>= 8
            self.n -= 8

    def flush(self):
        if self.n:
            self.data.append(self.acc & 0xFF)
            self.acc = self.n = 0
        return bytes(self.data)


def lzw(indices, min_code_size):
    clear = 1 << min_code_size
    end = clear + 1
    bw = BitWriter()
    table = {}
    next_code = end + 1
    size = min_code_size + 1

    bw.write(clear, size)
    prefix = None
    for px in indices:
        if prefix is None:
            prefix = px
            continue
        key = (prefix << 12) | px
        if key in table:
            prefix = table[key]
            continue
        bw.write(prefix, size)
        if next_code < 4096:
            table[key] = next_code
            next_code += 1
            if next_code > (1 << size) and size < 12:
                size += 1
        else:
            bw.write(clear, size)
            table = {}
            next_code = end + 1
            size = min_code_size + 1
        prefix = px
    if prefix is not None:
        bw.write(prefix, size)
    bw.write(end, size)
    return bw.flush()


def changed_box(prev, cur, w, h):
    """Bounding box of the pixels that differ from @p prev, as x0,y0,x1,y1."""
    if prev is None:
        return 0, 0, w, h

    rows = [y for y in range(h)
            if prev[y * w * 3:(y + 1) * w * 3] != cur[y * w * 3:(y + 1) * w * 3]]
    if not rows:
        return 0, 0, 1, 1  # nothing moved; one pixel keeps the frame legal

    x0, x1 = w, 0
    for y in rows:
        base = y * w * 3
        for x in range(w):
            o = base + x * 3
            if prev[o:o + 3] != cur[o:o + 3]:
                if x < x0:
                    x0 = x
                if x >= x1:
                    x1 = x + 1
    return x0, rows[0], x1, rows[-1] + 1


def sub_blocks(payload):
    out = bytearray()
    for i in range(0, len(payload), 255):
        chunk = payload[i:i + 255]
        out.append(len(chunk))
        out += chunk
    out.append(0)
    return bytes(out)


def main():
    out_path, scale, delay = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
    paths = sys.argv[4:]

    frames = [read_ppm(p) for p in paths]
    w, h, _ = frames[0]
    sw, sh = w * scale, h * scale

    seen = set()
    for _, _, px in frames:
        for i in range(0, len(px), 3):
            seen.add((px[i], px[i + 1], px[i + 2]))

    palette = median_cut(seen, 256) if len(seen) > 256 else sorted(seen)
    lookup = {}
    for c in seen:
        best, bd = 0, 1 << 30
        for i, p in enumerate(palette):
            d = (c[0] - p[0]) ** 2 + (c[1] - p[1]) ** 2 + (c[2] - p[2]) ** 2
            if d < bd:
                best, bd = i, d
        lookup[c] = best

    bits = max(1, (len(palette) - 1).bit_length())
    table_size = 1 << bits

    gif = bytearray(b"GIF89a")
    gif += sw.to_bytes(2, "little") + sh.to_bytes(2, "little")
    gif += bytes([0xF0 | (bits - 1), 0, 0])
    for i in range(table_size):
        gif += bytes(palette[i] if i < len(palette) else (0, 0, 0))
    gif += b"\x21\xFF\x0BNETSCAPE2.0\x03\x01\x00\x00\x00"

    min_code = max(2, bits)
    prev = None
    for _, _, px in frames:
        # Only the part that actually moved has to be sent. Disposal 1 leaves
        # the previous frame in place underneath, so the rest costs nothing.
        x0, y0, x1, y1 = changed_box(prev, px, w, h)
        prev = px

        rows = []
        for y in range(y0, y1):
            row = []
            for x in range(x0, x1):
                o = (y * w + x) * 3
                idx = lookup[(px[o], px[o + 1], px[o + 2])]
                row.extend([idx] * scale)
            for _ in range(scale):
                rows.extend(row)

        gif += b"\x21\xF9\x04\x04" + delay.to_bytes(2, "little") + b"\x00\x00"
        gif += b"\x2C"
        gif += (x0 * scale).to_bytes(2, "little")
        gif += (y0 * scale).to_bytes(2, "little")
        gif += ((x1 - x0) * scale).to_bytes(2, "little")
        gif += ((y1 - y0) * scale).to_bytes(2, "little")
        gif += b"\x00"
        gif += bytes([min_code])
        gif += sub_blocks(lzw(rows, min_code))

    gif += b"\x3B"
    open(out_path, "wb").write(gif)
    print("wrote %s  %dx%d  %d frames  %d colours" %
          (out_path, sw, sh, len(frames), len(palette)))


main()
