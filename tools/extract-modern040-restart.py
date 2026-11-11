#!/usr/bin/env python3
"""Extract pinned restart-aware 040 bodies for the physical-bus adapter.

The instruction bodies are copied verbatim from cpuemu_31.cpp. This does not
enable paged translation: the adapter below the bodies supplies a physical bus.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import argparse, hashlib, importlib.util, json, re, subprocess
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
PIN = 'd82bc2549f27943d11dc59482d5388daf1c0f337'
def sha(p): return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--upstream',type=Path,required=True)
    args=parser.parse_args(); upstream=args.upstream.resolve()
    spec=importlib.util.spec_from_file_location('runner_build',ROOT/'tests/cpu-core-upgrade/runner/build.py')
    build=importlib.util.module_from_spec(spec);spec.loader.exec_module(build)
    inputs={}
    for name in ['cpuemu_31.cpp','cpummu.cpp','newcpu_common.cpp','od-unix/machdep/m68k.h']:
        p=upstream/name
        assert p.read_bytes()==subprocess.check_output(['git','-C',str(upstream),'show',PIN+':'+name]),name
        inputs[name]=sha(p)
    source=(upstream/'cpuemu_31.cpp').read_text()
    matches=list(re.finditer(r'^uae_u32 REGPARAM2 (op_([0-9a-f]+)_31_ff)\(',source,re.M))
    additional=set('''return m68k_incpci m68k_getpci m68k_setpci get_iword_mmu040
        get_ibyte_mmu040 get_ilong_mmu040 get_byte_mmu040 get_word_mmu040 get_long_mmu040
        put_byte_mmu040 put_word_mmu040 put_long_mmu040 x_get_disp_ea_020
        x_get_bitfield x_put_bitfield Exception_cpu cpu_restore_fixup
        m68k_do_rte_mmu040 m68k_do_rts_mmu040 m68k_do_bsr_mmu040
        sfc040_get_byte sfc040_get_word sfc040_get_long
        dfc040_put_byte dfc040_put_word dfc040_put_long'''.split())
    functions={};bodies=[];excluded={};hashes={}
    for i,m in enumerate(matches):
        name,family=m.groups()
        body=build.extract(source[m.start():matches[i+1].start() if i+1<len(matches) else len(source)],name)
        code=re.sub(r'/\*.*?\*/|//[^\n]*','',body,flags=re.S)
        calls=set(re.findall(r'\b([A-Za-z_]\w*)\s*\(',code))-{name}
        missing=sorted(calls-build.CALLS-additional)
        if missing:excluded[family]=missing;continue
        functions[family]=name;bodies.append(body);hashes[name]=hashlib.sha256(body.encode()).hexdigest()
    common=(upstream/'newcpu_common.cpp').read_text()
    mmu=(upstream/'cpummu.cpp').read_text()
    helpers=[build.extract((upstream/'od-unix/machdep/m68k.h').read_text(),'cctrue'),build.extract(common,'cpu_restore_fixup')]
    # The RTE helper is retained verbatim; the physical adapter validates and
    # rejects unintegrated FPU/trace continuation before this helper is called.
    for name in ['m68k_do_rte_mmu040','m68k_do_rts_mmu040','m68k_do_bsr_mmu040']:
        helpers.append(build.extract(mmu,name))
    legacy=(ROOT/'src/modern040/integer.cpp').read_text()
    mapping=[]
    for opcode,family in re.findall(r'^\{0x([0-9a-f]+), op_([0-9a-f]+)_\d+_ff,',legacy,re.M):
        if family in functions:mapping.append((opcode,functions[family]))
    out=['// Verbatim pinned WinUAE instruction bodies; GPL-2.0-or-later.',
         '// Copyright Bernd Schmidt and WinUAE contributors.',
         '#include "restart.hpp"','namespace modern040 {',*helpers,*bodies,
         'struct RestartEntry { uae_u16 opcode;Handler handler; };',
         'static const RestartEntry restart_dispatch[] = {']
    out += ['{0x'+op+','+name+'},' for op,name in mapping]
    out += ['};','void install_restart_handlers() {',
            'restart_handlers.fill(nullptr);',
            'for(const auto &entry:restart_dispatch) restart_handlers[entry.opcode]=entry.handler;',
            '}','}']
    target=ROOT/'src/modern040/restart.cpp';target.write_text('\n'.join(out)+'\n')
    record={'upstream_pin':PIN,'inputs':inputs,'body_sha256':hashes,
            'extracted_families':len(functions),'mapped_opcodes':len(mapping),'excluded_families':excluded,
            'restart_sha256':sha(target),
            'scope':'physical bus; genuine 040 restart/update/writeback ordering; no paged MMU, locked RMW, MOVE16 or FPU continuation'}
    (ROOT/'src/modern040/RESTART-SOURCE.json').write_text(json.dumps(record,indent=2)+'\n')
    print('Restart bodies:',len(functions),'families,',len(mapping),'mapped opcodes')
if __name__=='__main__':main()
