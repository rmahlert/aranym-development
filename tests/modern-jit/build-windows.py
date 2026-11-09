#!/usr/bin/env python3
"""Cross-build an isolated modern-core/JIT Windows candidate; never launch/install.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import argparse,hashlib,json,os,re,shlex,shutil,subprocess
from pathlib import Path
HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[1]
RUNNER=ROOT/'tests/cpu-core-upgrade/runner'
PIN='d82bc2549f27943d11dc59482d5388daf1c0f337'
def sha(p):
    with p.open('rb') as stream:return hashlib.file_digest(stream,'sha256').hexdigest()
def run(command,**kw):subprocess.run([str(x) for x in command],check=True,**kw)
def value(path,name):return next(line.partition(' = ')[2] for line in path.read_text().splitlines() if line.startswith(name+' = '))
def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('build-dir','mingw-root','jpeg-root','core-dir'):p.add_argument('--'+name,required=True,type=Path)
    p.add_argument('--profile',default='aranym-upstream-jit-experimental')
    p.add_argument('--jobs',type=int,default=6)
    a=p.parse_args();build=a.build_dir.resolve();mingw=a.mingw_root.resolve(strict=True);jpeg=a.jpeg_root.resolve(strict=True);core=a.core_dir.resolve(strict=True)
    if not re.fullmatch(r'aranym-upstream-jit-[A-Za-z0-9_-]+',a.profile):raise RuntimeError('use an isolated upstream-JIT experiment profile name')
    if build==ROOT or ROOT in build.parents:raise RuntimeError('choose a separate owned out-of-tree build directory')
    inputs=json.loads((core/'inputs.json').read_text())
    seed=json.loads((core/'seed-build-identity.json').read_text())
    if seed.get('upstream_pin')!=PIN or seed.get('core')!=inputs:raise RuntimeError('copied Windows seed does not match pinned verified core inputs')
    for name,expected in inputs['inputs'].items():
        source=core/'integer.cpp' if Path(name).name=='integer.cpp' else RUNNER/Path(name).name
        if sha(source)!=expected:raise RuntimeError('copied Windows core source differs '+str(source))
    env=os.environ.copy();env.update(CC='x86_64-w64-mingw32-gcc',CXX='x86_64-w64-mingw32-g++',AR='x86_64-w64-mingw32-ar',RANLIB='x86_64-w64-mingw32-ranlib',CC_FOR_BUILD='gcc',CXX_FOR_BUILD='g++',CXXCPP_FOR_BUILD='g++ -E',PKG_CONFIG_LIBDIR=str(mingw/'lib/pkgconfig'),PKG_CONFIG_SYSROOT_DIR=str(mingw.parent))
    env.pop('PKG_CONFIG_PATH',None)
    env['CPPFLAGS']='-I'+str(jpeg/'include')+' -DEXPERIMENTAL_UPSTREAM_X86_JIT -DEXPERIMENTAL_UPSTREAM_MODERN040'
    env['LDFLAGS']='-L'+str(jpeg/'lib')
    env['SDL2_CFLAGS']='-I'+str(mingw/'include/SDL2');env['SDL2_LIBS']='-L'+str(mingw/'lib')+' -lSDL2'
    configure=['--host=x86_64-w64-mingw32','--disable-opengl','--enable-jit-compiler','--disable-jit-fpu','--enable-addressing=normal','--enable-slirp','--enable-nfjpeg','--enable-disasm=builtin','--disable-flightrecorder','--disable-fullhistory']
    configuration={'configure':configure,'environment':{k:env[k] for k in ('CPPFLAGS','LDFLAGS','SDL2_CFLAGS','SDL2_LIBS')},'profile':a.profile}
    build.mkdir(parents=True,exist_ok=True);marker=build/'owned-jit-windows-configuration.json'
    if (build/'config.status').exists():
        if not marker.exists() or json.loads(marker.read_text())!=configuration:raise RuntimeError('refusing changed/unmarked existing Windows build')
    else:
        local=os.environ.copy();local['NO_CONFIGURE']='1';run([ROOT/'autogen.sh'],cwd=ROOT,env=local)
        run([ROOT/'configure',*configure],cwd=build,env=env)
        marker.write_text(json.dumps(configuration,indent=2)+'\n')
    config=build/'config.h';text=config.read_text();text,count=re.subn(r'^#define ARANYMHOME .*$', '#define ARANYMHOME "'+a.profile+'"',text,flags=re.M)
    if count!=1:raise RuntimeError('cannot isolate generated Windows profile')
    if config.read_text()!=text:config.write_text(text)
    defines=shlex.split(value(build/'src/Makefile','DEFINES'))
    if not all(x in defines for x in ('-DFPU_IEEE','-DFIXED_ADDRESSING','-DUSE_JIT=1')):raise RuntimeError('incorrect JIT/addressing/FPU profile')
    for macro in ('HAVE_SLIRP','NFJPEG_SUPPORT'):
        if '#define '+macro+' 1' not in text:raise RuntimeError('missing accepted Windows dependency '+macro)
    effective=subprocess.check_output([env['CXX'],'-dM','-E','-x','c++','-include',str(config),'-'],input='',text=True,env=env)
    if '#define ARANYMHOME "'+a.profile+'"' not in effective.splitlines():raise RuntimeError('effective Windows profile does not match')
    run(['make','version_date.h'],cwd=build,env=env)
    run(['make','-j'+str(a.jobs),'-C','src/uae_cpu'],cwd=build,env=env)
    run(['make','-j'+str(a.jobs),'-C','src','libaranym.a'],cwd=build,env=env)
    adapter=build/'modern-jit-fallback.o'
    run([env['CXX'],*defines,'-DHAVE_CONFIG_H','-DEXPERIMENTAL_UPSTREAM_X86_JIT','-DEXPERIMENTAL_UPSTREAM_MODERN040','-I'+str(build),'-I'+str(build/'src'),'-I'+str(ROOT/'src/include'),'-I'+str(ROOT/'src/Unix'),'-I'+str(ROOT/'src/uae_cpu'),'-I'+str(build/'src/uae_cpu'),'-I'+str(mingw/'include/SDL2'),'-O2','-Wall','-Wextra','-c',HERE/'modern-fallback.cpp','-o',adapter],env=env)
    objects=[adapter]
    for name in ('integer','core','c-api'):
        target=build/('modern040-'+name+'.o');shutil.copyfile(core/target.name,target);objects.append(target)
    run([env['AR'],'rcs',build/'src/libaranym.a',*objects],env=env)
    run(['make','-j'+str(a.jobs),'-C','src','aranym.exe'],cwd=build,env=env)
    binary=build/'src/aranym.exe'
    sourcefiles=[ROOT/'src/uae_cpu/newcpu.cpp',ROOT/'src/Unix/vm_alloc.cpp',ROOT/'src/uae_cpu/compiler/compemu_support.cpp',ROOT/'src/uae_cpu/compiler/compemu.h',ROOT/'src/uae_cpu/compiler/codegen_x86.cpp',ROOT/'src/uae_cpu/compiler/codegen_x86.h',ROOT/'src/uae_cpu/compiler/compemu_midfunc_x86.cpp',ROOT/'src/uae_cpu/compiler/compemu_midfunc_x86.h',HERE/'modern-fallback.cpp',HERE/'build-windows.py',*sorted(HERE.glob('upstream-*'))]
    imports=subprocess.check_output(['x86_64-w64-mingw32-objdump','-p',str(binary)],text=True)
    record={'experimental':True,'launched':False,'installed':False,'source_commit':subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),'upstream_pin':PIN,'configuration':configuration,'effective_defines':defines,'effective_ldflags':value(build/'src/Makefile','LDFLAGS'),'effective_cxxflags':value(build/'src/Makefile','CXXFLAGS'),'profile_path':'%USERPROFILE%\\'+a.profile,'profile_macro_verified':True,'binary':str(binary),'binary_sha256':sha(binary),'config_h_sha256':sha(config),'source_sha256':{str(x.relative_to(ROOT)):sha(x) for x in sourcefiles},'core_input_sha256':sha(core/'inputs.json'),'seed_build_identity_sha256':sha(core/'seed-build-identity.json'),'core_object_sha256':{name:sha(core/('modern040-'+name+'.o')) for name in ('integer','core','c-api')},'dll_imports':re.findall(r'DLL Name:\s+(\S+)',imports),'image_base':next((line.split()[-1] for line in imports.splitlines() if line.startswith('ImageBase')),None),'limits':['cross-link/static ABI checks only; no Windows runtime acceptance','64MiB no-MMU/default register-control JIT profile','memory-JIT disabled; original IEEE FPU optional host service','use owned copied profiles/images; no installer/public release']}
    (build/'windows-jit-build.json').write_text(json.dumps(record,indent=2)+'\n');print('Cross-built private Windows JIT candidate; not launched/installed:',binary)
if __name__=='__main__':main()
