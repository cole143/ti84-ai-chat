"""Extract the graphx default 8x8 font (128 glyphs x 8 bytes) from clibs.8xg so the
host renderer can draw real text. Usage: python extract_font.py ../../clibs.8xg font8x8.bin"""
import sys

src, dst = sys.argv[1], sys.argv[2]
data = open(src, "rb").read()
for p in range(0, len(data) - 1024):
    if data[p:p + 8] != bytes(8):
        continue
    tbl = data[p:p + 1024]
    letters = list(range(0x41, 0x5B)) + list(range(0x30, 0x3A)) + [0x21]
    if not all(any(tbl[c * 8:(c + 1) * 8]) for c in letters):
        continue
    if sum(1 for c in range(0x21, 0x7F) if tbl[c * 8 + 7] == 0) < 60:
        continue
    open(dst, "wb").write(tbl)
    print("font table found at offset", hex(p), "->", dst)
    sys.exit(0)
print("font table not found; the renderer will fall back to box glyphs")
sys.exit(1)
