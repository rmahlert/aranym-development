#!/usr/bin/env python3
"""Build an opt-in experimental native Atari frontend, never install or launch it.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import shutil
import subprocess

HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[2]
RUNNER=HERE.parent/'runner'
def run(args,**kwargs):subprocess.run([str(a) for a in args],check=True,**kwargs)
def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--upstream',required=True,type=Path)
    parser.add_argument('--build-dir',type=Path,default=ROOT.parent/'build-experimental')
    parser.add_argument('--profile',type=Path,default=ROOT.parent/'profile-experimental')
    parser.add_argument('--core-dir',type=Path,default=HERE.parent/'build-runner-static')
    parser.add_argument('--reuse-core',action='store_true')
    parser.add_argument('--adopt-configured',action='store_true',help='adopt only a matching explicitly experimental existing configure tree')
    args=parser.parse_args();build=args.build_dir.resolve();profile=args.profile.resolve();core=args.core_dir.resolve()
    relative=profile.relative_to(Path.home())
    if not re.fullmatch(r'[A-Za-z0-9_./-]+',str(relative)):raise RuntimeError('profile must have a simple workspace path beneath home')
    cppflags='-DEXPERIMENTAL_MODERN040 -DARANYMHOME=\\"'+str(relative)+'\\"'
    configuration={'cppflags':cppflags,'configure':['--disable-opengl','--disable-jit-compiler','--enable-addressing=normal']}
    build.mkdir(parents=True,exist_ok=True);profile.mkdir(parents=True,exist_ok=True)
    marker=build/'modern040-configuration.json'
    if (build/'config.status').exists():
        if marker.exists():
            if json.loads(marker.read_text())!=configuration:raise RuntimeError('configure flags changed; use a new dedicated build directory')
        elif not args.adopt_configured:raise RuntimeError('refusing an unmarked existing configure tree; choose a new directory')
        else:
            line=next((x.partition(' = ')[2] for x in (build/'src/Makefile').read_text().splitlines() if x.startswith('CPPFLAGS = ')),None)
            if line!=cppflags:raise RuntimeError('existing configure CPPFLAGS are not this experimental profile')
    else:
        env=os.environ.copy();env['NO_CONFIGURE']='1';run([ROOT/'autogen.sh'],cwd=ROOT,env=env)
        env=os.environ.copy();env['CPPFLAGS']=cppflags
        run([ROOT/'configure',*configuration['configure']],cwd=build,env=env)
    marker.write_text(json.dumps(configuration,indent=2)+'\n')
    if not args.reuse_core:
        run(['python3',RUNNER/'build.py','--upstream',args.upstream.resolve(),'--build-dir',core],cwd=ROOT)
    provenance=json.loads((core/'runner-provenance.json').read_text())
    if provenance['runner'].get('build',{}).get('sanitized') is not False:raise RuntimeError('frontend requires explicitly nonsanitized core archive')
    code_suffixes={'.py','.cpp','.hpp','.c','.h'}
    expected_code={name:expected for name,expected in provenance['runner']['source_sha256'].items() if Path(name).suffix in code_suffixes}
    actual_code={p.name for p in RUNNER.iterdir() if p.is_file() and p.suffix in code_suffixes}
    if actual_code!=set(expected_code):raise RuntimeError('core source-file set changed; regenerate without --reuse-core')
    for name,expected in expected_code.items():
        if digest(RUNNER/name)!=expected:raise RuntimeError('stale core source '+name+'; regenerate without --reuse-core')
    run(['make','version_date.h'],cwd=build)
    run(['make','-j8','-C','src/uae_cpu'],cwd=build)
    run(['make','-j8','-C','src','libaranym.a'],cwd=build)
    cxx=os.environ.get('CXX','g++')
    sdl=shlex.split(subprocess.check_output(['pkg-config','--cflags','sdl2'],text=True))
    adapter=build/'modern040-aranym-adapter.o'
    run([cxx,'-DHAVE_CONFIG_H','-DEXPERIMENTAL_MODERN040','-I'+str(build),'-I'+str(build/'src'),
         '-I'+str(ROOT/'src/include'),'-I'+str(ROOT/'src/Unix'),'-I'+str(ROOT/'src/uae_cpu'),
         '-I'+str(build/'src/uae_cpu'),*sdl,'-O2','-Wall','-Wextra','-c',HERE/'adapter.cpp','-o',adapter],cwd=build)
    objects=[adapter]
    for name in ('integer','core','c-api'):
        obj=build/('modern040-'+name+'.o');shutil.copyfile(core/(name+'.o'),obj);objects.append(obj)
    # Test target only: append namespaced objects after the ordinary legacy
    # generator archive exists. Normal builds never include this archive patch.
    run(['ar','rcs',build/'src/libaranym.a',*objects],cwd=build)
    run(['make','-j8','-C','src','aranym'],cwd=build)
    binary=build/'src/aranym'
    identity={'experimental':True,'binary':str(binary),'binary_sha256':digest(binary),'configuration':configuration,
              'source_commit':subprocess.check_output(['git','-C',str(ROOT),'rev-parse','HEAD'],text=True).strip(),
              'source_sha256':{str(p.relative_to(ROOT)):digest(p) for p in (ROOT/'src/uae_cpu/newcpu.cpp',HERE/'adapter.cpp',HERE/'adapter.h')},
              'core_provenance_sha256':digest(core/'runner-provenance.json'),'profile':str(profile),
              'limits':['JIT/MMU/HW-signal addressing rejected','accessfault/RTE7 restart and nested EmulOp fail closed','builder does not install or launch']}
    (build/'modern040-build.json').write_text(json.dumps(identity,indent=2)+'\n')
    print('EXPERIMENTAL binary built (not launched):',binary)
if __name__=='__main__':main()
