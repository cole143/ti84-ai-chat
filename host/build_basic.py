"""Convert TI-BASIC source .txt files in basic/ to .8xp binaries in basic/bin/.

Run: python -m host.build_basic

Each .txt file must use the calculator's BASIC token syntax:
- Lines start with `:`
- `Menu(`, `Lbl`, `Goto`, `Disp`, `Input`, `Pause`, `Stop`, `If`, `Then`, `End`, `ClrHome`
- `->` for the store arrow

The on-calc program name is the .txt filename uppercased and truncated to 8 chars.
"""
from __future__ import annotations

from pathlib import Path

from tivars.types import TIProgram


PROJECT_ROOT = Path(__file__).resolve().parent.parent
BASIC_DIR = PROJECT_ROOT / "basic"
BIN_DIR = BASIC_DIR / "bin"


def build_one(src_path: Path, out_dir: Path) -> Path:
    """Convert a single .txt to .8xp. Returns the output path."""
    name = src_path.stem.upper()[:8]
    if not name.replace("_", "").isalnum():
        raise ValueError(f"Invalid program name {name!r} from {src_path.name}")

    src = src_path.read_text(encoding="utf-8")
    program = TIProgram(name=name)
    program.load_string(src)
    out_path = out_dir / f"{name}.8xp"
    out_path.write_bytes(program.export().bytes())
    return out_path


def main() -> None:
    BIN_DIR.mkdir(exist_ok=True)
    sources = sorted(p for p in BASIC_DIR.glob("*.txt"))
    if not sources:
        raise SystemExit(f"No .txt sources found in {BASIC_DIR}")

    print(f"Building {len(sources)} TI-BASIC programs -> {BIN_DIR}")
    for src in sources:
        out = build_one(src, BIN_DIR)
        size = out.stat().st_size
        print(f"  {src.name:18}-> {out.name:14}({size} bytes)")
    print("Done. Drag the .8xp files into TI Connect CE to upload to the calculator.")


if __name__ == "__main__":
    main()
