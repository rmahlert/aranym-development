#!/usr/bin/env python3
"""Build a relocatable native macOS development bundle."""
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
sys.dont_write_bytecode = True
from portmidi_dist import PortMidiDistribution

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
    parser.add_argument("--portmidi-prefix", type=Path, required=True)
    parser.add_argument("--jit-build-dir", type=Path, help="require the actual guest JIT build")
    parser.add_argument("--architecture", choices=("arm64", "x86_64"), default="arm64")
    parser.add_argument("--x64-fpu-jit", "--x64-fpu-jit-test", dest="x64_fpu_jit", action="store_true")
    parser.add_argument("--arm-fpu-jit", "--arm-fpu-jit-test", dest="arm_fpu_jit", action="store_true")
    args = parser.parse_args()
    if sys.platform != "darwin":
        parser.error("packaging requires native macOS tools")
    binary = args.binary.resolve(strict=True)
    portmidi = PortMidiDistribution(args.portmidi_prefix)
    pm_library = portmidi.library("darwin")
    if not any("libportmidi" in name for name in dependencies(binary)):
        raise RuntimeError("Mac application package requires linked PortMidi device support")
    architecture = args.architecture
    if os.uname().machine != architecture:
        parser.error("packaging requires a native runner matching --architecture")
    if args.arm_fpu_jit and architecture != "arm64":
        parser.error("ARM FPU packaging requires arm64")
    if args.x64_fpu_jit and architecture != "x86_64":
        parser.error("x64 FPU packaging requires x86_64")
    fpu_jit = args.arm_fpu_jit or args.x64_fpu_jit
    guest_jit = args.jit_build_dir is not None
    if guest_jit:
        build = args.jit_build_dir.resolve(strict=True)
        flags = (build / "Makefile").read_text()
        if binary != (build / "src/aranym").resolve() or not all(flag in flags for flag in ("-DUSE_JIT=1", "-DARANYM_ARM64" if architecture == "arm64" else "-DCPU_x86_64")):
            parser.error("JIT packaging requires the actual target JIT build binary and flags")
    if args.arm_fpu_jit and (not guest_jit or '-DARANYM_ARM_FPU_JIT=1' not in flags):
        parser.error('ARM FPU packaging requires the connected native FPU build')
    if args.x64_fpu_jit and (not guest_jit or '-DMODERN040_FPU_JIT' not in flags):
        parser.error('x64 FPU packaging requires the connected modern040 FPU build')
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
        "CFBundleDisplayName": "ARAnyM 1.2.0 Unofficial Beta 2" if fpu_jit else "ARAnyM Unofficial ARM64 JIT Integration" if guest_jit else "ARAnyM 1.2.0 Unofficial Beta 1 ARM64 Interpreter",
        "CFBundleIdentifier": "org.aranym.unofficial." + architecture,
        "CFBundleShortVersionString": "1.2.0",
        "CFBundleVersion": "1.2.0",
        "ARAnyMSourceCommit": sha,
        "ARAnyMBuildLabel": "1.2.0 Unofficial Beta 2" if fpu_jit else architecture + " JIT integration" if guest_jit else "1.2.0 Unofficial Beta 1",
        "ARAnyMGuestJIT": guest_jit,
        "ARAnyMFPUJITAvailable":fpu_jit,
        "ARAnyMArchitecture": architecture,
        "LSMinimumSystemVersion": "15.0" if fpu_jit else os.environ.get("MACOSX_DEPLOYMENT_TARGET", "11.0"),
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
    portmidi.stage_notices(resources, output)

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
        if (not resolved.is_relative_to(brew_prefix) and resolved != pm_library) or ".framework" in str(resolved):
            raise RuntimeError(f"unsupported dependency outside approved prefixes: {resolved}")
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
        run("lipo", str(packaged), "-verify_arch", architecture)
        search = rpaths(original) + main_rpaths + [str(portmidi.prefix / "lib")]

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

    minimum = tuple(int(part) for part in info["LSMinimumSystemVersion"].split("."))
    for image in [executable] + [p for p in frameworks.iterdir() if not p.is_symlink()]:
        commands = run("otool", "-l", str(image))
        versions = re.findall(r"cmd LC_BUILD_VERSION\s+cmdsize \d+\s+platform \S+\s+minos ([0-9.]+)", commands)
        versions += re.findall(r"cmd LC_VERSION_MIN_MACOSX\s+cmdsize \d+\s+version ([0-9.]+)", commands)
        for version in versions:
            minimum = max(minimum, tuple(int(part) for part in version.split(".")))
    info["LSMinimumSystemVersion"] = ".".join(map(str, minimum))
    with (contents / "Info.plist").open("wb") as stream:
        plistlib.dump(info, stream)

    cellar = Path(run("brew", "--cellar")).resolve(strict=True)
    formula_names = set()
    notices = {}
    for library in sorted(set(copied.values())):
        if not library.is_relative_to(cellar):
            continue
        formula, version, *_ = library.relative_to(cellar).parts
        if formula in formula_names:
            continue
        formula_names.add(formula)
        keg = cellar / formula / version
        notices[formula] = []
        for notice in sorted(keg.iterdir()):
            if not re.fullmatch(r"(?:licenses?|licences?|copying|copyright|notices?)(?:[._-].*)?",
                                notice.name, re.IGNORECASE):
                continue
            if not notice.is_file() or not notice.resolve().is_relative_to(keg):
                continue
            destination = resources / "Licenses" / formula / notice.name
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(notice, destination)
            notices[formula].append(notice.name)

    for library in frameworks.iterdir():
        if not library.is_symlink():
            run("codesign", "--force", "--sign", "-", str(library))
    if not any(name.startswith("libslirp") for name in copied):
        raise RuntimeError("Mac networking package requires a linked and bundled libslirp")
    if not any(name.startswith("libportmidi") for name in copied):
        raise RuntimeError("Mac MIDI package requires a linked and bundled PortMidi runtime")
    entitlements = "src/gui-osx/jit-arm64.entitlements.plist" if architecture == "arm64" else "src/gui-osx/jit-x86_64.entitlements.plist"
    signing_args = ["--entitlements", str(ROOT / entitlements)] if guest_jit else []
    run("codesign", "--force", "--sign", "-", "--options", "runtime", *signing_args, str(app))
    run("codesign", "--verify", "--deep", "--strict", str(app))
    manifest = {"release": info["ARAnyMBuildLabel"], "source_commit": sha, "architecture": architecture, "guest_jit": guest_jit,
                "fpu_jit_available":fpu_jit,"fpu_jit_default":False,
                "minimum_macos":info["LSMinimumSystemVersion"],
                "slirp": True, "portmidi": portmidi.summary(), "signing": "ad-hoc; not notarized", "libraries": records,
                "dynamic_libraries": dynamic_libraries, "homebrew_notices": notices,
                "emutos": firmware}
    (output / "build-manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    (output / "homebrew-dependencies.json").write_text(run("brew", "info", "--json=v2", "--formula", *sorted(formula_names)) + "\n")
    introduction = ("ARAnyM " + architecture + " JIT integration build; guest correctness gates remain required. SLIRP is included.\n" if guest_jit else
                    "ARAnyM 1.2.0 Unofficial Beta 1 - ARM64 interpreter.\nGuest ARM64 JIT is not connected in this baseline. SLIRP is included.\n")
    if fpu_jit:
        introduction = f"ARAnyM 1.2.0 Unofficial Beta 2 for macOS {architecture}.\n"
    (output / "README.txt").write_text(
        introduction +
        ("Intel FPU JIT: opt in with JITFPU = Yes and JITSafe = Yes. The x86-64 JIT uses extended precision with a pinned SoftFloat fallback. Enabled FPSP arithmetic exception continuation remains unsupported.\n" if args.x64_fpu_jit else "")+
        ("ARM FPU JIT: opt in with JITFPU = Yes and JITSafe = Yes. Native arithmetic retains the existing ARM IEEE64 precision; other modes/values use the interpreter. Requires macOS15+.\n" if args.arm_fpu_jit else "")+
        "Select SLIRP in Network settings or use the bundled slirp.config.example ETH0 settings.\n"
        "Guest: 10.0.2.15/24, gateway 10.0.2.2, DNS 10.0.2.3; requires the MiNT NatFeat Ethernet driver.\n"
        "Ad-hoc signed, not notarized.\n"
        f"Source commit: {sha}\n"
        "Exact ARAnyM source archive accompanies this artifact.\n"
        "EmuTOS snapshot built from pinned upstream source; matching source archive and build recipe included.\n"
        "Homebrew dependency versions/source URLs are recorded in homebrew-dependencies.json.\n")
    print(app)


if __name__ == "__main__":
    main()
