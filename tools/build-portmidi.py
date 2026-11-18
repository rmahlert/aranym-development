#!/usr/bin/env python3
"""Build the pinned PortMidi dependency outside the application source tree.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[1]
PIN = "8341a6713842d2374c3aad8998d43b20ad0db435"
VERSION = "2.0.7"


def sha(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", required=True, type=Path)
    parser.add_argument("--build-dir", required=True, type=Path)
    parser.add_argument("--prefix", required=True, type=Path)
    parser.add_argument("--cc", default=os.environ.get("CC", "cc"))
    parser.add_argument("--cxx", default=os.environ.get("CXX", "c++"))
    parser.add_argument("--toolchain", type=Path)
    parser.add_argument("--system-name", choices=("Linux", "Windows", "Darwin"))
    parser.add_argument("--processor")
    parser.add_argument("--macos-min-version", default=os.environ.get("MACOSX_DEPLOYMENT_TARGET"))
    parser.add_argument("--macos-architecture", choices=("arm64", "x86_64"))
    parser.add_argument("--jobs", type=int, default=min(os.cpu_count() or 1, 4))
    args = parser.parse_args()
    source = args.source.resolve(strict=True)
    build = args.build_dir.resolve()
    prefix = args.prefix.resolve()
    if build == prefix or build in prefix.parents or prefix in build.parents:
        parser.error("build directory and install prefix must be separate")
    for directory in (build, prefix):
        if directory == ROOT or ROOT in directory.parents:
            parser.error("build directory and install prefix must be outside the repository")
        if directory == source or source in directory.parents or directory in source.parents:
            parser.error("build/install directories must be separate from the dependency source")
    if args.jobs < 1:
        parser.error("--jobs must be positive")
    pin = subprocess.check_output(["git", "-C", str(source), "rev-parse", "HEAD"], text=True).strip()
    if pin != PIN:
        raise RuntimeError("PortMidi source does not match the required pin " + PIN)
    status = subprocess.check_output(["git", "-C", str(source), "status", "--porcelain", "--untracked-files=no"], text=True)
    if status.strip():
        raise RuntimeError("PortMidi source has tracked modifications")
    cc = shlex.split(args.cc)
    cxx = shlex.split(args.cxx)
    if len(cc) != 1 or len(cxx) != 1:
        parser.error("--cc and --cxx must name compiler executables; use a CMake toolchain for wrappers")
    cc_path = shutil.which(cc[0])
    cxx_path = shutil.which(cxx[0])
    if not cc_path or not cxx_path:
        raise RuntimeError("C/C++ compiler is unavailable")
    target = subprocess.check_output([cc_path, "-dumpmachine"], text=True).strip()
    owner = {"source_pin": PIN, "version": VERSION, "target": target,
             "cc": cc_path, "cxx": cxx_path, "system": args.system_name,
             "processor": args.processor, "macos_minimum": args.macos_min_version,
             "macos_architecture": args.macos_architecture,
             "toolchain_sha256": sha(args.toolchain) if args.toolchain else None}
    for directory, marker_name in ((build, "owned-portmidi-build.json"), (prefix, "owned-portmidi-prefix.json")):
        marker = directory / marker_name
        if directory.exists() and any(directory.iterdir()):
            if not marker.is_file() or json.loads(marker.read_text()) != owner:
                raise RuntimeError("refusing an unowned or differently configured directory: " + str(directory))
        directory.mkdir(parents=True, exist_ok=True)
        marker.write_text(json.dumps(owner, indent=2) + "\n")
    command = ["cmake", "-S", str(source), "-B", str(build),
               "-DCMAKE_BUILD_TYPE=Release", "-DBUILD_SHARED_LIBS=ON",
               "-DBUILD_PORTMIDI_TESTS=OFF", "-DBUILD_DOC=OFF",
               "-DBUILD_JAVA_NATIVE_INTERFACE=OFF", "-DPM_CHECK_ERRORS=OFF",
               "-DCMAKE_INSTALL_PREFIX=" + str(prefix), "-DCMAKE_INSTALL_LIBDIR=lib",
               "-DCMAKE_C_COMPILER=" + cc_path, "-DCMAKE_CXX_COMPILER=" + cxx_path]
    if args.toolchain:
        command.append("-DCMAKE_TOOLCHAIN_FILE=" + str(args.toolchain.resolve(strict=True)))
    if args.system_name:
        command.append("-DCMAKE_SYSTEM_NAME=" + args.system_name)
        if args.processor:
            command.append("-DCMAKE_SYSTEM_PROCESSOR=" + args.processor)
    elif "mingw" in target:
        command.extend(["-DCMAKE_SYSTEM_NAME=Windows",
                        "-DCMAKE_SYSTEM_PROCESSOR=" + target.split("-", 1)[0]])
    if args.macos_min_version:
        command.append("-DCMAKE_OSX_DEPLOYMENT_TARGET=" + args.macos_min_version)
    if args.macos_architecture:
        command.append("-DCMAKE_OSX_ARCHITECTURES=" + args.macos_architecture)
    commands = [command, ["cmake", "--build", str(build), "--parallel", str(args.jobs)],
                ["cmake", "--install", str(build)]]
    for command in commands:
        subprocess.run(command, check=True)
    if subprocess.check_output(["git", "-C", str(source), "rev-parse", "HEAD"], text=True).strip() != PIN or subprocess.check_output(
            ["git", "-C", str(source), "status", "--porcelain", "--untracked-files=no"], text=True).strip():
        raise RuntimeError("PortMidi source changed during the build")
    license_path = prefix / "share/licenses/portmidi/license.txt"
    license_path.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(source / "license.txt", license_path)
    source_archive = prefix / ("portmidi-" + VERSION + "-source.tar.gz")
    subprocess.run(["git", "-C", str(source), "archive", "--format=tar.gz",
                    "--prefix=portmidi-" + VERSION + "/", "-o", str(source_archive), PIN], check=True)
    source_files = subprocess.check_output(["git", "-C", str(source), "ls-files", "-z"]).decode().split("\0")
    libraries = [path for path in prefix.rglob("*") if path.is_file() and
                 "portmidi" in path.name.lower() and
                 (path.name.endswith((".dll", ".dylib")) or ".so" in path.name)]
    if not libraries:
        raise RuntimeError("PortMidi shared library was not installed")
    record = {"project": "PortMidi", "version": VERSION,
              "source_url": "https://github.com/PortMidi/portmidi", "source_pin": PIN,
              "target": target, "commands": commands,
              "compiler": subprocess.check_output([cc_path, "--version"], text=True).splitlines()[0],
              "license": {"path": str(license_path.relative_to(prefix)), "sha256": sha(license_path)},
              "source_archive": {"path": source_archive.name, "sha256": sha(source_archive)},
              "source_sha256": {name: sha(source / name) for name in source_files if name and (source / name).is_file()},
              "files": {str(path.relative_to(prefix)): sha(path) for path in sorted(prefix.rglob("*"))
                        if path.is_file() and path.name != "provenance.json"}}
    (prefix / "provenance.json").write_text(json.dumps(record, indent=2) + "\n")
    print(prefix, flush=True)


if __name__ == "__main__":
    main()
