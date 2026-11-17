#!/usr/bin/env python3
"""Boot an owned Atari guest in the relocated Mac app and require host exit0.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

HERE = Path(__file__).resolve().parent
MARKER = "ARANYM FIRMWARE GUEST requesting clean shutdown"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--app", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if sys.platform != "darwin" or os.uname().machine not in ("arm64", "x86_64"):
        parser.error("this acceptance gate requires native macOS")
    args.output.mkdir(parents=True, exist_ok=True)
    report = {"native_mac_arm64": os.uname().machine == "arm64",
              "native_mac_x86_64": os.uname().machine == "x86_64", "guest_boot": False, "host_exit0": False,
              "guest_jit": False, "guest_desktop": "not tested"}
    log = args.output / "firmware-boot.txt"
    report_path = args.output / "firmware-boot.json"
    with tempfile.TemporaryDirectory(prefix="ARAnyM relocated firmware ") as directory:
        root = Path(directory)
        app = root / "ARAnyM firmware test.app"
        shutil.copytree(args.app.resolve(strict=True), app, symlinks=True)
        executable = app / "Contents/MacOS/aranym"
        rom = app / "Contents/Resources/emutos-aranym.img"
        program = root / "BOOT.PRG"
        subprocess.run([sys.executable, str(HERE / "build-guest.py"), str(program)], check=True)
        report["guest_sha256"] = hashlib.sha256(program.read_bytes()).hexdigest()
        report["firmware_sha256"] = hashlib.sha256(rom.read_bytes()).hexdigest()
        (root / "Aranym_files").mkdir()
        config = root / "boot-test.config"
        config.write_text(
            f"[GLOBAL]\nFastRAM = 8\nEmuTOS = {rom}\nBootstrap = {program}\n"
            "TOS =\nFloppy =\n[STARTUP]\nGrabMouse = No\n[ETH0]\nType = none\n")
        environment = {key: value for key, value in os.environ.items()
                       if not key.startswith(("DYLD_", "ARANYM_", "SDL_"))}
        with log.open("w") as stream:
            process = subprocess.Popen([str(executable), "-c", str(config)], cwd=root,
                                       env=environment, stdout=stream, stderr=subprocess.STDOUT)
            try:
                status = process.wait(timeout=45)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
                report["failure"] = "boot or guest shutdown timed out"
                report_path.write_text(json.dumps(report, indent=2) + "\n")
                print(log.read_text(errors="replace")[-8000:])
                raise RuntimeError(report["failure"])
        text = log.read_text(errors="replace")
        report["host_status"] = status
        report["host_exit0"] = status == 0
        report["guest_boot"] = MARKER in text and "EmuTOS" in text
        report["cocoa_video"] = "Using video driver cocoa" in text
        report_path.write_text(json.dumps(report, indent=2) + "\n")
        print(text[-8000:])
        if not (report["host_exit0"] and report["guest_boot"] and report["cocoa_video"]):
            raise RuntimeError("firmware guest did not boot with Cocoa and terminate the host cleanly")
    print("PASS: relocated native Mac app, EmuTOS, owned 68k supervisor guest and clean NF_SHUTDOWN0")


if __name__ == "__main__":
    main()
