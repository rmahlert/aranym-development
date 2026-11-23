#!/usr/bin/env python3
"""Package qualified Linux x86-64 or Raspberry Pi guest JIT application builds."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tarfile
sys.dont_write_bytecode = True
from portmidi_dist import PortMidiDistribution

ROOT = Path(__file__).resolve().parents[1]
VERSION = "1.2.0~unofficial.beta1"
PACKAGE = "aranym-unofficial"


def run(*args, **kwargs):
    return subprocess.check_output(args, text=True, **kwargs).strip()


def sha(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, required=True)
    parser.add_argument("--slirp-library", type=Path, required=True)
    parser.add_argument("--slirp-source-archive", type=Path, required=True)
    parser.add_argument("--emutos-source-archive", type=Path, required=True)
    parser.add_argument("--winuae-source-archive", type=Path)
    parser.add_argument("--portmidi-prefix", type=Path, required=True)
    parser.add_argument("--architecture", choices=("amd64", "arm64", "armhf"), default="amd64")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument('--x64-fpu-jit', '--fpu-jit-test', dest='x64_fpu_jit', action='store_true')
    parser.add_argument('--arm-fpu-jit', '--arm-fpu-jit-test', dest='arm_fpu_jit', action='store_true')
    parser.add_argument('--dsp-midi-preview',action='store_true',help='package the Raspberry Pi DSP/MIDI preview with ARM FPU JIT support')
    cross_options = parser.add_mutually_exclusive_group()
    cross_options.add_argument('--cross-armhf',action='store_true',help='cross-build ARM32 package; requires explicit QEMU qualification separately')
    cross_options.add_argument('--cross-arm64',action='store_true',help='cross-build ARM64 package; requires explicit QEMU qualification separately')
    args = parser.parse_args()
    if sys.platform != "linux":
        parser.error("Debian packaging requires Linux")
    arch=args.architecture
    cross = args.cross_armhf or args.cross_arm64
    arm_fpu_available = args.arm_fpu_jit or args.dsp_midi_preview
    if args.x64_fpu_jit and arch!='amd64':
        parser.error('--x64-fpu-jit requires amd64')
    if arm_fpu_available and arch not in ('arm64','armhf'):
        parser.error('ARM FPU and DSP/MIDI preview packages require an ARM target')
    if args.cross_armhf and arch!='armhf':
        parser.error('--cross-armhf is only for ARM32')
    if args.cross_arm64 and arch!='arm64':
        parser.error('--cross-arm64 is only for ARM64')
    if not cross and run("dpkg", "--print-architecture") != arch:
        parser.error("use a packaging environment with the requested Debian userspace architecture")
    if arch=="amd64" and not args.winuae_source_archive:
        parser.error("modern040 x86-64 requires --winuae-source-archive")
    label="Unofficial Beta 1" if arch=="amd64" else "Unofficial ARM JIT Test 1"
    version=VERSION if arch=="amd64" else "1.2.0~unofficial.armjit.test1"
    package_basename=("aranym-1.2.0-unofficial-beta1-linux-amd64" if arch=="amd64" else
          "aranym-1.2.0-unofficial-arm-jit-test1-rpi-"+arch)
    backend={"amd64":"x86-64 modern040 JIT", "arm64":"ARM64 guest JIT", "armhf":"ARM32 guest JIT"}[arch]
    if args.x64_fpu_jit or args.arm_fpu_jit:
        label = "Unofficial Beta 3"
        version = "1.2.0~unofficial.release.beta3"
        platform_name = "linux-amd64" if arch == "amd64" else "rpi-" + arch
        package_basename = "aranym-1.2.0-unofficial-beta3-" + platform_name
    if args.dsp_midi_preview:
        label="Unofficial DSP/MIDI Preview 2"
        version="1.2.0~unofficial.dspmidi.preview2"
        package_basename="aranym-1.2.0-unofficial-dsp-midi-preview2-rpi-"+arch
    fpu_available=args.x64_fpu_jit or arm_fpu_available
    fpu_description="guarded native FPU JIT available; opt-in" if fpu_available else "FPU JIT disabled"
    commit = run("git", "-C", str(ROOT), "rev-parse", "HEAD")
    changed = run("git", "-C", str(ROOT), "status", "--porcelain", "--untracked-files=no")
    if changed:
        print(changed, file=sys.stderr)
        print(run("git", "-C", str(ROOT), "diff", "--stat"), file=sys.stderr)
        parser.error("commit source changes before packaging")
    build = args.build_dir.resolve(strict=True)
    binary = build / "src/aranym"
    config = (build / "config.h").read_text()
    for macro in ("HAVE_SLIRP", "ETHERNET_SUPPORT", "HAVE_PORTMIDI"):
        if not re.search(r"^#define " + macro + r" 1$", config, re.M):
            raise RuntimeError("missing build feature: " + macro)
    flags=(build / "Makefile").read_text()
    required={"amd64":"-DEXPERIMENTAL_UPSTREAM_MODERN040", "arm64":"-DARANYM_ARM64", "armhf":"-DARANYM_ARM32"}[arch]
    if required not in flags or "-DUSE_JIT=1" not in flags:
        raise RuntimeError("missing requested guest JIT build profile")
    if args.x64_fpu_jit and not all(x in flags for x in ('-DUSE_JIT_FPU=1','-DMODERN040_FPU_JIT')):
        raise RuntimeError('FPU JIT package requires the actual connected FPU build')
    if arm_fpu_available and '-DARANYM_ARM_FPU_JIT=1' not in flags:
        raise RuntimeError('ARM FPU package requires the connected native FPU build')
    elf=run("readelf", "-h", str(binary))
    machine=re.search(r"^\s*Machine:\s*(.*?)$",elf,re.M).group(1)
    if machine != {"amd64":"Advanced Micro Devices X86-64", "arm64":"AArch64", "armhf":"ARM"}[arch]:
        raise RuntimeError("binary machine does not match requested architecture")
    if "libslirp.so.0" not in run("readelf", "-d", str(binary)):
        raise RuntimeError("binary does not link libslirp")
    portmidi = PortMidiDistribution(args.portmidi_prefix)
    pm_library = portmidi.library("linux")
    if "libportmidi.so.2" not in run("readelf", "-d", str(binary)):
        raise RuntimeError("binary does not link the pinned PortMidi runtime")
    pm_machine = re.search(r"^\s*Machine:\s*(.*?)$", run("readelf", "-h", str(pm_library)), re.M).group(1)
    if pm_machine != machine:
        raise RuntimeError("PortMidi library architecture differs from the application")
    if "[/usr/lib/aranym-unofficial]" not in run("readelf", "-d", str(binary)):
        raise RuntimeError("configure LDFLAGS with -Wl,-rpath,/usr/lib/aranym-unofficial")
    if "--datadir=/usr/share/aranym-unofficial" not in run(str(build / "config.status"), "--config"):
        raise RuntimeError("configure with --datadir=/usr/share/aranym-unofficial")
    output = args.output.resolve()
    stage = output / "deb-root"
    if stage.exists():
        parser.error("choose an output without an existing deb-root")
    output.mkdir(parents=True, exist_ok=True)
    data = stage / "usr/share/aranym-unofficial/aranym"
    doc = stage / "usr/share/doc/aranym-unofficial"
    lib = stage / "usr/lib/aranym-unofficial"
    exe = stage / "usr/bin/aranym-unofficial"
    for path in (data, doc, lib, exe.parent, stage / "DEBIAN"):
        path.mkdir(parents=True, exist_ok=True)
    shutil.copy2(binary, exe)
    triplet = {"armhf":"arm-linux-gnueabihf", "arm64":"aarch64-linux-gnu"}.get(arch)
    subprocess.run([triplet+"-strip" if cross else "strip", str(exe)], check=True)
    shutil.copy2(args.slirp_library.resolve(strict=True), lib / "libslirp.so.0")
    shutil.copy2(pm_library, lib / "libportmidi.so.2")
    # Fixed, reviewed allowlist: no guest hard disk, user config or applications.
    for name in ("logo.bmp", "wm_icon.bmp"):
        shutil.copy2(ROOT / "data" / name, data / name)
    shutil.copy2(ROOT / "doc/config", data / "config.example")
    shutil.copy2(ROOT / "COPYING", doc / "copyright")
    firmware = json.loads((ROOT / "data/emutos-macos.json").read_text())
    image = ROOT / "data/emutos-macos-reference.img"
    if sha(image) != firmware["image_sha256"] or sha(args.emutos_source_archive) != firmware["source_archive_sha256"]:
        raise RuntimeError("firmware/source pair does not match pinned provenance")
    shutil.copy2(image, data / "emutos-aranym.img")
    shutil.copy2(ROOT / "data/emutos-macos.json", doc / "EmuTOS-BUILD.json")
    with tarfile.open(args.emutos_source_archive) as archive:
        license_file = next(m for m in archive.getmembers() if m.name.endswith("/doc/license.txt"))
        (doc / "EmuTOS-LICENSE.txt").write_bytes(archive.extractfile(license_file).read())
    if sha(args.slirp_source_archive) != "3caff6e2de445f4995629d4929c55419f661b2b1d14f12481e155a71c1e8f811":
        raise RuntimeError("libslirp source archive does not match 4.9.1 pin")
    with tarfile.open(args.slirp_source_archive) as archive:
        license_file = next(m for m in archive.getmembers() if m.name.endswith("/COPYRIGHT"))
        (doc / "libslirp-COPYRIGHT").write_bytes(archive.extractfile(license_file).read())
    sources = output / "source"
    sources.mkdir()
    portmidi.stage_notices(doc, sources)
    subprocess.run(["git", "-C", str(ROOT), "archive", "--format=tar.gz", "-o", str(sources / "aranym-source.tar.gz"), commit], check=True)
    for source, source_name in ((args.emutos_source_archive, "emutos-e0f0c233-source.tar.gz"),
                         (args.winuae_source_archive, "winuae-d82bc254-source.tar.gz"),
                         (args.slirp_source_archive, "libslirp-v4.9.1.tar.bz2")):
        if source is not None:
            shutil.copy2(source, sources / source_name)
    # Resolve system package dependencies from the actual ELF images. The
    # private libslirp is supplied here, so exclude the self dependency only.
    (output / "debian").mkdir()
    (output / "debian/control").write_text(f"Source: aranym-unofficial\n\nPackage: aranym-unofficial\nArchitecture: {arch}\nDescription: Unofficial ARAnyM beta\n")
    shlibs = output / "debian/shlibs.local"
    shlibs.write_text("libslirp 0 aranym-unofficial\nlibportmidi 2 aranym-unofficial\n")
    dependency_env = dict(os.environ, DEB_HOST_ARCH=arch)
    dependencies = run("dpkg-shlibdeps", "-O", "-xaranym-unofficial", "-L" + str(shlibs),
                       "-l" + str(lib), *(["-l/usr/lib/"+triplet, "-l/lib/"+triplet] if cross else []),
                       "-e" + str(exe), "-e" + str(lib / "libslirp.so.0"), "-e" + str(lib / "libportmidi.so.2"), cwd=output, env=dependency_env)
    depends = next(line.split("=", 1)[1] for line in dependencies.splitlines() if line.startswith("shlibs:Depends="))
    (stage / "DEBIAN/control").write_text(
        f"Package: {PACKAGE}\nVersion: {version}\nArchitecture: {arch}\n"
        "Maintainer: ARAnyM Unofficial Community\nSection: otherosfs\nPriority: optional\n"
        f"Depends: {depends}\n"
        f"Description: ARAnyM 1.2.0 {label}\n"
        f" Experimental {backend} and SLIRP networking.\n"
        " Application and EmuTOS firmware only; no guest hard disk.\n")
    (doc / "README.txt").write_text(
        f"ARAnyM 1.2.0 {label}, Linux {arch}.\n"
        f"Install the .deb with: sudo apt install ./{package_basename}.deb\n"
        "Launch aranym-unofficial. Use -c PATH to open your existing configuration.\n"+
        (("Default profile: ~/aranym-modern040-beta1.\n"+
          ("Opt in with JITFPU = Yes in [JIT]. Guarded native arithmetic; other operations use SoftFloat. Enabled FPU exception/FPSP continuation remains unsupported.\n" if args.x64_fpu_jit else "")) if arch=="amd64" else
         "Use dpkg --print-architecture to select armhf or arm64 for your BeePi OS.\n"
         "Launch with -c PATH_TO_EXISTING_CONFIG. This package adds aranym-unofficial; point your BeePi launcher to it when ready.\n"+
         ("Opt in with JIT = Yes,JITSafe = Yes,JITFPU = Yes. ARM native arithmetic retains the existing IEEE64 register profile; other modes/values use the interpreter.\n" if arm_fpu_available else
          "Enable JIT = Yes and JITFPU = No in the JIT config section. Existing guest images/configurations are preserved.\n"))+
        "Firmware: /usr/share/aranym-unofficial/aranym/emutos-aranym.img\n"
        "SLIRP: ETH0 Type=slirp, AtariIP=10.0.2.15, HostIP=10.0.2.2, Netmask=255.255.255.0.\n"
        "Guest gateway=10.0.2.2, DNS=10.0.2.3; use the MiNT NatFeat Ethernet driver.\n"
        "MIDI Setup: choose named native or USB MIDI input and output ports; choose none to disable either direction, then Save.\n"
        "For a loopback check, connect MIDI OUT to MIDI IN. Physical MIDI hardware timing remains unverified.\n"
        f"FPU: {fpu_description}. Full desktop testing remains required.\n"
        "Corresponding source archives accompany this package.\n")
    applications = stage / "usr/share/applications"
    applications.mkdir(parents=True)
    (applications / "aranym-unofficial.desktop").write_text(
        f"[Desktop Entry]\nType=Application\nName=ARAnyM {label}\n"
        "Exec=aranym-unofficial\nIcon=aranym-unofficial\nCategories=System;Emulator;\nTerminal=false\n")
    icons = stage / "usr/share/icons/hicolor/48x48/apps"
    icons.mkdir(parents=True)
    shutil.copy2(ROOT / "contrib/icon-48.png", icons / "aranym-unofficial.png")
    manifest = {"release": "1.2.0 " + label, "debian_version": version, "source_commit": commit, "architecture": arch, "guest_jit": backend+"; "+fpu_description, "fpu_jit_available":fpu_available, "fpu_jit_default":False,
                "slirp": "4.9.1 (bundled)", "portmidi": portmidi.summary(), "cross_compiled":cross, "depends": depends, "emutos": firmware,
                "files": {str(p.relative_to(stage)): sha(p) for p in sorted(stage.rglob("*")) if p.is_file()},
                "sources": {p.name: sha(p) for p in sources.iterdir()}}
    (output / "build-manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    package = output / (package_basename+".deb")
    subprocess.run(["dpkg-deb", "--root-owner-group", "--build", str(stage), str(package)], check=True)
    print(package)


if __name__ == "__main__":
    main()
