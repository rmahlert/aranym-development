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
import zipfile

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
    parser.add_argument("--emutos-binary-archive", type=Path, required=True)
    parser.add_argument("--emutos-source-archive", type=Path, required=True)
    args = parser.parse_args()
    if sys.platform != "darwin":
        parser.error("packaging requires native macOS tools")
    binary = args.binary.resolve(strict=True)
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
        "CFBundleDisplayName": "ARAnyM Unofficial ARM64 Interpreter",
        "CFBundleIdentifier": "org.aranym.unofficial.arm64",
        "CFBundleShortVersionString": "1.2.0",
        "CFBundleVersion": "1.2.0",
        "ARAnyMSourceCommit": sha,
        "ARAnyMGuestJIT": False,
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
    rom_archive_hash = hashlib.sha256(args.emutos_binary_archive.read_bytes()).hexdigest()
    source_archive_hash = hashlib.sha256(args.emutos_source_archive.read_bytes()).hexdigest()
    if rom_archive_hash != "a7b78a8ce8dfe8b6940b8a9556808b24aae5674ff481b9b6e6388bd9bb8ec6b2":
        raise RuntimeError("EmuTOS binary archive does not match pinned official 1.4 release")
    if source_archive_hash != "98977ab2593d9ab81d6fcf9629f6781369912e3fe05c224a35147d5665749702":
        raise RuntimeError("EmuTOS source archive does not match pinned official 1.4 release")
    with zipfile.ZipFile(args.emutos_binary_archive) as archive:
        (resources / "emutos-aranym.img").write_bytes(archive.read("emutos-aranym-1.4/emutos-aranym.img"))
        (resources / "EmuTOS-LICENSE.txt").write_bytes(archive.read("emutos-aranym-1.4/doc/license.txt"))
    shutil.copy2(args.emutos_source_archive, output / "emutos-src-1.4.tar.gz")

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
    # Interpreter baseline needs no JIT entitlement.
    run("codesign", "--force", "--sign", "-", "--options", "runtime", str(app))
    run("codesign", "--verify", "--deep", "--strict", str(app))
    manifest = {"source_commit": sha, "architecture": "arm64", "guest_jit": False,
                "slirp": False, "signing": "ad-hoc; not notarized", "libraries": records,
                "dynamic_libraries": dynamic_libraries,
                "emutos": {"version": "1.4", "binary_archive_sha256": rom_archive_hash,
                           "source_archive_sha256": source_archive_hash}}
    (output / "build-manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    (output / "homebrew-dependencies.json").write_text(run("brew", "info", "--json=v2", "--installed") + "\n")
    (output / "README.txt").write_text(
        "Unofficial ARAnyM ARM64 interpreter development artifact.\n"
        "Guest ARM64 JIT and SLIRP are not connected in this baseline.\n"
        "Ad-hoc signed, not notarized. No guest desktop acceptance yet.\n"
        f"Source commit: {sha}\n"
        "Exact ARAnyM source archive accompanies this artifact.\n"
        "Official EmuTOS 1.4 image; matching emutos-src-1.4.tar.gz accompanies this artifact.\n"
        "Homebrew dependency versions/source URLs are recorded in homebrew-dependencies.json.\n")
    print(app)


if __name__ == "__main__":
    main()
