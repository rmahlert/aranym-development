#!/usr/bin/env python3
"""Build the pinned experimental core library from corresponding source.
SPDX-License-Identifier: GPL-2.0-or-later
"""
from pathlib import Path
import argparse,hashlib,json,os,shlex,subprocess
ROOT=Path(__file__).resolve().parent.parent
PIN='d82bc2549f27943d11dc59482d5388daf1c0f337'
def sha(p):
    with p.open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--output',required=True,type=Path)
    p.add_argument('--cxx',default=os.environ.get('CXX','c++'))
    p.add_argument('--ar',default=os.environ.get('AR','ar'))
    p.add_argument('--upstream',required=True,type=Path,help='exact pinned WinUAE source tree supplying SoftFloat')
    a=p.parse_args();out=a.output.resolve()
    if out==ROOT or ROOT in out.parents:raise RuntimeError('choose an out-of-tree build directory')
    marker=out/'owned-modern040-library.json'
    if out.exists() and any(out.iterdir()) and not marker.is_file():raise RuntimeError('unmarked existing output refused')
    source=ROOT/'src/modern040';runner=source/'runtime'
    upstream=a.upstream.resolve(strict=True)
    if subprocess.check_output(['git','-C',str(upstream),'rev-parse','HEAD'],text=True).strip()!=PIN:raise RuntimeError('WinUAE source is not the required pin')
    pins=json.loads((source/'SOURCE.json').read_text())
    assert sha(source/'integer.cpp')==pins['integer_sha256']
    restart=json.loads((source/'RESTART-SOURCE.json').read_text())
    assert sha(source/'restart.cpp')==restart['restart_sha256']
    assert sha(source/'include/uae/types.h')==pins['types_sha256']
    for n,h in pins['local_inputs'].items():assert sha(ROOT/n)==h,n
    out.mkdir(parents=True,exist_ok=True)
    marker.write_text('{"purpose":"source-built experimental modern040 library"}\n')
    cxx_command=shlex.split(a.cxx)
    target=subprocess.check_output([*cxx_command,'-dumpmachine'],text=True).strip()
    mingw='mingw' in target
    platform_include=source/'softfloat-mingw' if mingw else upstream/'od-unix'
    platform_flags=['-I'+str(platform_include),'-I'+str(upstream/'od-unix')]
    if not mingw:platform_flags.insert(0,'-DUAE_TARGET_UNIX=1')
    flags=['-std=c++20','-O2','-g','-fwrapv','-DFPUEMU','-I'+str(runner),'-I'+str(source/'include'),'-I'+str(upstream/'include')]
    commands=[];objects=[]
    core_sources=[source/'integer.cpp',source/'restart.cpp',runner/'core.cpp',runner/'c-api.cpp',runner/'fpu-frontend.cpp',runner/'mmu-walk.cpp',runner/'softfloat-adapter.cpp']
    for s in core_sources:
        obj=out/('modern040-'+s.stem+'.o');source_flags=flags
        if s.name=='softfloat-adapter.cpp':source_flags=[*flags,'-DWITH_SOFTFLOAT',*platform_flags,'-I'+str(upstream)]
        cmd=[*cxx_command,*source_flags,'-c',str(s),'-o',str(obj)]
        commands.append(cmd);subprocess.run(cmd,check=True);objects.append(obj)
    softfloat_sources=[upstream/'fpp_softfloat.cpp',upstream/'softfloat/softfloat.cpp',upstream/'softfloat/softfloat_decimal.cpp',upstream/'softfloat/softfloat_fpsp.cpp']
    softfloat_flags=['-std=gnu++17','-O2','-g','-DFPUEMU','-DWITH_SOFTFLOAT',*platform_flags,'-Wno-c++11-narrowing','-Wno-narrowing','-Wno-unused-value','-I'+str(upstream/'od-unix/include'),'-I'+str(upstream),'-I'+str(upstream/'include')]
    for s in softfloat_sources:
        tracked=subprocess.check_output(['git','-C',str(upstream),'show',f'{PIN}:{s.relative_to(upstream)}'])
        if s.read_bytes()!=tracked:raise RuntimeError('modified pinned SoftFloat source '+str(s))
        obj=out/('winuae-'+s.stem+'.o');cmd=[*cxx_command,*softfloat_flags,'-c',str(s),'-o',str(obj)]
        commands.append(cmd);subprocess.run(cmd,check=True);objects.append(obj)
    lib=out/'libmodern040.a';cmd=[*shlex.split(a.ar),'rcs',str(lib),*map(str,objects)]
    commands.append(cmd);subprocess.run(cmd,check=True)
    local_sources=[source/'integer.cpp',source/'restart.cpp',source/'RESTART-SOURCE.json',source/'include/uae/types.h',runner/'core.cpp',runner/'core.hpp',runner/'restart.hpp',runner/'c-api.cpp',runner/'c-api.h',runner/'fpu-frontend.cpp',runner/'softfloat-adapter.cpp',runner/'softfloat-adapter.hpp',runner/'mmu-walk.cpp',runner/'mmu-walk.hpp']
    if mingw:local_sources += [source/'softfloat-mingw/sysconfig.h',source/'softfloat-mingw/target.h']
    record={'source_pin':pins['upstream_pin'],'target':target,'compiler':subprocess.check_output([*cxx_command,'--version'],text=True).splitlines()[0],'commands':commands,'library_sha256':sha(lib),'sources':{str(s.relative_to(ROOT)):sha(s) for s in local_sources},'upstream_softfloat':{str(s.relative_to(upstream)):sha(s) for s in softfloat_sources},'objects':{o.name:sha(o) for o in objects},'scope':pins['scope']}
    (out/'provenance.json').write_text(json.dumps(record,indent=2)+'\n')
    print(lib,flush=True)
if __name__=='__main__':main()
