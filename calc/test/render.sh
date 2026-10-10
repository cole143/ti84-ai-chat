#!/bin/bash
# Renders every screen of the calculator UI on the host (no calculator needed).
# Run inside WSL/Linux:  bash calc/test/render.sh   → calc/test/out/*.ppm
# Then on Windows:        python calc/test/ppm2png.py → calc/test/out/*.png (3x)
set -e
cd "$(dirname "$0")/.."
mkdir -p test/out
if [ ! -f test/font8x8.bin ] && [ -f ../clibs.8xg ]; then
    python3 test/extract_font.py ../clibs.8xg test/font8x8.bin || true
fi
gcc -std=c11 -Wall -Wextra -Wno-unused-function -I test/shim -I src \
    test/render_screens.c test/shim/shim.c src/logic.c -o /tmp/render_screens
/tmp/render_screens
