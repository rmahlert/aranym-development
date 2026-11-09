#!/usr/bin/env python3
"""Build owned ordinary Atari guest integration fixtures; never launch/install.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import argparse,hashlib,json
from pathlib import Path
import subprocess
HERE=Path(__file__).resolve().parent

def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--out-dir',required=True,type=Path)
    p.add_argument('--cc',default='m68k-atari-mint-gcc')
    a=p.parse_args();out=a.out_dir.resolve();marker=out/'owned-guest-fixtures.json'
    if out.exists() and any(out.iterdir()) and not marker.exists():raise RuntimeError('choose a new owned fixture directory')
    out.mkdir(parents=True,exist_ok=True);marker.write_text('{"purpose":"owned JIT integration guests"}\n')
    commands=[]
    def run(command):commands.append([str(x) for x in command]);subprocess.run(commands[-1],check=True)
    cases=[('EMUON','emulop-jit',['-DEMULOP_EXPECT_JIT=1']),('EMUOFF','emulop-jit',['-DEMULOP_EXPECT_JIT=0']),
           ('EMUNEG','emulop-jit',['-DEMULOP_EXPECT_JIT=1','-DEMULOP_EXPECT_PC_DELTA=2']),
           ('INVALID','invalidation',[]),('INVNEG','invalidation',['-DEXPECTED_PATCH=3']),('TRACE','trace',[]),('TRACEN','trace',['-DTRACE_PC_DELTA=2']),('CHECKMEM','checked-move',[]),('MEMNEG','checked-move',['-DCOUNT_DELTA=1']),('MEMFAULT','checked-move',['-DFAULT_TRANSFER=1'])]
    for family in ('emulop-jit','invalidation','trace','checked-move'):
        run([a.cc,'-m68040','-c',HERE/'guest'/(family+'.S'),'-o',out/(family+'.o')])
    for name,family,defines in cases:
        run([a.cc,'-m68000','-msoft-float','-O2','-Wall','-Wextra',*defines,'-c',HERE/'guest'/(family+'.c'),'-o',out/(name+'.o')])
        run([a.cc,'-m68000','-msoft-float',out/(name+'.o'),out/(family+'.o'),'-o',out/(name+'.PRG')])
    record={'commands':commands,'source_sha256':{str(x.relative_to(HERE)):sha(x) for x in sorted((HERE/'guest').iterdir()) if x.is_file()},
            'guest_sha256':{name+'.PRG':sha(out/(name+'.PRG')) for name,_,_ in cases}}
    marker.write_text(json.dumps(record,indent=2)+'\n');print('Built owned Atari fixtures:',out)
if __name__=='__main__':main()
