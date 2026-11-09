#!/usr/bin/env python3
"""Build the experimental full upstream x86 JIT in an owned Linux directory.
Never installs or launches an emulator. SPDX-License-Identifier: GPL-2.0-or-later
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shlex
import subprocess

HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[1]
def run(command,**kwargs):subprocess.run([str(x) for x in command],check=True,**kwargs)
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--build-dir',type=Path,required=True)
    p.add_argument('--profile',type=Path,required=True)
    p.add_argument('--jobs',type=int,default=6)
    p.add_argument('--adopt-configured',action='store_true')
    a=p.parse_args()
    if platform.system()!='Linux' or platform.machine()!='x86_64':
        raise RuntimeError('this first integration profile is Linux x86-64 only')
    build=a.build_dir.resolve();profile=a.profile.resolve()
    if build==ROOT or ROOT in build.parents:raise RuntimeError('use a separate owned out-of-tree build')
    relative=profile.relative_to(Path.home())
    if not re.fullmatch('[A-Za-z0-9_./-]+',str(relative)):raise RuntimeError('profile must be a simple owned path beneath home')
    cppflags='-DEXPERIMENTAL_UPSTREAM_X86_JIT -DARANYMHOME=\\"'+str(relative)+'\\"'
    flags={'CPPFLAGS':cppflags,'CFLAGS':'-O2 -g -fno-pie','CXXFLAGS':'-O2 -g -fno-pie','LDFLAGS':'-no-pie'}
    configuration={'environment':flags,'configure':['--disable-opengl','--enable-jit-compiler','--disable-jit-fpu','--enable-addressing=normal']}
    build.mkdir(parents=True,exist_ok=True);profile.mkdir(parents=True,exist_ok=True)
    marker=build/'upstream-jit-configuration.json'
    if (build/'config.status').exists():
        if marker.exists():
            if json.loads(marker.read_text())!=configuration:raise RuntimeError('configuration changed; choose a new dedicated directory')
        elif not a.adopt_configured:raise RuntimeError('refusing an unmarked existing build')
        else:
            configured=shlex.split(subprocess.check_output([build/'config.status','--config'],text=True))
            for key,expected in flags.items():
                if key+'='+expected not in configured:raise RuntimeError('existing configure '+key+' differs from this profile')
    else:
        env=os.environ.copy();env['NO_CONFIGURE']='1';run([ROOT/'autogen.sh'],cwd=ROOT,env=env)
        env=os.environ.copy();env.update(flags)
        run([ROOT/'configure',*configuration['configure']],cwd=build,env=env)
    marker.write_text(json.dumps(configuration,indent=2)+'\n')
    run(['make','-j'+str(a.jobs)],cwd=build)
    binary=build/'src/aranym'
    inputs=[ROOT/'src/uae_cpu/Makefile.am',*sorted((ROOT/'src/uae_cpu/compiler').glob('*x86*')),
            ROOT/'src/uae_cpu/compiler/compemu.h',ROOT/'src/uae_cpu/compiler/compemu_support.cpp',
            *sorted(HERE.glob('upstream-*')),Path(__file__)]
    record={'source_commit':subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),
            'upstream_pin':'d82bc2549f27943d11dc59482d5388daf1c0f337','configuration':configuration,
            'source_sha256':{str(x.relative_to(ROOT)):sha(x) for x in inputs},
            'binary':str(binary),'binary_sha256':sha(binary),'profile':str(profile),
            'limits':['experimental Linux x86-64 no-PIE fixed-address native profile',
                      'native ARAnyM interpreter fallback; modern040 fallback not linked yet',
                      'memory/traps/FPU remain interpreter operations; no MMU/desktop/performance acceptance']}
    (build/'upstream-jit-build.json').write_text(json.dumps(record,indent=2)+'\n')
    print('Built experimental full upstream JIT; not launched or installed:',binary)
if __name__=='__main__':main()
