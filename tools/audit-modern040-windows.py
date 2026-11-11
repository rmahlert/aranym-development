#!/usr/bin/env python3
"""Report the actual RC2 Windows modernization boundary without building.

This inventory is not execution evidence and does not mark features qualified.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import argparse
import collections
import hashlib
import json
import pathlib
import re
import subprocess

ROOT = pathlib.Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=pathlib.Path)
    args = parser.parse_args()
    source_names = [
        'configure.ac', 'src/modern040/README.txt', 'src/modern040/SOURCE.json',
        'src/modern040/RESTART-SOURCE.json', 'src/uae_cpu/registers.h',
        'src/uae_cpu/compiler/compemu_support.cpp',
        'tests/cpu-core-upgrade/runner/core.hpp',
        'tests/cpu-core-upgrade/runner/core.cpp',
        'tests/cpu-core-upgrade/runner/c-api.h',
        'tests/cpu-core-upgrade/runner/restart.hpp',
        'tests/modern-jit/modern-fallback.cpp',
        'tests/modern-jit/import-provenance.json',
        'tests/modern-jit/upstream-compemu.cpp',
    ]
    sources = {name: hashlib.sha256((ROOT/name).read_bytes()).hexdigest()
               for name in source_names}
    restart = json.loads((ROOT/'src/modern040/RESTART-SOURCE.json').read_text())
    groups = collections.defaultdict(list)
    for family, missing in restart['excluded_families'].items():
        groups[', '.join(missing)].append(family)
    upstream = json.loads((ROOT/'tests/cpu-core-upgrade/upstream-delta-2026-11-11.json').read_text())
    compiler = (ROOT/'tests/modern-jit/upstream-compemu.cpp').read_text()
    native_inventory = collections.defaultdict(lambda: collections.Counter())
    for _, mode, mnemonic in re.findall(
            r'void REGPARAM2 (op_\w+_comp_(ff|nf))\(uae_u32 opcode\) /\* (\w+) \*/', compiler):
        native_inventory[mnemonic][mode] += 1
    support = (ROOT/'src/uae_cpu/compiler/compemu_support.cpp').read_text()
    admission = support.split('/* First native integration profile:', 1)[1]
    admission = admission.split('switch(in.mnemo)', 1)[1].split('default:break;', 1)[0]
    admitted = set(re.findall(r'case i_(\w+)', admission))
    gaps = [
        {'issue': 'shin-95jh.3.12', 'target': 'Windows x64', 'component': 'remaining native integer/control admission',
         'current': 'a whitelist still excludes imported scalar MUL/EXG and effective-address/stack/control families',
         'missing': ['qualify additional actual upstream compiler bodies including conditional/no-flags variants',
                     'distinguish deliberately unsupported upstream translations from locally disabled bodies'],
         'gate': 'independent register/CCR/width/stack/control oracles with per-case native witness; no invented replacement compiler'},
        {'issue': 'shin-95jh.3.5', 'target': 'Windows x64', 'component': 'memory JIT',
         'current': 'guarded MOVE/MOVEA only; simple indirect/update/d16 operands, TT-RAM code',
         'missing': ['absolute, PC-relative, brief/full indexed and indirect addressing',
                     'general integer memory/RMW, implicit stack/control and MOVEM native execution'],
         'gate': 'independent all-register/CCR/RAM oracle plus actual per-case native witness; exact device transfer/fault deoptimization, self-modification and service/IRQ checks'},
        {'issue': 'shin-95jh.3.3', 'target': 'Windows x64', 'component': 'fault continuation',
         'current': 'precise physical-bus ordinary integer format7/RTE and MOVEM continuation',
         'missing': ['locked RMW and MOVE16 continuation', 'trace/FPU continuation',
                     'explicit policy for partial mapped-device transfers'],
         'gate': 'genuine guest handler/RTE with manual-derived frame/writeback/update ordering; never infer unknown transfer atomicity or replay completed device effects'},
        {'issue': 'shin-95jh.3.6', 'target': 'Windows x64', 'component': 'modern MMU',
         'current': 'MMU names in restart.hpp alias physical accessors; enabling translation stops; modern configure rejects MMU',
         'missing': ['real 040 page-table/ATC/transparent-permission engine and PTEST/PFLUSH',
                     'logical-to-physical fault metadata and descriptor update faults',
                     'modern MMU interpreter build/OS boot, separate MMU-JIT policy'],
         'gate': '4K/8K pages, user/supervisor and alternate FC, descriptors/protection/TTR/ATC flush/restart; MMU-on full-system boot'},
        {'issue': 'shin-95jh.3.7', 'target': 'Windows x64', 'component': 'modern FPU and FPU JIT',
         'current': 'original IEEE FPU external service, no FP register file in C ABI; modern configure rejects JIT-FPU',
         'missing': ['canonical extended-precision register/control/status state and modern arithmetic engine',
                     'FP exception/FSAVE/FRESTORE/FPSP pending state and operand faults',
                     'FPU JIT state/precision/helper ABI and fallback compatibility'],
         'gate': 'Motorola bit-pattern/rounding/special-value and frame oracles; interpreter, integer-JIT/FPU-fallback and FPU-JIT comparisons; font/desktop tests'},
        {'issue': 'shin-95jh.3.11', 'target': 'Windows x64', 'component': 'nested Atari services',
         'current': 'modern execute and JIT fallback stop on depth>0; legacy FPU enabled exceptions/trace/operand faults stop',
         'missing': ['restart-aware nested guest invocation and full state restoration across host services'],
         'gate': 'nested m68k_call/NatFeat/fVDI fixtures with exact callback counts, PC, D/A, SR, USP/ISP/MSP, exceptions and IRQ/trace checks'},
        {'issue': 'shin-95jh.3.8', 'target': 'ARM/macOS separate host targets', 'component': 'host backend',
         'current': 'x64 imported backend only',
         'missing': ['ARM32/AArch64 emitter/ABI and native acceptance', 'Apple allocation/protection/signing and fault context'],
         'gate': 'separate native Linux ARM and macOS gates; not an excuse to exclude unfinished Windows work'},
        {'issue': 'shin-95jh.3.9 / shin-95jh.2', 'target': 'Windows x64', 'component': 'owner acceptance and installer',
         'current': 'RC2 owned full-system/Windows-under-Wine/preservation-installer checks passed; laptop same-image acceptance pending',
         'missing': ['same-input laptop desktop/HighWire acceptance after the reported faults',
                     'new exact-binary interpreter/JIT/installer gates after remaining integrations'],
         'gate': 'preserve config/NVRAM/ROM/drives/fVDI; no superseding RC until bounded gates pass'},
    ]
    report = {
        'kind': 'static source audit; not executable qualification',
        'baseline_rc2': 'e866507747383d9d768d3a398ee8668607dc3226',
        'source_commit_at_audit': subprocess.check_output(
            ['git', '-C', str(ROOT), 'rev-parse', 'HEAD'], text=True).strip(),
        'source_sha256': sources,
        'restart_extracted_families': restart['extracted_families'],
        'restart_mapped_opcodes': restart['mapped_opcodes'],
        'restart_excluded_families_by_helper': dict(groups),
        'verified_upstream_head': upstream['head'],
        'upstream_comparison_url': upstream['url'],
        'upstream_commits_since_pin': upstream['ahead'],
        'upstream_changed_x86_jit_files': [f['filename'] for f in upstream['coreFiles']
                                          if f['filename'].startswith('jit/x86/')],
        'imported_compiler_function_headers_by_mnemonic': dict(native_inventory),
        'native_admission_whitelist_mnemonics': sorted(admitted),
        'imported_mnemonics_excluded_by_whitelist': sorted(set(native_inventory)-admitted),
        'compiler_inventory_limit': 'source function headers do not establish successful emission, table coverage or native execution; conditional FAIL and no-flags behavior still require executable gates',
        'gaps': gaps,
        'completion': 'open; Windows gaps above must not be renamed out-of-target ARM work',
    }
    text = json.dumps(report, indent=2) + '\n'
    if args.output:
        args.output.write_text(text)
    else:
        print(text, end='')


if __name__ == '__main__':
    main()
