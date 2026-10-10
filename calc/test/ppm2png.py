"""Convert test/out/*.ppm to PNG (pure Python, no Pillow needed) and upscale 3x."""
import glob, os, struct, sys, zlib

SCALE = 3

def png(path, w, h, rows):
    def chunk(tag, data):
        c = struct.pack(">I", len(data)) + tag + data
        return c + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)
    raw = b"".join(b"\x00" + r for r in rows)
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
                + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))

for src in sorted(glob.glob(os.path.join(os.path.dirname(__file__) or ".", "out", "*.ppm"))):
    data = open(src, "rb").read()
    parts = data.split(b"\n", 3)
    w, h = map(int, parts[1].split())
    pix = parts[3]
    rows = []
    for y in range(h):
        row = pix[y * w * 3:(y + 1) * w * 3]
        big = b"".join(row[x * 3:x * 3 + 3] * SCALE for x in range(w))
        rows.extend([big] * SCALE)
    out = src[:-4] + ".png"
    png(out, w * SCALE, h * SCALE, rows)
    print("wrote", out)
