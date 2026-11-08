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
    branch_stack_pop_rte fpuop_bcc fpuop_save fpuop_restore get_bitfield
    put_bitfield divbyzero_special setdivuflags setdivsflags setchkundefinedflags
    setchk2undefinedflags m68k_mull m68k_divl op_unimpl
    sfc_nommu_get_byte sfc_nommu_get_word sfc_nommu_get_long
    dfc_nommu_put_byte dfc_nommu_put_word dfc_nommu_put_long cpureset
    flush_cpu_caches_040 flush_icache""".split())
HELPERS = {
    "newcpu.cpp": ["activate_trace", "MakeSR", "MakeFromSR_x", "MakeFromSR_T0",
                   "MakeFromSR", "MakeFromSR_STOP", "check_t0_trace", "internalexception", "exception_check_trace",
                   "Exception_cpu_oldpc", "Exception", "ExceptionL", "op_unimpl", "exception3f", "exception3_notinstruction",
                   "exception3_read_prefetch_only", "exception3_read_prefetch", "exception3_read_prefetch_68040bug",
                   "sfc_nommu_get_byte", "sfc_nommu_get_word", "sfc_nommu_get_long",
                   "dfc_nommu_put_byte", "dfc_nommu_put_word", "dfc_nommu_put_long", "flush_cpu_caches_040"],
    "newcpu_common.cpp": ["movec_illg", "m68k_move2c", "m68k_movec2", "get_disp_ea_020", "exception_pc",
                          "Exception_build_stack_frame_common", "get_bitfield", "put_bitfield",
                          "divbyzero_special", "setdivuflags", "setdivsflags", "setchkundefinedflags",
                          "chk2_sub_wrap", "setchk2undefinedflags", "divsl_overflow", "divul_overflow",
                          "divsl_divbyzero", "divul_divbyzero", "m68k_divl", "m68k_mull"],
    "od-unix/machdep/m68k.h": ["cctrue"],
    "cpummu.cpp": ["mmu_tt_modified","mmu_do_match_ttr","mmu_match_ttr","mmu_flush_cache"],
}


def extract(source, name, occurrence=None):
    pattern = r"^.*\b" + re.escape(name) + r"\s*\([^;\n]*\)\n\{"
    matches = list(re.finditer(pattern, source, re.M))
    if occurrence is None and len(matches) != 1:
        raise RuntimeError(f"Expected one function {name}, found {len(matches)}")
    match = matches[0 if occurrence is None else occurrence]
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


def frame_builder_040(source):
    """Retain the exact upstream 040/LC040 frame cases; exclude other CPU models.

    Case statements and common writes are copied verbatim. The replacement
    default fails closed instead of linking irrelevant 68010/030 MMU state.
    This restriction is explicit in provenance, not advertised as an unchanged
    whole multi-model helper.
    """
    full = extract(source, "Exception_build_stack_frame")
    begin = full.index("\tswitch (format) {\n") + len("\tswitch (format) {\n")
    footer = full.rindex("\n\t}\n\tm68k_areg(regs, 7) -= 2;")
    labels = list(re.finditer(r"^\t(?:case 0x([0-9a-fA-F]+):|default:)", full[begin:footer], re.M))
    selected, hashes = [], {}
    for i, label in enumerate(labels):
        if label.group(1) and int(label.group(1),16) in (0,1,2,3,4,7):
            last = labels[i+1].start() if i+1<len(labels) else footer-begin
            case = full[begin+label.start():begin+last]
            selected.append(case); hashes[label.group(1)] = hashlib.sha256(case.encode()).hexdigest()
    return full[:begin]+''.join(selected)+"\tdefault: unsupported_frame(format);"+full[footer:], hashes


def absent_fpu_helpers(source):
    """Real source no-FPU chain + exact EA/decode prefixes for FSAVE/FRESTORE.

    Tail slicing is explicit: only code through fault_if_no_fpu is retained;
    reaching an active-FPU tail fails closed. No FPU arithmetic is synthesized.
    """
    prefix = ['namespace absent_fpu {',
              'static bool support_exceptions=false, fpu_mmu_fixup=false;',
              'static int warned=0;',
              'struct Fixup { int reg=-1; uae_u32 value=0; }; static Fixup mmufixup[1];',
              'static int fpp_cond(uae_u32) { unsupported_fpu_body(); }']
    # These are exactly the upstream non-cycle-exact empty timing functions.
    prefix += [extract(source,'fpu_timing_begin',-1),extract(source,'fpu_timing_cond',-1)]
    for name in ('jit_fpu','fp_exception_pending','fp_unimp_instruction_exception_pending',
                 'fpu_op_illg','if_no_fpu','fault_if_no_fpu','fault_if_no_6888x',
                 'get_fpu_version','get_fp_ad','maybe_idle_state','fpuop_bcc'):
        prefix.append(extract(source,name))
    hashes={}
    for name in ('fpuop_save','fpuop_restore'):
        function=extract(source,name)
        marker=re.search(r'\tif \(fault_if_no_fpu \(opcode, 0, ad, adset, pc\)\)\n\t\treturn;\n',function)
        if not marker: raise RuntimeError('No exact no-FPU guard boundary in '+name)
        kept=function[:marker.end()]
        hashes[name]=hashlib.sha256(kept.encode()).hexdigest()
        prefix.append(kept+'\tunsupported_fpu_body();\n}')
    prefix += ['}',
               'static void require_absent_profile() { if(!host.lc040_probes||regs.fp_exp_pend||regs.fp_unimp_pend||regs.fp_branch) unsupported_fpu_body(); }',
               'void fpuop_bcc(uae_u32 op,uaecptr pc,uae_u32 extra) { require_absent_profile(); absent_fpu::fpuop_bcc(op,pc,extra); }',
               'void fpuop_save(uae_u32 op) { require_absent_profile(); absent_fpu::fpuop_save(op); }',
               'void fpuop_restore(uae_u32 op) { require_absent_profile(); absent_fpu::fpuop_restore(op); }']
    return prefix,hashes


def private_aranym_handlers(generated):
    """Keep original generated extension bodies and their resolved decoder.

    These are ARAnyM platform opcodes absent in public WinUAE's decoder, not
    replacements for modern architecture instruction bodies.
    """
    target=generated/'legacy'
    provenance=json.loads((generated/'legacy-provenance.json').read_text())
    pin='c762046250551840d9c0f32c607713f71a05afa3'
    if provenance['commit']!=pin:raise RuntimeError('Unexpected ARAnyM extension source pin')
    for name,expected in provenance['source_sha256'].items():
        data=(target/'source'/name).read_bytes()
        if hashlib.sha256(data).hexdigest()!=expected:raise RuntimeError('Changed extension generator input '+name)
        if data!=subprocess.check_output(['git','-C',str(HARNESS.parents[1]),'show',f'{pin}:src/uae_cpu/{name}']):
            raise RuntimeError('Unpinned extension generator input '+name)
    source=(target/'cpuemu.cpp').read_text()
    if hashlib.sha256(source.encode()).hexdigest()!=provenance['generated_cpuemu_sha256']:
        raise RuntimeError('Changed generated ARAnyM extension bodies')
    table=(target/'cpufunctbl.cpp').read_text().split('#else\n',1)[0]
    rows=re.findall(r'^\s*(?:CPUFUNC(?:_FF)?\((op_[0-9a-f]+_[0-9]+)\)|(op_illg_1)),? /\*',table,re.M)
    if len(rows)!=65536:raise RuntimeError('Bad private resolved decoder')
    names=('op_7100_0','op_7101_0','op_7300_0','op_7301_0')
    text=['namespace private_aranym {','#define CPUFUNC(name) name','#define cpuop_begin() do {} while(0)',
          '#define cpuop_end() do {} while(0)',
          'static void Exception(int nr,uae_u32 oldpc) { if(oldpc) unsupported_frame(-1); modern040::Exception(nr); }']
    hashes={}
    for name in names:
        match=re.search(r'^void REGPARAM2 CPUFUNC\('+name+r'\)\(uae_u32 opcode\) /\* [^\n]* \*/\n\{',source,re.M)
        if not match:raise RuntimeError('Missing actual extension '+name)
        end=match.end();depth=1
        while depth:
            if source[end]=='{':depth+=1
            elif source[end]=='}':depth-=1
            end+=1
        body=source[match.start():end];text.append(body);hashes[name]=hashlib.sha256(body.encode()).hexdigest()
    text+=['#undef cpuop_begin','#undef cpuop_end','#undef CPUFUNC','}',
           'void install_private_handlers() { private_handlers.fill(nullptr);']
    mappings=[]
    for opcode,(name,_) in enumerate(rows):
        if name in names:
            text.append(f'private_handlers[0x{opcode:04x}]=private_aranym::{name};');mappings.append(opcode)
    expected=list(range(0x7100,0x7200))+[0x7300,0x7301]
    if mappings!=expected:raise RuntimeError('ARAnyM private mapping changed; audit before integration')
    text.append('}')
    return text,{'pin':pin,'body_sha256':hashes,'resolved_opcodes':len(mappings),
                 'restriction':'unchanged generated private bodies; external service suspension/completion seam, no platform backend supplied'}


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
    for path,prefixes in [('include/cpummu.h',('MMU_TTR_','TTR_')),('include/memory.h',('CACHE_',))]:
        data=(upstream/path).read_bytes()
        if data!=subprocess.check_output(['git','-C',str(upstream),'show',f'{PIN}:{path}']):raise RuntimeError('Changed TTR constant input '+path)
        helper_hashes[path]=hashlib.sha256(data).hexdigest()
        helper_text += [line for line in data.decode().splitlines() if re.match(r'^#define\s+(?:'+ '|'.join(prefixes)+')',line)]
    for path, names in HELPERS.items():
        data = (upstream / path).read_bytes()
        tracked = subprocess.check_output(["git", "-C", str(upstream), "show", f"{PIN}:{path}"])
        if data != tracked: raise RuntimeError("Modified upstream helper input " + path)
        helper_hashes[path] = hashlib.sha256(data).hexdigest()
        helper_text += [extract(data.decode(), name) for name in names]
    frame, frame_hashes = frame_builder_040((upstream / "newcpu_common.cpp").read_text())
    helper_text += [frame, 'void complete_exception_trace(int nr) { exception_check_trace(nr); }']
    fpu_source=(upstream/'fpp.cpp').read_bytes()
    if fpu_source!=subprocess.check_output(['git','-C',str(upstream),'show',f'{PIN}:fpp.cpp']):
        raise RuntimeError('Modified upstream no-FPU helper input')
    fpu_helpers,fpu_prefix_hashes=absent_fpu_helpers(fpu_source.decode())
    helper_text += fpu_helpers
    private_text,private_provenance=private_aranym_handlers(generated)
    helper_hashes['fpp.cpp']=hashlib.sha256(fpu_source).hexdigest()
    tables = (generated / "cpustbl.cpp").read_text()
    table = tables.split("const struct cputbl op_smalltbl_41[] = {\n", 1)[1].split("\n{ 0 }};", 1)[0]
    rows = re.findall(r"\{ (op_[0-9a-f]+_[0-9]+_ff), NULL, 0x([0-9a-f]+),[^\n]*/\* ([^*]+) \*/", table)
    source = (generated / "cpuemu_40.cpp").read_text()
    functions, selected, excluded, probe_families = {}, {}, {}, set()
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
            if calls & {'fpuop_bcc','fpuop_save','fpuop_restore'}: probe_families.add(int(opcode,16))
    decoded = subprocess.check_output([str(generated / "decode")], text=True)
    body = ['// Mechanically extracted, unmodified pinned WinUAE/UAE generated bodies and helpers.',
            '// Copyright Bernd Schmidt and WinUAE/ARAnyM contributors; retain upstream licenses.',
            '#include "core.hpp"', '#include <cstdlib>', '#define MMU_ICACHE 0', 'namespace modern040 {',
            *helper_text, *functions.values(), *private_text,
            f'extern const unsigned supported_families = {len(selected)};',
            'struct Dispatch { uae_u16 opcode; Handler handler; bool lc040_probe; };',
            'static const Dispatch dispatch[] = {']
    count = 0
    for line in decoded.splitlines():
        opcode, family = (int(x, 16) for x in line.split())
        if family in selected:
            body.append(f'{{0x{opcode:04x}, {selected[family]}, {str(family in probe_families).lower()}}},')
            count += 1
        else:
            body.append(f'{{0x{opcode:04x}, nullptr, false}},')
    body += ['};', 'void install_handlers() {',
             'handlers.fill(nullptr); legal.fill(false); requires_lc040_probes.fill(false);',
             'for (const auto &entry : dispatch) { legal[entry.opcode] = true; handlers[entry.opcode] = entry.handler; requires_lc040_probes[entry.opcode]=entry.lc040_probe; }',
             'install_private_handlers();',
             '}', f'extern const unsigned supported_opcodes = {count};', '}']
    (build / "integer.cpp").write_text("\n".join(body) + "\n")
    provenance["runner"] = {"supported_families": len(selected), "supported_opcodes": count,
                            "excluded_families": excluded, "helper_sha256": helper_hashes,
                            "source_sha256": {p.name: hashlib.sha256(p.read_bytes()).hexdigest()
                                              for p in HERE.iterdir() if p.is_file()},
                            "frame_builder": {"restriction": "exact upstream cases0/1/2/3/4/7 and common writes; other model cases fail closed",
                                              "retained_case_sha256": frame_hashes},
                            "lc040_probes": {"families":len(probe_families),"restriction":"real no-FPU chain; FSAVE/FRESTORE prefixes stop at guard; active arithmetic excluded",
                                             "retained_prefix_sha256":fpu_prefix_hashes},
                            "private_aranym":private_provenance,
                            "profile": "68040 integer, no MMU/JIT/arithmeticFPU, flat RAM or explicit bus callback, gated LC040 probes and real frame delivery"}
    flags = ["-std=c++20", "-O1" if args.sanitize else "-O2", "-g", "-fwrapv", "-DFPUEMU",
             "-Wall", "-Wextra", "-Wno-unused-parameter", "-Wno-unused-variable", "-Wno-unused-but-set-variable",
             "-I" + str(HERE), "-I" + str(upstream / "include")]
    if args.sanitize: flags += ["-fsanitize=address,undefined", "-fno-sanitize-recover=all", "-fno-omit-frame-pointer"]
    cxx = os.environ.get("CXX", "c++")
    provenance['runner']['build']={'sanitized':args.sanitize,'compiler':subprocess.check_output([cxx,'--version'],text=True).splitlines()[0],'flags':flags}
    (build / "runner-provenance.json").write_text(json.dumps(provenance, indent=2) + "\n")
    objects = []
    for source in (build / "integer.cpp", HERE / "core.cpp", HERE / "c-api.cpp"):
        obj = build / (source.stem + ".o")
        command([cxx, *flags, "-c", source, "-o", obj])
        objects.append(obj)
    command(["ar", "rcs", build / "libmodern040-integer.a", *objects])
    cflags=["-std=c11","-O1" if args.sanitize else "-O2","-g","-Wall","-Wextra","-I"+str(HERE)]
    if args.sanitize:cflags += ["-fsanitize=address,undefined","-fno-sanitize-recover=all","-fno-omit-frame-pointer"]
    command([os.environ.get("CC","cc"),*cflags,"-c",HERE/"c-api-test.c","-o",build/"c-api-test.o"])
    command([cxx,*flags,build/"c-api-test.o",build/"libmodern040-integer.a","-o",build/"c-api-tests"])
    for target, driver in [("runner-tests", "test.cpp"), ("frame-tests", "frame-test.cpp"),
                           ("lc040-probe-tests", "lc040-test.cpp"), ("integer-helper-tests", "integer-helper-test.cpp"),
                           ("bus-tests", "bus-test.cpp"),
                           ("service-tests", "service-test.cpp"),
                           ("platform-contract-tests", "platform-contract-test.cpp"),
                           ("ttr-tests", "ttr-test.cpp"),
                           ("cache-tests", "cache-test.cpp"),
                           ("read-probe-tests", "read-probe-test.cpp"),
                           ("integer-runner", "main.cpp")]:
        command([cxx, *flags, HERE / driver, build / "libmodern040-integer.a", "-o", build / target])
    print(f"Runner: {len(selected)} generated families, {count} opcodes; {len(excluded)} families explicitly excluded", flush=True)
    command([build / "runner-tests"])
    command([build / "frame-tests"])
    command([build / "lc040-probe-tests"])
    command([build / "integer-helper-tests"])
    result=subprocess.run([str(build / "integer-helper-tests"),"--negative-bitfield"])
    if result.returncode!=1: raise RuntimeError(f"Integer checker returned {result.returncode}, expected1")
    print("REJECTED deliberate --negative-bitfield by actual integer checker",flush=True)
    command([build / "bus-tests"])
    command([build / "c-api-tests"])
    result=subprocess.run([str(build / "c-api-tests"),"--negative-state"])
    if result.returncode!=1: raise RuntimeError(f"C ABI checker returned {result.returncode}, expected1")
    print("REJECTED deliberate --negative-state by actual C ABI checker",flush=True)
    command([build / "service-tests"])
    result=subprocess.run([str(build / "service-tests"),"--negative-pc"])
    if result.returncode!=1: raise RuntimeError(f"Private service checker returned {result.returncode}, expected1")
    print("REJECTED deliberate --negative-pc by actual private service checker",flush=True)
    command([build / "platform-contract-tests"])
    command([build / "ttr-tests"])
    result=subprocess.run([str(build / "ttr-tests"),"--negative-match"])
    if result.returncode!=1: raise RuntimeError(f"TTR checker returned {result.returncode}, expected1")
    print("REJECTED deliberate --negative-match by actual TTR checker",flush=True)
    command([build / "cache-tests"])
    result=subprocess.run([str(build / "cache-tests"),"--negative-selector"])
    if result.returncode!=1: raise RuntimeError(f"Cache checker returned {result.returncode}, expected1")
    print("REJECTED deliberate --negative-selector by actual cache checker",flush=True)
    command([build / "read-probe-tests"])
    result=subprocess.run([str(build / "read-probe-tests"),"--negative-pc"])
    if result.returncode!=1: raise RuntimeError(f"Read-probe checker returned {result.returncode}, expected1")
    print("REJECTED deliberate read-probe --negative-pc by actual frame checker",flush=True)
    result=subprocess.run([str(build / "bus-tests"),"--negative-fc"])
    if result.returncode!=1: raise RuntimeError(f"Bus checker returned {result.returncode}, expected1")
    print("REJECTED deliberate --negative-fc by actual bus checker",flush=True)
    for mode in ("--negative-format", "--negative-read"):
        result=subprocess.run([str(build / "lc040-probe-tests"),mode])
        if result.returncode!=1: raise RuntimeError(f"LC040 checker {mode} returned {result.returncode}, expected1")
    for mode in ("--negative-m-clear", "--negative-frame-pc"):
        result=subprocess.run([str(build / "frame-tests"),mode])
        if result.returncode!=1: raise RuntimeError(f"Frame checker {mode} returned {result.returncode}, expected1")
    command([build / "integer-runner", "--demo", "--budget", "100"])


if __name__ == "__main__":
    try: main()
    except (RuntimeError, subprocess.CalledProcessError) as error: sys.exit(str(error))
