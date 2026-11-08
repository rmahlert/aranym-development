#!/usr/bin/env python3
"""Freeze private paired PE/runtime artifacts by static inspection only.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[2]
def sha(path):
    with path.open('rb') as source:return hashlib.file_digest(source,'sha256').hexdigest()
def main():
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('build-dir','mingw-root','jpeg-root','output'):parser.add_argument('--'+name,required=True,type=Path)
    args=parser.parse_args();build=args.build_dir.resolve();output=args.output.resolve()
    if output.exists():raise RuntimeError('frozen output already exists; choose a new directory')
    identity=json.loads((build/'windows-build-identity.json').read_text())
    if identity['launched'] or identity['installed']:raise RuntimeError('unexpected artifact provenance')
    for entry in identity['builds']:
        if not entry['profile_macro_verified'] or sha(Path(entry['binary']))!=entry['sha256']:raise RuntimeError('changed/unverified PE input')
    output.mkdir(parents=True);copies=[]
    for entry in identity['builds']:
        name='aranym-modern040-experimental.exe' if entry['kind']=='modern' else 'aranym-original-core-baseline.exe'
        target=output/name;shutil.copyfile(entry['binary'],target);copies.append(target)
        profile=entry['configuration']['profile']
        if profile.encode() not in target.read_bytes():raise RuntimeError('effective profile literal absent from linked PE')
    # The existing dependency collector handles the complete transitive closure.
    # JPEG comes from a separate already-owned prefix, so seed that exact DLL.
    shutil.copyfile(args.jpeg_root/'bin/libjpeg-8.dll',output/'libjpeg-8.dll')
    env=os.environ.copy();env['MINGW_ROOT']=str(args.mingw_root.resolve());env['MINGW_CXX']='x86_64-w64-mingw32-g++'
    imports=subprocess.check_output([str(ROOT/'tools/collect-mingw-dlls.sh'),str(output),*[str(p) for p in copies]],env=env,text=True)
    (output/'DEPENDENCIES.txt').write_text(imports)
    licenses=output/'licenses';licenses.mkdir()
    for name in ('COPYING','AUTHORS'):
        if (ROOT/name).is_file():shutil.copyfile(ROOT/name,licenses/(name+'.ARAnyM'))
    for prefix in (args.mingw_root,args.jpeg_root):
        path=prefix/'share/licenses'
        if path.exists():
            for source in path.iterdir():
                target=licenses/source.name
                if not target.exists():shutil.copytree(source,target) if source.is_dir() else shutil.copyfile(source,target)
    for kind,name in [('modern','run-modern-core.cmd'),('baseline','run-original-baseline.cmd')]:
        exe='aranym-modern040-experimental.exe' if kind=='modern' else 'aranym-original-core-baseline.exe'
        setting='1' if kind=='modern' else ''
        text=f'''@echo off
setlocal
if "%~1"=="" (
  echo Usage: {name} "FULL PATH TO OWNED DISPOSABLE CONFIG"
  exit /b 2
)
if not exist "%~f1" (
  echo Config file does not exist.
  exit /b 2
)
set ARANYM_MODERN040_LEGACY_FPU={setting}
set ARANYM_MODERN040_DIAGNOSTICS=
"%~dp0{exe}" -c "%~f1"
exit /b %errorlevel%
'''
        (output/name).write_bytes(text.replace('\n','\r\n').encode())
    shutil.copyfile(HERE/'windows-test-README.txt',output/'README-FIRST.txt')
    shutil.copyfile(build/'windows-build-identity.json',output/'BUILD-IDENTITY.json')
    source_commit=subprocess.check_output(['git','-C',str(ROOT),'rev-parse','HEAD'],text=True).strip()
    record={'frozen':True,'launched':False,'installed':False,'source_commit_at_freeze':source_commit,'build_source_commit':identity['source_commit'],'upstream_pin':identity['upstream_pin'],'files':{str(p.relative_to(output)):sha(p) for p in sorted(output.rglob('*')) if p.is_file()},'packaging_recipe_sha256':sha(Path(__file__))}
    (output/'SHA256-MANIFEST.json').write_text(json.dumps(record,indent=2)+'\n')
    print('FROZEN PRIVATE RUNTIME (NOT LAUNCHED):',output)
    for target in copies:print(target.name,sha(target))
if __name__=='__main__':main()
