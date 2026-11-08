#!/usr/bin/env python3
"""Cross-build private paired Windows core candidates; never launch or install.
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
PIN='d82bc2549f27943d11dc59482d5388daf1c0f337'
def run(args,**kwargs):subprocess.run([str(a) for a in args],check=True,**kwargs)
def sha(path):
    with path.open('rb') as source:return hashlib.file_digest(source,'sha256').hexdigest()
def value(path,name):
    return next(line.partition(' = ')[2] for line in path.read_text().splitlines() if line.startswith(name+' = '))
def verify_profile(build,profile,env):
    config=build/'config.h';text=config.read_text()
    text,count=re.subn(r'^#define ARANYMHOME .*$', '#define ARANYMHOME "'+profile+'"',text,flags=re.M)
    if count!=1:raise RuntimeError('cannot isolate generated Windows profile')
    config.write_text(text)
    defines=shlex.split(value(build/'src/Makefile','DEFINES'))
    if '-DFPU_IEEE' not in defines or '-DNORMAL_ADDRESSING' not in defines or any(x.startswith('-DUSE_JIT') for x in defines):raise RuntimeError('unqualified native CPU/addressing/FPU profile')
    for feature in ('HAVE_SLIRP','NFJPEG_SUPPORT'):
        if '#define '+feature+' 1' not in text:raise RuntimeError('missing accepted feature '+feature)
    macro=subprocess.check_output([env['CXX'],'-dM','-E','-x','c++','-include',str(config),'-'],input='',text=True,env=env)
    expected='#define ARANYMHOME "'+profile+'"'
    if expected not in macro.splitlines():raise RuntimeError('effective Windows profile override failed')
    return defines

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('upstream','mingw-root','jpeg-root'):parser.add_argument('--'+name,required=True,type=Path)
    parser.add_argument('--build-dir',type=Path,default=ROOT.parent/'build-windows-experimental')
    parser.add_argument('--native-core',type=Path,default=HERE.parent/'build-runner-static')
    parser.add_argument('--jobs',type=int,default=6)
    parser.add_argument('--reuse-objects',action='store_true')
    args=parser.parse_args();upstream=args.upstream.resolve();build=args.build_dir.resolve();native=args.native_core.resolve()
    mingw=args.mingw_root.resolve();jpeg=args.jpeg_root.resolve();build.mkdir(parents=True,exist_ok=True)
    provenance=json.loads((native/'runner-provenance.json').read_text())
    if provenance['commit']!=PIN or provenance['runner']['build']['sanitized']:raise RuntimeError('requires tested normal pinned native generation')
    if subprocess.check_output(['git','-C',str(upstream),'rev-parse','HEAD'],text=True).strip()!=PIN:raise RuntimeError('incorrect upstream pin')
    code={p.name for p in RUNNER.iterdir() if p.is_file() and p.suffix in {'.py','.cpp','.hpp','.h','.c'}}
    expected={n:h for n,h in provenance['runner']['source_sha256'].items() if Path(n).suffix in {'.py','.cpp','.hpp','.h','.c'}}
    if code!=set(expected):raise RuntimeError('native core source file set changed')
    for name,digest in expected.items():
        if sha(RUNNER/name)!=digest:raise RuntimeError('native-tested source changed '+name)
    env=os.environ.copy();env.update(CC='x86_64-w64-mingw32-gcc',CXX='x86_64-w64-mingw32-g++',AR='x86_64-w64-mingw32-ar',RANLIB='x86_64-w64-mingw32-ranlib',CC_FOR_BUILD='gcc',CXX_FOR_BUILD='g++',CXXCPP_FOR_BUILD='g++ -E',PKG_CONFIG_LIBDIR=str(mingw/'lib/pkgconfig'),PKG_CONFIG_SYSROOT_DIR=str(mingw.parent))
    env.pop('PKG_CONFIG_PATH',None)
    common_cpp='-I'+str(jpeg/'include');env['LDFLAGS']='-L'+str(jpeg/'lib')
    env['SDL2_CFLAGS']='-I'+str(mingw/'include/SDL2');env['SDL2_LIBS']='-L'+str(mingw/'lib')+' -lSDL2'
    core=build/'modern-objects';core.mkdir(exist_ok=True)
    flags=['-std=c++20','-O2','-g','-fwrapv','-DFPUEMU','-I'+str(RUNNER),'-I'+str(upstream/'include')]
    core_inputs={str(p):sha(p) for p in [native/'integer.cpp',RUNNER/'core.cpp',RUNNER/'c-api.cpp',RUNNER/'core.hpp',RUNNER/'c-api.h']}
    core_identity={'inputs':core_inputs,'compiler':subprocess.check_output([env['CXX'],'--version'],text=True).splitlines()[0],'flags':flags}
    core_marker=core/'inputs.json'
    reuse=args.reuse_objects and core_marker.exists() and json.loads(core_marker.read_text())==core_identity
    if not reuse:
        for source in (native/'integer.cpp',RUNNER/'core.cpp',RUNNER/'c-api.cpp'):
            print('CROSS COMPILE modern '+source.name,flush=True)
            run([env['CXX'],*flags,'-c',source,'-o',core/('modern040-'+source.stem+'.o')],env=env)
        core_marker.write_text(json.dumps(core_identity,indent=2)+'\n')
    configure=['--host=x86_64-w64-mingw32','--disable-opengl','--disable-jit-compiler','--enable-addressing=normal','--enable-slirp','--enable-nfjpeg','--enable-disasm=builtin','--disable-flightrecorder','--disable-fullhistory']
    records=[]
    for modern in (True,False):
        kind='modern' if modern else 'baseline';tree=build/kind;tree.mkdir(exist_ok=True)
        profile='aranym-modern040-experimental' if modern else 'aranym-modern040-baseline'
        local=env.copy();local['CPPFLAGS']=common_cpp+(' -DEXPERIMENTAL_MODERN040' if modern else '')
        identity={'configure':configure,'cppflags':local['CPPFLAGS'],'ldflags':local['LDFLAGS'],'SDL2_CFLAGS':local['SDL2_CFLAGS'],'SDL2_LIBS':local['SDL2_LIBS'],'profile':profile}
        marker=tree/'owned-configuration.json'
        if (tree/'config.status').exists():
            if not marker.exists() or json.loads(marker.read_text())!=identity:raise RuntimeError('refusing changed/unowned configure tree '+str(tree))
        else:
            print('CONFIGURE '+kind,flush=True);run([ROOT/'configure',*configure],cwd=tree,env=local)
            marker.write_text(json.dumps(identity,indent=2)+'\n')
        defines=verify_profile(tree,profile,local)
        run(['make','version_date.h'],cwd=tree,env=local)
        run(['make','-j'+str(args.jobs),'-C','src/uae_cpu'],cwd=tree,env=local)
        run(['make','-j'+str(args.jobs),'-C','src','libaranym.a'],cwd=tree,env=local)
        if modern:
            adapter=tree/'modern040-aranym-adapter.o'
            run([env['CXX'],*defines,'-DHAVE_CONFIG_H','-DEXPERIMENTAL_MODERN040','-I'+str(tree),'-I'+str(tree/'src'),'-I'+str(ROOT/'src/include'),'-I'+str(ROOT/'src/Unix'),'-I'+str(ROOT/'src/uae_cpu'),'-I'+str(tree/'src/uae_cpu'),'-I'+str(mingw/'include/SDL2'),'-O2','-Wall','-Wextra','-c',HERE/'adapter.cpp','-o',adapter],env=local)
            run([env['AR'],'rcs',tree/'src/libaranym.a',adapter,*[core/('modern040-'+name+'.o') for name in ('integer','core','c-api')]],env=local)
        run(['make','-j'+str(args.jobs),'-C','src','aranym.exe'],cwd=tree,env=local)
        if verify_profile(tree,profile,local)!=defines:raise RuntimeError('native public header ABI changed')
        exe=tree/'src/aranym.exe'
        records.append({'kind':kind,'binary':str(exe),'sha256':sha(exe),'configuration':identity,'config_h_sha256':sha(tree/'config.h'),'native_defines':defines,'profile_path':'%USERPROFILE%\\'+profile,'profile_macro_verified':True})
        print('LINKED '+kind+' '+records[-1]['sha256'],flush=True)
    identity={'experimental':True,'launched':False,'installed':False,'source_commit':subprocess.check_output(['git','-C',str(ROOT),'rev-parse','HEAD'],text=True).strip(),'upstream_pin':PIN,'native_test_provenance_sha256':sha(native/'runner-provenance.json'),'core':core_identity,'builds':records,'source_sha256':{str(p.relative_to(ROOT)):sha(p) for p in [ROOT/'src/uae_cpu/newcpu.cpp',HERE/'adapter.cpp',HERE/'build-windows.py']},'limits':['modern integer with explicitly original IEEE FPU; not modernFPU','noJIT/noMMU/generalrestart; no speedup claim','requires advance notice and owned closed guest copy before Windows desktop test']}
    (build/'windows-build-identity.json').write_text(json.dumps(identity,indent=2)+'\n')
    print('PAIRED WINDOWS BUILD COMPLETE; neither executable launched',flush=True)
if __name__=='__main__':main()
