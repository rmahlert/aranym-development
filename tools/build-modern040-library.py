#!/usr/bin/env python3
"""Build the pinned experimental core library from corresponding source.
SPDX-License-Identifier: GPL-2.0-or-later
"""
from pathlib import Path
import argparse,hashlib,json,os,shlex,subprocess
ROOT=Path(__file__).resolve().parent.parent
def sha(p):
    with p.open('rb') as f:return hashlib.file_digest(f,'sha256').hexdigest()
def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--output',required=True,type=Path)
    p.add_argument('--cxx',default=os.environ.get('CXX','c++'))
    p.add_argument('--ar',default=os.environ.get('AR','ar'))
    a=p.parse_args();out=a.output.resolve()
    if out==ROOT or ROOT in out.parents:raise RuntimeError('choose an out-of-tree build directory')
    marker=out/'owned-modern040-library.json'
    if out.exists() and any(out.iterdir()) and not marker.is_file():raise RuntimeError('unmarked existing output refused')
    source=ROOT/'src/modern040';runner=ROOT/'tests/cpu-core-upgrade/runner'
    pins=json.loads((source/'SOURCE.json').read_text())
    assert sha(source/'integer.cpp')==pins['integer_sha256']
    restart=json.loads((source/'RESTART-SOURCE.json').read_text())
    assert sha(source/'restart.cpp')==restart['restart_sha256']
    assert sha(source/'include/uae/types.h')==pins['types_sha256']
    for n,h in pins['runner_inputs'].items():assert sha(ROOT/n)==h,n
    out.mkdir(parents=True,exist_ok=True)
    marker.write_text('{"purpose":"source-built experimental modern040 library"}\n')
    flags=['-std=c++20','-O2','-g','-fwrapv','-DFPUEMU','-I'+str(runner),'-I'+str(source/'include')]
    commands=[];objects=[]
    for s in [source/'integer.cpp',source/'restart.cpp',runner/'core.cpp',runner/'c-api.cpp']:
        obj=out/(s.stem+'.o');cmd=[*shlex.split(a.cxx),*flags,'-c',str(s),'-o',str(obj)]
        commands.append(cmd);subprocess.run(cmd,check=True);objects.append(obj)
    lib=out/'libmodern040.a';cmd=[*shlex.split(a.ar),'rcs',str(lib),*map(str,objects)]
    commands.append(cmd);subprocess.run(cmd,check=True)
    record={'source_pin':pins['upstream_pin'],'compiler':subprocess.check_output([*shlex.split(a.cxx),'--version'],text=True).splitlines()[0],'commands':commands,'library_sha256':sha(lib),'sources':{str(s.relative_to(ROOT)):sha(s) for s in [source/'integer.cpp',source/'restart.cpp',source/'RESTART-SOURCE.json',source/'include/uae/types.h',runner/'core.cpp',runner/'core.hpp',runner/'restart.hpp',runner/'c-api.cpp',runner/'c-api.h']},'objects':{o.name:sha(o) for o in objects},'scope':pins['scope']}
    (out/'provenance.json').write_text(json.dumps(record,indent=2)+'\n')
    print(lib,flush=True)
if __name__=='__main__':main()
