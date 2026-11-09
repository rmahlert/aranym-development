#!/usr/bin/env python3
"""Inspect the owned cross-build and preprocessed host ABI; never launch its PE.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import argparse,hashlib,json,re,shlex,subprocess
from pathlib import Path
HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[1]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def value(p,name):return next(line.partition(' = ')[2] for line in p.read_text().splitlines() if line.startswith(name+' = '))
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--build-dir',required=True,type=Path);p.add_argument('--mingw-root',required=True,type=Path);p.add_argument('--reference-pe',required=True,type=Path);a=p.parse_args()
    build=a.build_dir.resolve();record=json.loads((build/'windows-jit-build.json').read_text());binary=Path(record['binary'])
    if sha(binary)!=record['binary_sha256']:raise RuntimeError('candidate changed after build')
    if record['image_base']!='0000000010000000' or not record['profile_macro_verified']:raise RuntimeError('wrong low-image/profile metadata')
    defines=shlex.split(value(build/'src/Makefile','DEFINES'));cpp=shlex.split(value(build/'src/Makefile','CPPFLAGS'))
    command=['x86_64-w64-mingw32-g++',*defines,*cpp,'-DHAVE_CONFIG_H','-I'+str(build),'-I'+str(build/'src'),'-I'+str(ROOT/'src/include'),'-I'+str(ROOT/'src/Unix'),'-I'+str(ROOT/'src/uae_cpu'),'-I'+str(build/'src/uae_cpu'),'-I'+str(a.mingw_root.resolve()/'include/SDL2'),'-E']
    source=ROOT/'src/uae_cpu/compiler/compemu_support.cpp'
    macros=subprocess.check_output([*command,'-dM',str(source)],text=True)
    macro_map=dict(re.findall(r'^#define (\w+) (.+)$',macros,re.M))
    expected={'X86_TARGET_64BIT':'1','REG_PAR1':'ECX_INDEX','REG_PAR2':'EDX_INDEX','STACK_SHADOW_SPACE':'32','STACK_ALIGN':'16','USE_NORMAL_CALLING_CONVENTION':'0'}
    for name,want in expected.items():
        if macro_map.get(name)!=want:raise RuntimeError('wrong target ABI macro '+name+'='+str(macro_map.get(name)))
    expanded=subprocess.check_output([*command,'-P',str(source)],text=True)
    array=re.search(r'need_to_preserve\[\]\s*=\s*\{([^}]+)\}',expanded)
    if not array:raise RuntimeError('missing actual target preservation table')
    preserved=[i for i,x in enumerate(array.group(1).split(',')) if int(x.strip())]
    if preserved!=[3,5,6,7,11,12,13,14,15]:raise RuntimeError('wrong Win64 preservation register table')
    reference=subprocess.check_output(['x86_64-w64-mingw32-objdump','-p',str(a.reference_pe)],text=True)
    reference_dlls=re.findall(r'DLL Name:\s+(\S+)',reference)
    if set(reference_dlls)!=set(record['dll_imports']):raise RuntimeError('direct DLL set differs from the existing accepted dependency set')
    result={'static_only':True,'launched':False,'candidate_sha256':sha(binary),'reference_pe_sha256':sha(a.reference_pe),'same_direct_dll_set':True,'profile':record['profile_path'],'image_base':record['image_base'],'selected_abi_macros':{k:macro_map[k] for k in expected},'preserved_register_indices':preserved,'note':'RSP is restored by stack-balanced entry/exit; R11 is an extra upstream-preserved volatile register. These source/PE checks do not establish runtime ABI acceptance.'}
    (build/'windows-jit-static-review.json').write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(result,indent=2))
if __name__=='__main__':main()
