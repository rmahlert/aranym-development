#!/usr/bin/env python3
"""Build a relocatable ARM64 interpreter development bundle on macOS."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import plistlib
import re
import shutil
import subprocess
import sys
import tarfile

ROOT = Path(__file__).resolve().parents[1]


def run(*args):
    return subprocess.check_output(args, text=True).strip()


def dependencies(path):
    return [line.strip().split(" (compatibility version", 1)[0]
            for line in run("otool", "-L", str(path)).splitlines()[1:]]


def rpaths(path):
    return re.findall(r"cmd LC_RPATH\s+cmdsize \d+\s+path (.*?) \(offset",
                      run("otool", "-l", str(path)))


def system_library(name):
    return name.startswith(("/System/Library/", "/usr/lib/"))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--emutos-source-archive", type=Path, required=True)
    parser.add_argument("--jit-build-dir", type=Path, help="require and package the experimental ARM64 JIT build")
    args = parser.parse_args()
    if sys.platform != "darwin":
        parser.error("packaging requires native macOS tools")
    binary = args.binary.resolve(strict=True)
    guest_jit = args.jit_build_dir is not None
    if guest_jit:
        build = args.jit_build_dir.resolve(strict=True)
        flags = (build / "Makefile").read_text()
        if binary != (build / "src/aranym").resolve() or not all(flag in flags for flag in ("-DUSE_JIT=1", "-DARANYM_ARM64")):
            parser.error("JIT packaging requires the actual ARM64 JIT build binary and flags")
    output = args.output.resolve()
    app = output / "ARAnyM-Unofficial.app"
    if app.exists():
        parser.error(f"output bundle already exists: {app}")
    contents = app / "Contents"
    frameworks = contents / "Frameworks"
    resources = contents / "Resources"
    executable = contents / "MacOS" / "aranym"
    for directory in (frameworks, resources, executable.parent):
        directory.mkdir(parents=True, exist_ok=True)
    shutil.copy2(binary, executable)
    sha = run("git", "-C", str(ROOT), "rev-parse", "HEAD")
    if run("git", "-C", str(ROOT), "status", "--porcelain", "--untracked-files=no"):
        parser.error("commit source changes before packaging")
    with (ROOT / "src/gui-osx/Info.plist").open("rb") as stream:
        info = plistlib.load(stream)
    info.update({
        "CFBundleExecutable": "aranym",
        "CFBundleName": "ARAnyM Unofficial",
        "CFBundleDisplayName": "ARAnyM Unofficial ARM64 JIT Integration" if guest_jit else "ARAnyM 1.2.0 Unofficial Beta 1 ARM64 Interpreter",
        "CFBundleIdentifier": "org.aranym.unofficial.arm64",
        "CFBundleShortVersionString": "1.2.0",
        "CFBundleVersion": "1.2.0",
        "ARAnyMSourceCommit": sha,
        "ARAnyMBuildLabel": "ARM64 JIT integration" if guest_jit else "1.2.0 Unofficial Beta 1",
        "ARAnyMGuestJIT": guest_jit,
        "LSMinimumSystemVersion": os.environ.get("MACOSX_DEPLOYMENT_TARGET", "11.0"),
    })
    info.pop("LSMinimumSystemVersionByArchitecture", None)
    with (contents / "Info.plist").open("wb") as stream:
        plistlib.dump(info, stream)
    for localization in (ROOT / "src/gui-osx").glob("*.lproj"):
        shutil.copytree(localization, resources / localization.name)
    for source in (ROOT / "src/gui-osx/aranym.icns", ROOT / "data/logo.bmp",
                   ROOT / "data/wm_icon.bmp",
                   ROOT / "COPYING", ROOT / "README.md"):
        shutil.copy2(source, resources / source.name)
    shutil.copy2(ROOT / "doc/config", resources / "config.example")
    (resources / "slirp.config.example").write_text(
        "[ETH0]\nType = slirp\nTunnel =\nHostIP = 10.0.2.2\n"
        "AtariIP = 10.0.2.15\nNetmask = 255.255.255.0\nMAC = 52:54:00:12:34:56\n")
    firmware = json.loads((ROOT / "data/emutos-macos.json").read_text())
    image_path = ROOT / "data" / firmware["image"]
    image_hash = hashlib.sha256(image_path.read_bytes()).hexdigest()
    source_archive_hash = hashlib.sha256(args.emutos_source_archive.read_bytes()).hexdigest()
    if image_hash != firmware["image_sha256"]:
        raise RuntimeError("EmuTOS image does not match its pinned build provenance")
    if source_archive_hash != firmware["source_archive_sha256"]:
        raise RuntimeError("EmuTOS source archive does not match its pinned source")
    shutil.copy2(image_path, resources / "emutos-aranym.img")
    shutil.copy2(ROOT / "data/emutos-macos.json", resources / "EmuTOS-BUILD.json")
    with tarfile.open(args.emutos_source_archive) as archive:
        license_member = next(m for m in archive.getmembers() if m.name.endswith("/doc/license.txt"))
        (resources / "EmuTOS-LICENSE.txt").write_bytes(archive.extractfile(license_member).read())
    shutil.copy2(args.emutos_source_archive, output / "emutos-e0f0c233-source.tar.gz")

    # Resolve dependencies before rewriting their load commands. Framework
    # dependencies outside the system SDK are deliberately rejected here.
    brew_prefix = Path(run("brew", "--prefix")).resolve()
    copied = {}
    queue = [(binary, executable)]
    records = []
    dynamic_libraries = []
    main_rpaths = rpaths(binary)

    def stage_library(resolved):
        resolved = resolved.resolve(strict=True)
        if not resolved.is_relative_to(brew_prefix) or ".framework" in str(resolved):
            raise RuntimeError(f"unsupported non-Homebrew dependency: {resolved}")
        destination = frameworks / resolved.name
        if destination.name in copied and copied[destination.name] != resolved:
            raise RuntimeError(f"conflicting library names: {destination.name}")
        if destination.name not in copied:
            copied[destination.name] = resolved
            shutil.copy2(resolved, destination)
            destination.chmod(destination.stat().st_mode | 0o200)
            run("install_name_tool", "-id", "@rpath/" + destination.name, str(destination))
            queue.append((resolved, destination))
            records.append({"source": str(resolved), "packaged": destination.name,
                            "source_sha256": hashlib.sha256(resolved.read_bytes()).hexdigest()})
        return destination

    while queue:
        original, packaged = queue.pop(0)
        run("lipo", str(packaged), "-verify_arch", "arm64")
        search = rpaths(original) + main_rpaths

        def expand(name):
            return Path(name.replace("@loader_path", str(original.parent))
                        .replace("@executable_path", str(binary.parent)))

        install_id = None
        if packaged != executable:
            ids = run("otool", "-D", str(original)).splitlines()[1:]
            install_id = ids[0].strip() if ids else None
        for name in dependencies(original):
            if name == install_id:
                continue
            if system_library(name):
                continue
            candidates = ([expand(base) / name[len("@rpath/"):]
                           for base in search] if name.startswith("@rpath/")
                          else [expand(name)])
            resolved = next((p.resolve() for p in candidates if p.is_file()), None)
            if resolved is None:
                raise RuntimeError(f"unresolved dependency {name} in {original}")
            destination = stage_library(resolved)
            if "sdl2-compat" in resolved.parts and not dynamic_libraries:
                # SDL2-compat dlopens @loader_path/libSDL3.dylib before main().
                # It is absent from otool -L, but is a required runtime input.
                sdl3 = stage_library(Path(run("brew", "--prefix", "sdl3")) / "lib/libSDL3.dylib")
                alias = frameworks / "libSDL3.dylib"
                if alias != sdl3:
                    alias.symlink_to(sdl3.name)
                dynamic_libraries.append(alias.name)
            replacement = ("@executable_path/../Frameworks/" if packaged == executable
                           else "@loader_path/") + destination.name
            run("install_name_tool", "-change", name, replacement, str(packaged))
        # All non-system dependencies now have explicit bundle-relative paths.
        for path in rpaths(packaged):
            run("install_name_tool", "-delete_rpath", path, str(packaged))

    for library in frameworks.iterdir():
        if not library.is_symlink():
            run("codesign", "--force", "--sign", "-", str(library))
    if not any(name.startswith("libslirp") for name in copied):
        raise RuntimeError("Mac networking package requires a linked and bundled libslirp")
    signing_args = ["--entitlements", str(ROOT / "src/gui-osx/jit-arm64.entitlements.plist")] if guest_jit else []
    run("codesign", "--force", "--sign", "-", "--options", "runtime", *signing_args, str(app))
    run("codesign", "--verify", "--deep", "--strict", str(app))
    manifest = {"source_commit": sha, "architecture": "arm64", "guest_jit": guest_jit,
                "slirp": True, "signing": "ad-hoc; not notarized", "libraries": records,
                "dynamic_libraries": dynamic_libraries,
                "emutos": firmware}
    (output / "build-manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    (output / "homebrew-dependencies.json").write_text(run("brew", "info", "--json=v2", "--installed") + "\n")
    introduction = ("ARAnyM ARM64 JIT integration build; guest correctness gates remain required. SLIRP is included.\n" if guest_jit else
                    "ARAnyM 1.2.0 Unofficial Beta 1 - ARM64 interpreter.\nGuest ARM64 JIT is not connected in this baseline. SLIRP is included.\n")
    (output / "README.txt").write_text(
        introduction +
        "Select SLIRP in Network settings or use the bundled slirp.config.example ETH0 settings.\n"
        "Guest: 10.0.2.15/24, gateway 10.0.2.2, DNS 10.0.2.3; requires the MiNT NatFeat Ethernet driver.\n"
        "Ad-hoc signed, not notarized. No guest desktop acceptance yet.\n"
        f"Source commit: {sha}\n"
        "Exact ARAnyM source archive accompanies this artifact.\n"
        "EmuTOS snapshot built from pinned upstream source; matching source archive and build recipe included.\n"
        "Homebrew dependency versions/source URLs are recorded in homebrew-dependencies.json.\n")
    print(app)


if __name__ == "__main__":
    main()
