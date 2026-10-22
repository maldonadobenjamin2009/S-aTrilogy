#!/usr/bin/env python3
"""Create a Windows .ico from Sonic Advance Trilogy's canonical 256x256 PNG icon."""
from __future__ import annotations
import pathlib
import struct
import sys

def main() -> int:
    if len(sys.argv) != 4:
        print("usage: png_to_ico.py INPUT.png OUTPUT.ico OUTPUT.rc", file=sys.stderr)
        return 2
    png_path, ico_path, rc_path = map(pathlib.Path, sys.argv[1:])
    data = png_path.read_bytes()
    if not data.startswith(b"\x89PNG\r\n\x1a\n") or len(data) < 24:
        raise SystemExit(f"Invalid PNG icon: {png_path}")
    width, height = struct.unpack(">II", data[16:24])
    if (width, height) != (256, 256):
        raise SystemExit(f"Expected a 256x256 icon in {png_path}, got {width}x{height}")
    # ICONDIR + a single entry containing the unchanged PNG image payload.
    header = struct.pack("<HHH", 0, 1, 1)
    entry = struct.pack("<BBBBHHII", 0, 0, 0, 0, 1, 32, len(data), 22)
    ico_path.parent.mkdir(parents=True, exist_ok=True)
    rc_path.parent.mkdir(parents=True, exist_ok=True)
    ico_path.write_bytes(header + entry + data)
    icon_ref = ico_path.resolve().as_posix().replace('"', r'\"')
    rc_path.write_text(f'1 ICON "{icon_ref}"\n', encoding="ascii")
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
