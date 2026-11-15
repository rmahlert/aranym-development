#!/usr/bin/env python3
"""Create a relocatable TOS program from the checked-in owned guest code.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import argparse
from pathlib import Path
import struct
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    parser.add_argument("--verify-assembler", action="store_true")
    args = parser.parse_args()
    code = bytes.fromhex((HERE / "guest.hex").read_text())
    if args.verify_assembler:
        with tempfile.TemporaryDirectory(prefix="aranym-guest-assembly-") as directory:
            obj = Path(directory) / "guest.o"
            raw = Path(directory) / "guest.bin"
            subprocess.run(["m68k-atari-mint-as", "-m68000", str(HERE / "guest.S"), "-o", str(obj)], check=True)
            subprocess.run(["m68k-atari-mint-objcopy", "-O", "binary", "-j", ".text", str(obj), str(raw)], check=True)
            if raw.read_bytes() != code:
                raise RuntimeError("guest.hex does not match assembled guest.S")
    # All code/data references are PC-relative; no relocations or C runtime.
    header = struct.pack(">HIIIIIIH", 0x601a, len(code), 0, 1024, 0, 0, 0, 1)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(header + code)


if __name__ == "__main__":
    main()
