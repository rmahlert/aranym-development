#!/usr/bin/env python3
"""Check an installed ARAnyM package and its runtime dependencies."""
import argparse
import json
import os
import plistlib
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
import time


def check_runtime(command, firmware, root, output, mac):
    env = {key: value for key, value in os.environ.items()
           if not key.startswith(("LD_", "DYLD_", "ARANYM_", "SDL_"))}
    env["HOME"] = str(root)
    if not mac:
        env.update(SDL_VIDEODRIVER="dummy", SDL_AUDIODRIVER="dummy")
    midi = subprocess.run([*command, "--midi-list"], cwd=root, env=env, timeout=30,
                          stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    (output / "midi-list.txt").write_text(midi.stdout)
    if midi.returncode or not re.search(r"^(?:No native MIDI devices found\.|(?:input|output|input/output): .+)$",
                                        midi.stdout, re.MULTILINE):
        raise RuntimeError("MIDI device enumeration failed: " + midi.stdout)
    config = root / "boot.cfg"
    config.write_text(f"[GLOBAL]\nFastRAM = 64\nEmuTOS = {firmware}\nTOS =\nFloppy =\nBootstrap =\n"
                      "[JIT]\nJIT = Yes\nJITSafe = Yes\nJITFPU = Yes\n"
                      "[ETH0]\nType = slirp\nHostIP = 10.0.2.2\nAtariIP = 10.0.2.15\n"
                      "Netmask = 255.255.255.0\n[STARTUP]\nGrabMouse = No\n[AUDIO]\nEnabled = No\n")
    checkpoints = [r"Using video driver " + ("cocoa" if mac else "dummy"),
                   r"JIT runtime: enabled=yes safe=yes fpu=yes", r"ETH0: SLIRP \S+ user-mode networking ready",
                   r"EmuTOS .*loading from .*\[OK\]", r"NF BOOTSTRAP\(\$[0-9a-fA-F]+, \d+\) -> 0"]
    log = output / "firmware-startup.txt"
    with log.open("w") as stream, subprocess.Popen([*command, "-c", str(config)], cwd=root, env=env,
                                                   stdout=stream, stderr=subprocess.STDOUT) as process:
        try:
            deadline = time.monotonic() + 120
            while process.poll() is None and time.monotonic() < deadline:
                contents = log.read_text(errors="replace")
                if all(re.search(pattern, contents) for pattern in checkpoints):
                    return
                time.sleep(0.2)
            contents = log.read_text(errors="replace")
            missing = [pattern for pattern in checkpoints if not re.search(pattern, contents)]
            raise RuntimeError(f"firmware boot stopped (status {process.poll()}, missing {missing}):\n" + contents[-8000:])
        finally:
            if process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("app", type=Path, nargs="?")
    parser.add_argument("--installed-linux", action="store_true")
    parser.add_argument("--runtime", action="store_true")
    parser.add_argument("--qemu", help="QEMU user-mode emulator for a foreign Debian architecture")
    parser.add_argument("--diagnostics", type=Path)
    args = parser.parse_args()
    if args.installed_linux:
        if sys.platform != "linux" or args.app or not args.runtime or not args.diagnostics:
            parser.error("installed Linux checks require --runtime, --diagnostics and a Linux host")
        output = args.diagnostics.resolve()
        output.mkdir(parents=True, exist_ok=True)
        firmware = Path("/usr/share/aranym-unofficial/aranym/emutos-aranym.img").resolve(strict=True)
        prefix = [args.qemu, "-L", "/"] if args.qemu else []
        with tempfile.TemporaryDirectory(prefix="ARAnyM installed package ") as directory:
            check_runtime([*prefix, "/usr/bin/aranym-unofficial"], firmware, Path(directory), output, False)
        print("PASS: installed application, MIDI enumeration, JIT startup, SLIRP and diskless EmuTOS boot")
        return
    if not args.app or args.qemu:
        parser.error("provide a Mac app bundle or use --installed-linux")
    if sys.platform != "darwin":
        parser.error("package checks require native macOS")
    app = args.app.resolve(strict=True)
    output = args.diagnostics.resolve() if args.diagnostics else app.parent
    output.mkdir(parents=True, exist_ok=True)
    with (app / "Contents/Info.plist").open("rb") as stream:
        info = plistlib.load(stream)
        jit_enabled = bool(info.get("ARAnyMGuestJIT"))
        architecture = info.get("ARAnyMArchitecture", "arm64")
    report = {"native_arch": os.uname().machine, "relocated_cli": False,
              "guest_boot": "not tested", "guest_jit": "enabled; execution tested separately" if jit_enabled else "disabled"}
    if architecture not in ("arm64", "x86_64") or os.uname().machine != architecture:
        raise RuntimeError("native runner matching the app architecture required")
    with tempfile.TemporaryDirectory(prefix="ARAnyM relocated package ") as directory:
        root = Path(directory)
        relocated = root / app.name
        shutil.copytree(app, relocated, symlinks=True)
        contents = relocated / "Contents"
        executable = contents / "MacOS/aranym"
        libraries = list((contents / "Frameworks").iterdir())
        for alias in libraries:
            if alias.is_symlink() and (not alias.resolve().is_relative_to((contents / "Frameworks").resolve())
                                       or not alias.resolve().is_file()):
                raise RuntimeError(f"missing/escaping library alias {alias.name}")
        images = [executable] + [p for p in libraries if not p.is_symlink()]
        for image in images:
            subprocess.run(["lipo", str(image), "-verify_arch", architecture], check=True)
            subprocess.run(["codesign", "--verify", "--strict", str(image)], check=True)
            loads = subprocess.check_output(["otool", "-L", str(image)], text=True)
            for line in loads.splitlines()[1:]:
                name = line.strip().split(" (compatibility version", 1)[0]
                if name.startswith(("/usr/lib/", "/System/Library/")):
                    continue
                # Dylib install ID is not a dependency.
                if image != executable and name == "@rpath/" + image.name:
                    continue
                base = ("@executable_path/../Frameworks/" if image == executable
                        else "@loader_path/")
                if not name.startswith(base):
                    raise RuntimeError(f"external dependency {name} in {image.name}")
                target = (contents / "Frameworks" / name[len(base):]).resolve()
                if not target.is_relative_to((contents / "Frameworks").resolve()) or not target.is_file():
                    raise RuntimeError(f"missing/escaping bundled dependency {name}")
            commands = subprocess.check_output(["otool", "-l", str(image)], text=True)
            if re.search(r"\bcmd LC_RPATH\b", commands):
                raise RuntimeError(f"unexpected external search paths in {image.name}")
        subprocess.run(["codesign", "--verify", "--deep", "--strict", str(relocated)], check=True)
        # Controlled, disposable configuration directory for Cocoa startup.
        (root / "Aranym_files").mkdir()
        environment = {key: value for key, value in os.environ.items()
                       if not key.startswith(("DYLD_", "ARANYM_"))}
        log = output / "relocated-startup.txt"
        with log.open("w") as stream:
            process = subprocess.Popen([str(executable), "--help"], cwd=root, env=environment,
                                       text=True, stdout=stream, stderr=subprocess.STDOUT)
            try:
                status = process.wait(timeout=45)
            except subprocess.TimeoutExpired:
                try:
                    subprocess.run(["sample", str(process.pid), "2", "-file",
                                    str(output / "relocated-startup-stack.txt")],
                                   stdout=subprocess.DEVNULL, stderr=subprocess.STDOUT,
                                   timeout=10, check=False)
                finally:
                    process.kill()
                    process.wait()
                print(log.read_text(errors="replace")[-6000:])
                raise RuntimeError("relocated startup timed out; retained output and native stack")
        text = log.read_text(errors="replace")
        if status != 0 or "Usage:" not in text:
            print(text[-6000:])
            raise RuntimeError(f"relocated CLI startup failed: status {status}")
        report["relocated_cli"] = True
        if args.runtime:
            check_runtime([str(executable)], contents / "Resources/emutos-aranym.img", root, output, True)
            report.update(guest_boot=True, guest_jit="configured", midi_enumeration=True, slirp_startup=True)
    (output / "package-check.json").write_text(json.dumps(report, indent=2) + "\n")
    print("PASS: native architecture, signatures, bundled dependency closure, relocated CLI startup")
    if args.runtime:
        print("PASS: relocated application, MIDI enumeration, JIT startup, SLIRP and diskless EmuTOS boot")


if __name__ == "__main__":
    main()
