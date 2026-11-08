"""Build the actual pinned ARAnyM generator and extract the shared subset.

SPDX-License-Identifier: GPL-2.0-or-later
Instruction bodies remain unchanged. Only their void ABI is wrapped to match the
test dispatch signature; helper/memory bindings live explicitly in legacy.hpp.
"""
import hashlib
from pathlib import Path
import re
import subprocess

PIN = "c762046250551840d9c0f32c607713f71a05afa3"
HERE = Path(__file__).resolve().parent


def generate(cxx, upstream, build, modern_dispatch):
    repo = HERE.parents[1]
    target = build / "legacy"
    source = target / "source"
    source.mkdir(parents=True, exist_ok=True)
    inputs = ("build68k.c", "readcpu.cpp", "gencpu.c", "table68k", "readcpu.h",
              "newcpu.h", "registers.h")
    hashes = {}
    for name in inputs:
        data = subprocess.check_output(["git", "-C", str(repo), "show", f"{PIN}:src/uae_cpu/{name}"])
        (source / name).write_bytes(data)
        hashes[name] = hashlib.sha256(data).hexdigest()
    flags = ["-std=c++17", "-O2", "-Wno-write-strings", "-Ddo_byteswap_16=__builtin_bswap16",
             "-I" + str(HERE / "host"), "-I" + str(source), "-I" + str(upstream / "include")]

    def run(args, **kwargs):
        subprocess.run([str(arg) for arg in args], check=True, **kwargs)

    run([cxx, *flags, "-include", HERE / "host/sysdeps.h", source / "build68k.c",
         "-o", target / "build68k"])
    with (source / "table68k").open("rb") as table, (target / "cpudefs.cpp").open("wb") as out:
        run([target / "build68k"], stdin=table, stdout=out)
    run([cxx, *flags, source / "readcpu.cpp", source / "gencpu.c", target / "cpudefs.cpp",
         "-o", target / "gencpu"])
    run([target / "gencpu"], cwd=target)
    # The legacy generator already emits its complete resolved opcode table.
    # Use the ordinary (not byte-swapped) half, exactly as the declared host bus
    # presents fetched opcodes to both backends.
    full_table = (target / "cpufunctbl.cpp").read_text().split("#else\n", 1)[0]
    rows = re.findall(r"^\s*(?:CPUFUNC(?:_FF)?\((op_[0-9a-f]+_[0-9]+)\)|(op_illg_1)),? /\*", full_table, re.M)
    if len(rows) != 65536:
        raise RuntimeError(f"Legacy generated dispatch table has {len(rows)} rows, expected 65536")
    names = [handler or illegal for handler, illegal in rows]
    selected = {names[opcode] for opcode, _ in modern_dispatch}
    if "op_illg_1" in selected:
        raise RuntimeError("Modern subset contains an opcode illegal in the pinned legacy decoder")
    expected_opcodes = {opcode for opcode, _ in modern_dispatch}
    legacy_opcodes = {opcode for opcode, name in enumerate(names) if name in selected}
    if expected_opcodes != legacy_opcodes:
        raise RuntimeError("Old and modern decoded family coverage differs; audit before comparing")
    generated = (target / "cpuemu.cpp").read_text()
    functions = []
    for name in sorted(selected):
        match = re.search(r"^void REGPARAM2 CPUFUNC\(" + re.escape(name) +
                          r"\)\(uae_u32 opcode\) /\* [^\n]* \*/\n\{", generated, re.M)
        if not match:
            raise RuntimeError("Missing legacy generated function " + name)
        depth = 1
        end = match.end()
        while depth:
            if end == len(generated):
                raise RuntimeError("Unbalanced generated legacy function " + name)
            if generated[end] == "{": depth += 1
            elif generated[end] == "}": depth -= 1
            end += 1
        functions.append(generated[match.start():end])
    body = ["// Generated unchanged from pinned ARAnyM; see legacy-provenance.json.",
            "// Copyright 2001-2009 ARAnyM developers; UAE generator Bernd Schmidt.",
            "// Distributed under GPL-2.0-or-later; original source notices retained externally.",
            '#include "legacy.hpp"', *functions]
    for name in sorted(selected):
        body.append(f"uae_u32 wrap_{name}(uae_u32 opcode) {{ CPUFUNC({name})(opcode); return 0; }}")
    body.append("void install_legacy_handlers() {")
    body += [f"    legacy_handlers[0x{opcode:04x}] = wrap_{names[opcode]};" for opcode in sorted(expected_opcodes)]
    body += [f"    legacy_native_handlers[0x{opcode:04x}] = CPUFUNC({names[opcode]});" for opcode in sorted(expected_opcodes)]
    body += ["}", ""]
    (build / "legacy-handlers.cpp").write_text("\n\n".join(body))
    return {"origin": "https://github.com/aranym/aranym.git (private baseline snapshot)",
            "commit": PIN,
            "commit_date": subprocess.check_output(["git", "-C", str(repo), "show", "-s", "--format=%cI", PIN], text=True).strip(),
            "source_sha256": hashes,
            "generated_cpuemu_sha256": hashlib.sha256((target / "cpuemu.cpp").read_bytes()).hexdigest(),
            "dispatch_table": "generated cpufunctbl, normal endian, level 4 (68040)",
            "mapped_opcodes": len(expected_opcodes), "families": sorted(selected),
            "abi_adapter": "correctness: void wrapped to uae_u32; benchmark: original native void ABI, no wrapper",
            "helpers": "legacy.hpp; explicit PC/fault cursor/JSR/RTS bindings"}
