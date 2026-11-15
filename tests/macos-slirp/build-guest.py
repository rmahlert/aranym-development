#!/usr/bin/env python3
"""Build the owned networking PRG, optionally verify against the cross compiler.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import argparse
from pathlib import Path
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    parser.add_argument("--verify-compiler", action="store_true")
    args = parser.parse_args()
    code = bytes.fromhex((HERE / "guest.hex").read_text())
    if args.verify_compiler:
        with tempfile.TemporaryDirectory(prefix="aranym-network-guest-") as directory:
            program = Path(directory) / "GUEST.PRG"
            subprocess.run(["m68k-atari-mint-gcc", "-m68000", "-Os", "-fno-builtin", "-ffreestanding",
                            "-nostdlib", "-Wall", "-Wextra", "-Werror", "-Wl,-e,_start",
                            str(HERE / "entry.S"), str(HERE / "guest.c"), "-o", str(program)], check=True)
            subprocess.run(["m68k-atari-mint-strip", "--strip-all", str(program)], check=True)
            if program.read_bytes() != code:
                raise RuntimeError("guest.hex does not match the compiled guest source")
    if code[:2] != bytes.fromhex("601a"):
        raise RuntimeError("fixture is not a TOS PRG")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(code)


if __name__ == "__main__":
    main()
