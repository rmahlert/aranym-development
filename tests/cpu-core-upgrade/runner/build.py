#!/usr/bin/env python3
"""Build a broader isolated pinned modern040 integer runner, never an emulator.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys

HERE = Path(__file__).resolve().parent
HARNESS = HERE.parent
PIN = "d82bc2549f27943d11dc59482d5388daf1c0f337"
CALLS = set("""if while for switch sizeof
    m68k_dreg m68k_areg m68k_getpc m68k_incpc m68k_setpc_j m68k_setpc
    GET_XFLG GET_CFLG GET_NFLG GET_VFLG GET_ZFLG CLEAR_CZNV
    SET_XFLG SET_CFLG SET_NFLG SET_VFLG SET_ZFLG COPY_CARRY
    SET_ALWAYS_CFLG SET_ALWAYS_NFLG get_byte_jit get_word_jit get_long_jit
    put_byte_jit put_word_jit put_long_jit get_dibyte get_diword get_dilong
    get_disp_ea_020 cctrue check_t0_trace MakeSR MakeFromSR MakeFromSR_T0
    MakeFromSR_STOP Exception Exception_cpu_oldpc op_illg m68k_move2c
    m68k_movec2 m68k_setstopped do_cycles_stop checkint m68k_do_rts m68k_do_bsr
    exception3_read_prefetch exception3_read_prefetch_only
    exception3_read_prefetch_68040bug branch_stack_push branch_stack_pop_rts
    branch_stack_pop_rte""".split())
HELPERS = {
    "newcpu.cpp": ["activate_trace", "MakeSR", "MakeFromSR_x", "MakeFromSR_T0",
                   "MakeFromSR", "MakeFromSR_STOP", "check_t0_trace"],
    "newcpu_common.cpp": ["movec_illg", "m68k_move2c", "m68k_movec2", "get_disp_ea_020"],
    "od-unix/machdep/m68k.h": ["cctrue"],
}


def extract(source, name):
    pattern = r"^.*\b" + re.escape(name) + r"\s*\([^;\n]*\)\n\{"
    matches = list(re.finditer(pattern, source, re.M))
    if len(matches) != 1:
        raise RuntimeError(f"Expected one function {name}, found {len(matches)}")
    match = matches[0]
    depth, pos, mode = 1, match.end(), "code"
    while depth:
        if pos == len(source): raise RuntimeError("Unclosed function " + name)
        c, nxt = source[pos], source[pos:pos+2]
        if mode == "comment":
            if nxt == "*/": mode = "code"; pos += 2; continue
        elif mode == "line":
            if c == "\n": mode = "code"
        elif mode in ('"', "'"):
            if c == "\\": pos += 2; continue
            if c == mode: mode = "code"
        elif nxt == "/*": mode = "comment"; pos += 2; continue
        elif nxt == "//": mode = "line"; pos += 2; continue
        elif c in ('"', "'"): mode = c
        elif c == "{": depth += 1
        elif c == "}": depth -= 1
        pos += 1
    return source[match.start():pos]


def command(args, **kwargs):
    subprocess.run([str(a) for a in args], check=True, **kwargs)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--upstream", required=True, type=Path)
    parser.add_argument("--build-dir", type=Path, default=HARNESS / "build-runner")
    parser.add_argument("--sanitize", action="store_true")
    parser.add_argument("--reuse-generated", action="store_true", help="reuse matching harness regeneration with verified pin")
    args = parser.parse_args()
    upstream, build = args.upstream.resolve(), args.build_dir.resolve()
    build.mkdir(parents=True, exist_ok=True)
    generated = build / "harness"
    cmd = [sys.executable, HARNESS / "run.py", "--upstream", upstream, "--compare-old", "--build-dir", generated]
    if args.sanitize: cmd.append("--sanitize")
    if not args.reuse_generated: command(cmd)
    provenance = json.loads((generated / "provenance.json").read_text())
    if provenance["commit"] != PIN: raise RuntimeError("Incorrect generated core pin")
    for name, digest in provenance["generator_inputs_sha256"].items():
        if hashlib.sha256((upstream / name).read_bytes()).hexdigest() != digest:
            raise RuntimeError("Stale or modified generator input " + name)
    if hashlib.sha256((generated / "cpuemu_40.cpp").read_bytes()).hexdigest() != provenance["generated_cpuemu40_sha256"]:
        raise RuntimeError("Modified regenerated handler input")
    if subprocess.check_output(["git", "-C", str(upstream), "rev-parse", "HEAD"], text=True).strip() != PIN:
        raise RuntimeError("Incorrect upstream helper pin")
    helper_text, helper_hashes = [], {}
    for path, names in HELPERS.items():
        data = (upstream / path).read_bytes()
        tracked = subprocess.check_output(["git", "-C", str(upstream), "show", f"{PIN}:{path}"])
        if data != tracked: raise RuntimeError("Modified upstream helper input " + path)
        helper_hashes[path] = hashlib.sha256(data).hexdigest()
        helper_text += [extract(data.decode(), name) for name in names]
    tables = (generated / "cpustbl.cpp").read_text()
    table = tables.split("const struct cputbl op_smalltbl_41[] = {\n", 1)[1].split("\n{ 0 }};", 1)[0]
    rows = re.findall(r"\{ (op_[0-9a-f]+_[0-9]+_ff), NULL, 0x([0-9a-f]+),[^\n]*/\* ([^*]+) \*/", table)
    source = (generated / "cpuemu_40.cpp").read_text()
    functions, selected, excluded = {}, {}, {}
    for name, opcode, mnemonic in rows:
        body = extract(source, name)
        code = re.sub(r"/\*.*?\*/|//[^\n]*", "", body, flags=re.S)
        calls = set(re.findall(r"\b([A-Za-z_]\w*)\s*\(", code)) - {name, "REGPARAM2"}
        missing = sorted(calls - CALLS)
        if missing:
            excluded[opcode] = {"mnemonic": mnemonic, "missing_helpers": missing}
        else:
            selected[int(opcode, 16)] = name
            functions[name] = body
    decoded = subprocess.check_output([str(generated / "decode")], text=True)
    body = ['// Mechanically extracted, unmodified pinned WinUAE/UAE generated bodies and helpers.',
            '// Copyright Bernd Schmidt and WinUAE/ARAnyM contributors; retain upstream licenses.',
            '#include "core.hpp"', '#include <cstdlib>', 'namespace modern040 {',
            *helper_text, *functions.values(),
            f'extern const unsigned supported_families = {len(selected)};',
            'struct Dispatch { uae_u16 opcode; Handler handler; };',
            'static const Dispatch dispatch[] = {']
    count = 0
    for line in decoded.splitlines():
        opcode, family = (int(x, 16) for x in line.split())
        if family in selected:
            body.append(f'{{0x{opcode:04x}, {selected[family]}}},')
            count += 1
        else:
            body.append(f'{{0x{opcode:04x}, nullptr}},')
    body += ['};', 'void install_handlers() {',
             'handlers.fill(nullptr); legal.fill(false);',
             'for (const auto &entry : dispatch) { legal[entry.opcode] = true; handlers[entry.opcode] = entry.handler; }',
             '}', f'extern const unsigned supported_opcodes = {count};', '}']
    (build / "integer.cpp").write_text("\n".join(body) + "\n")
    provenance["runner"] = {"supported_families": len(selected), "supported_opcodes": count,
                            "excluded_families": excluded, "helper_sha256": helper_hashes,
                            "source_sha256": {p.name: hashlib.sha256(p.read_bytes()).hexdigest()
                                              for p in HERE.iterdir() if p.is_file()},
                            "profile": "68040 integer, no MMU/JIT/FPU, flat RAM, terminal exceptions"}
    (build / "runner-provenance.json").write_text(json.dumps(provenance, indent=2) + "\n")
    flags = ["-std=c++20", "-O1" if args.sanitize else "-O2", "-g", "-fwrapv",
             "-Wall", "-Wextra", "-Wno-unused-parameter", "-Wno-unused-variable", "-Wno-unused-but-set-variable",
             "-I" + str(HERE), "-I" + str(upstream / "include")]
    if args.sanitize: flags += ["-fsanitize=address,undefined", "-fno-sanitize-recover=all", "-fno-omit-frame-pointer"]
    cxx = os.environ.get("CXX", "c++")
    objects = []
    for source in (build / "integer.cpp", HERE / "core.cpp"):
        obj = build / (source.stem + ".o")
        command([cxx, *flags, "-c", source, "-o", obj])
        objects.append(obj)
    command(["ar", "rcs", build / "libmodern040-integer.a", *objects])
    for target, driver in [("runner-tests", "test.cpp"), ("integer-runner", "main.cpp")]:
        command([cxx, *flags, HERE / driver, build / "libmodern040-integer.a", "-o", build / target])
    print(f"Runner: {len(selected)} generated families, {count} opcodes; {len(excluded)} families explicitly excluded", flush=True)
    command([build / "runner-tests"])
    command([build / "integer-runner", "--demo", "--budget", "100"])


if __name__ == "__main__":
    try: main()
    except (RuntimeError, subprocess.CalledProcessError) as error: sys.exit(str(error))
