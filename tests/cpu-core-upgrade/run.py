#!/usr/bin/env python3
"""Build a bounded subset of unmodified generated WinUAE 68040 handlers.

SPDX-License-Identifier: GPL-2.0-or-later
Sources stay external. Every build regenerates decoder tables and handlers from
one verified source pin. No network operations occur in this build script.
"""
import argparse
import hashlib
import json
import os
import platform
from pathlib import Path
import re
import subprocess
import sys

PIN = "d82bc2549f27943d11dc59482d5388daf1c0f337"
ORIGIN = "https://github.com/tonioni/WinUAE.git"
HERE = Path(__file__).resolve().parent
FAMILIES = (0x1018, 0x1020, 0x2010, 0x2080, 0x4e71, 0x4e75,
            0x4e90, 0x4ed0, 0x5080, 0x7000,
            0x9100, 0x9140, 0x9180, 0xd100, 0xd140, 0xd180)


def command(args, *, cwd=None, output=None):
    subprocess.run([str(x) for x in args], cwd=cwd, stdout=output, check=True)


def git(upstream, *args):
    return subprocess.check_output(["git", "-C", str(upstream), *args], text=True).strip()


def extract_function(text, name):
    match = re.search(r"^uae_u32 REGPARAM2 " + re.escape(name) +
                      r"\(uae_u32 opcode\)\n\{", text, re.M)
    if not match:
        raise RuntimeError("Missing generated handler: " + name)
    # Generated handlers end with a brace at column zero, and have no literal
    # strings with multiline closing braces. Require exactly that boundary.
    end = text.index("\n}", match.end()) + 2
    return text[match.start():end]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--upstream", required=True, type=Path)
    parser.add_argument("--build-dir", type=Path, default=HERE / "build")
    parser.add_argument("--sanitize", action="store_true")
    parser.add_argument("--benchmark", action="store_true", help="record handler-only throughput; not emulator speed")
    args = parser.parse_args()
    upstream = args.upstream.resolve()
    build = args.build_dir.resolve()
    build.mkdir(parents=True, exist_ok=True)
    if git(upstream, "rev-parse", "HEAD") != PIN:
        raise RuntimeError("Upstream HEAD does not match pinned " + PIN)
    inputs = ["build68k.cpp", "readcpu.cpp", "gencpu.cpp", "table68k",
              "include/readcpu.h", "include/uae/types.h"]
    dirty = git(upstream, "status", "--porcelain", "--", *inputs)
    if dirty:
        raise RuntimeError("Modified upstream generator input: " + dirty)
    cxx = os.environ.get("CXX", "c++")
    hostflags = ["-std=c++17", "-O2", "-I" + str(HERE / "host"),
                 "-I" + str(upstream / "include")]
    command([cxx, *hostflags, upstream / "build68k.cpp", "-o", build / "build68k"])
    with (upstream / "table68k").open("rb") as source, (build / "cpudefs.cpp").open("wb") as out:
        subprocess.run([str(build / "build68k")], stdin=source, stdout=out, check=True)
    command([cxx, *hostflags, upstream / "readcpu.cpp", upstream / "gencpu.cpp",
             build / "cpudefs.cpp", "-o", build / "gencpu"])
    command([build / "gencpu"], cwd=build)
    command([cxx, *hostflags, upstream / "readcpu.cpp", build / "cpudefs.cpp",
             HERE / "decode.cpp", "-o", build / "decode"])
    decoded = subprocess.check_output([str(build / "decode")], text=True)
    # Family 41 is the generator's fast direct 68040 table. It legitimately
    # shares unchanged handlers from family 40 (68060); use the actual table,
    # never infer the handler suffix or substitute a hand-written decoder.
    tables = (build / "cpustbl.cpp").read_text()
    table = tables.split("const struct cputbl op_smalltbl_41[] = {\n", 1)[1].split("\n{ 0 }};", 1)[0]
    mappings = {int(opcode, 16): handler for handler, opcode in re.findall(
        r"\{ (op_[0-9a-f]+_[0-9]+_ff), NULL, 0x([0-9a-f]+),", table)}
    selected = {opcode: mappings[opcode] for opcode in FAMILIES}
    generated = (build / "cpuemu_40.cpp").read_text()
    functions = {name: extract_function(generated, name) for name in sorted(set(selected.values()))}
    dispatch = []
    for line in decoded.splitlines():
        opcode, family = (int(value, 16) for value in line.split())
        if family in selected:
            dispatch.append((opcode, selected[family]))
    body = ["// Generated from pinned upstream; do not edit. See provenance.json.",
            "// UAE CPU generator: Copyright 1995-2000 Bernd Schmidt.",
            "// Distributed under the upstream GNU General Public License.",
            '#include "adapter.hpp"', *functions.values(),
            "void install_generated_handlers() {"]
    body += [f"    handlers[0x{opcode:04x}] = {name};" for opcode, name in dispatch]
    body += ["}", ""]
    (build / "handlers.cpp").write_text("\n\n".join(body))
    provenance = {"origin": git(upstream, "remote", "get-url", "origin"),
                  "requested_origin": ORIGIN, "commit": PIN,
                  "commit_date": git(upstream, "show", "-s", "--format=%cI", "HEAD"),
                  "generator_inputs_sha256": {name: hashlib.sha256((upstream / name).read_bytes()).hexdigest()
                                               for name in inputs},
                  "generated_cpuemu40_sha256": hashlib.sha256((build / "cpuemu_40.cpp").read_bytes()).hexdigest(),
                  "dispatch_table": "op_smalltbl_41 (fast direct 68040)",
                  "families": {f"{opcode:04x}": name for opcode, name in selected.items()},
                  "mapped_opcodes": len(dispatch), "compiler": cxx,
                  "compiler_version": subprocess.check_output([cxx, "--version"], text=True).splitlines()[0],
                  "host": platform.platform(),
                  "host_cpu": next((line.split(":", 1)[1].strip() for line in Path("/proc/cpuinfo").read_text().splitlines()
                                    if line.startswith("model name")), platform.machine())
                              if Path("/proc/cpuinfo").exists() else platform.machine(),
                  "sanitizers": args.sanitize}
    (build / "provenance.json").write_text(json.dumps(provenance, indent=2) + "\n")
    flags = ["-std=c++20", "-O1" if args.sanitize else "-O2", "-g", "-fwrapv",
             "-Wall", "-Wextra", "-Wno-unused-parameter", "-Wno-unused-variable", "-I" + str(HERE),
             "-I" + str(HERE / "host"), "-I" + str(upstream / "include")]
    if args.sanitize:
        flags += ["-fsanitize=address,undefined", "-fno-sanitize-recover=all", "-fno-omit-frame-pointer"]
    command([cxx, *flags, build / "handlers.cpp", HERE / "adapter.cpp", HERE / "test.cpp",
             "-o", build / "core-harness"])
    print(f"Pinned {PIN}; {len(functions)} handler families, {len(dispatch)} mapped opcodes", flush=True)
    command([build / "core-harness"])
    for mutation in ("ccr", "pc", "a7", "memory", "access", "fault"):
        result = subprocess.run([str(build / "core-harness"), "--negative-check", mutation])
        if result.returncode != 1:
            raise RuntimeError(f"Checker mutation {mutation} returned {result.returncode}, expected 1")
    print("PASS: all six deliberately corrupted snapshots rejected", flush=True)
    if args.benchmark:
        if args.sanitize:
            raise RuntimeError("Run performance measurements without sanitizer instrumentation")
        command([cxx, *flags, build / "handlers.cpp", HERE / "adapter.cpp", HERE / "bench.cpp",
                 "-o", build / "core-bench"])
        measurement = json.loads(subprocess.check_output([str(build / "core-bench")], text=True))
        measurement["provenance"] = provenance
        measurement["compile_flags"] = flags
        (build / "benchmark.json").write_text(json.dumps(measurement, indent=2) + "\n")
        print(json.dumps({key: value for key, value in measurement.items() if key != "provenance"}), flush=True)


if __name__ == "__main__":
    try:
        main()
    except (RuntimeError, subprocess.CalledProcessError) as error:
        sys.exit(str(error))
