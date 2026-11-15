#!/usr/bin/env python3
"""Independently check bundle load paths and relocated CLI startup."""
import argparse
import json
import os
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
    report = {"native_arch": os.uname().machine, "relocated_cli": False,
              "guest_boot": "not tested", "guest_jit": "not connected"}
    if os.uname().machine != "arm64":
        raise RuntimeError("native Apple Silicon runner required")
    with tempfile.TemporaryDirectory(prefix="ARAnyM relocated package ") as directory:
        root = Path(directory)
        relocated = root / app.name
        shutil.copytree(app, relocated)
        contents = relocated / "Contents"
        executable = contents / "MacOS/aranym"
        images = [executable] + list((contents / "Frameworks").iterdir())
        for image in images:
            subprocess.run(["lipo", str(image), "-verify_arch", "arm64"], check=True)
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
        result = subprocess.run([str(executable), "--help"], cwd=root, env=environment,
                                text=True, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                                timeout=45)
        (output / "relocated-startup.txt").write_text(result.stdout)
        if result.returncode != 0 or "Usage:" not in result.stdout:
            raise RuntimeError(f"relocated CLI startup failed: status {result.returncode}")
        report["relocated_cli"] = True
    (output / "package-check.json").write_text(json.dumps(report, indent=2) + "\n")
    print("PASS: native architecture, signatures, bundled dependency closure, relocated CLI startup")
    print("Guest boot, guest JIT and GBE tests remain required.")


if __name__ == "__main__":
    main()
