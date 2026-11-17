#!/usr/bin/env python3
"""Independently check bundle load paths and relocated CLI startup."""
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


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("app", type=Path)
    args = parser.parse_args()
    if sys.platform != "darwin":
        parser.error("package checks require native macOS")
    app = args.app.resolve(strict=True)
    output = app.parent
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
    (output / "package-check.json").write_text(json.dumps(report, indent=2) + "\n")
    print("PASS: native architecture, signatures, bundled dependency closure, relocated CLI startup")
    print("Guest boot, guest JIT and GBE tests remain required.")


if __name__ == "__main__":
    main()
