#!/usr/bin/env python3
"""Run one explicitly owned headless Atari guest fixture, never install anything.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import time

def sha(path):
    with path.open('rb') as source:return hashlib.file_digest(source,'sha256').hexdigest()
def main():
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('binary','rom','guest','work-dir'):parser.add_argument('--'+name,required=True,type=Path)
    parser.add_argument('--expect-exit',type=int,default=0)
    parser.add_argument('--expect-marker',required=True)
    parser.add_argument('--legacy-fpu',action='store_true')
    parser.add_argument('--jit',action='store_true')
    parser.add_argument('--timeout',type=int,default=30)
    args=parser.parse_args()
    if sys.platform!='linux':raise RuntimeError('this experimental recipe is qualified only for native Linux')
    for name in ('binary','rom','guest'):
        path=getattr(args,name).resolve(strict=True)
        if not path.is_file():raise RuntimeError(name+' must be a file')
        setattr(args,name,path)
    if args.timeout<1 or args.timeout>300:raise RuntimeError('fixture timeout must be1..300s')
    work=args.work_dir.resolve();marker=work/'owned-bootstrap-fixture.json'
    if work.exists() and any(work.iterdir()) and not marker.exists():raise RuntimeError('choose a new owned work directory')
    work.mkdir(parents=True,exist_ok=True);(work/'host').mkdir(exist_ok=True)
    marker.write_text(json.dumps({'purpose':'owned no-disk Atari bootstrap fixture'})+'\n')
    config=work/'config';config.write_text(f'''[GLOBAL]
FastRAM = 64
EmuTOS = {args.rom}
Bootstrap = {args.guest}
[JIT]
JIT = {"Yes" if args.jit else "No"}
JITCacheSize = 8192
JITFPU = No
[HOSTFS]
H = {work/'host'}
[ETH0]
Type =
Tunnel =
[STARTUP]
GrabMouse = No
[AUDIO]
Enabled = No
[TOS]
RedirConsole = Yes
''')
    env=os.environ.copy();env.update(SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy')
    env['ARANYM_UPSTREAM_JIT_DIAGNOSTICS']='1'
    env.pop('ARANYM_MODERN040_DIAGNOSTICS',None);env.pop('ARANYM_MODERN040_LEGACY_FPU',None)
    if args.legacy_fpu:env['ARANYM_MODERN040_LEGACY_FPU']='1'
    inputs={name:{'path':str(getattr(args,name)),'sha256':sha(getattr(args,name))} for name in ('binary','rom','guest')}
    started=time.monotonic();log=work/'run.log'
    with log.open('w') as out:
        result=subprocess.run(['timeout','-k','5',str(args.timeout),str(args.binary),'-c',str(config)],cwd=work,env=env,stdout=out,stderr=subprocess.STDOUT)
    elapsed=time.monotonic()-started;found=args.expect_marker in log.read_text(errors='replace')
    record={'inputs':inputs,'jit':args.jit,'legacy_fpu':args.legacy_fpu,'exit':result.returncode,'expected_exit':args.expect_exit,'marker':args.expect_marker,'marker_found':found,'seconds':elapsed,'config_sha256':sha(config),'log_sha256':sha(log),'passed':result.returncode==args.expect_exit and found}
    (work/'result.json').write_text(json.dumps(record,indent=2)+'\n');print(json.dumps(record,indent=2))
    return 0 if record['passed'] else 1
if __name__=='__main__':raise SystemExit(main())
