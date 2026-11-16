#!/usr/bin/env python3
"""Run identical owned 68k correctness guests with native ARM64 JIT on and off.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import shutil
import subprocess
import sys
import tempfile

HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[1]

def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    mode=parser.add_mutually_exclusive_group(required=True)
    mode.add_argument('--app',type=Path)
    mode.add_argument('--emulator',type=Path)
    parser.add_argument('--firmware',type=Path)
    parser.add_argument('--output',required=True,type=Path)
    args=parser.parse_args()
    if args.app:
        if sys.platform!='darwin' or platform.machine()!='arm64': parser.error('native Apple Silicon required')
    elif sys.platform!='linux' or platform.machine() not in ('aarch64','arm64'):
        parser.error('native Linux ARM64 required')
    if args.emulator and not args.firmware: parser.error('--emulator needs --firmware')
    output=args.output.resolve();output.mkdir(parents=True,exist_ok=True)
    pins=json.loads((HERE/'fixtures/provenance.json').read_text())
    for name,expected in pins['source_sha256'].items():
        if digest(ROOT/name)!=expected: raise RuntimeError('guest source changed: '+name)
    cases=[('emulop','EMUON','EMUOFF','EMULOP JIT GUEST PASS',0),
           ('fpu-fallback','ARMFPU','ARMFPU','ARM FPU FALLBACK PASS',0),
           ('bridge','MASTER','MASTER','CPU BRIDGE PASS all checks',0),
           ('vbl','MVBL','MVBL','MASTER IRQ PASS',0),
           ('mfp','MMFP','MMFP','MASTER IRQ PASS',0),
           ('invalidate','INVALID','INVALID','JIT INVALIDATION GUEST PASS',0),
           ('trace','TRACE','TRACE','JIT TRACE GUEST PASS',0),
           ('negative','EMUNEG','EMUNEG','EMULOP JIT GUEST FAIL',3)]
    report={'native_arch':platform.machine(),'platform':sys.platform,'profiles':[], 'passed':False}
    report_path=output/'arm64-guests.json'
    try:
        for label,on,off,marker,status in cases:
            # The emulop negative control intentionally expects JIT enabled.
            for jit in ((True,) if label=='negative' else (False,True)):
                name=on if jit else off
                body=bytes.fromhex((HERE/'fixtures'/(name+'.hex')).read_text())
                if hashlib.sha256(body).hexdigest()!=pins['guest_sha256'][name+'.PRG']:
                    raise RuntimeError('guest binary changed: '+name)
                arm=label+('-jit' if jit else '-interpreter')
                with tempfile.TemporaryDirectory(prefix='ARAnyM ARM64 '+arm+' ') as temporary:
                    work=Path(temporary)
                    if args.app:
                        app=work/'ARAnyM ARM JIT test.app'
                        shutil.copytree(args.app.resolve(strict=True),app,symlinks=True)
                        binary=app/'Contents/MacOS/aranym'
                        rom=app/'Contents/Resources/emutos-aranym.img'
                    else:
                        binary=args.emulator.resolve(strict=True);rom=args.firmware.resolve(strict=True)
                    program=work/'CHECK.PRG';program.write_bytes(body)
                    config=work/'config'
                    config.write_text(f'[GLOBAL]\nFastRAM = 64\nEmuTOS = {rom}\nBootstrap = {program}\nTOS =\nFloppy =\n'
                        f'[JIT]\nJIT = {"Yes" if jit else "No"}\nJITFPU = No\nJITCacheSize = 8192\nJITInline = No\n'
                        '[ETH0]\nType = none\n[STARTUP]\nGrabMouse = No\n[AUDIO]\nEnabled = No\n')
                    env={k:v for k,v in os.environ.items() if not k.startswith(('ARANYM_','DYLD_','SDL_'))}
                    env['ARANYM_ARM64_JIT_DIAGNOSTICS']='1'
                    if args.emulator: env.update(SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy')
                    log=output/(arm+'.txt')
                    record={'name':arm,'jit':jit,'guest_sha256':digest(program),'binary_sha256':digest(binary),
                            'firmware_sha256':digest(rom),'config_sha256':digest(config),'passed':False}
                    report['profiles'].append(record)
                    with log.open('w') as stream:
                        process=subprocess.Popen([str(binary),'-c',str(config)],cwd=work,env=env,stdout=stream,stderr=subprocess.STDOUT)
                        try: record['exit']=process.wait(timeout=90)
                        except subprocess.TimeoutExpired:
                            process.kill();process.wait();record['failure']='guest timed out'
                    text=log.read_text(errors='replace')
                    witness=re.search(r'ARM64-JIT executed-native=(\d+) executed-guest=(\d+) last-guest-pc=([0-9a-f]+)',text)
                    record['witness']=None if not witness else {'native':int(witness[1]),'guest':int(witness[2]),'last_pc':int(witness[3],16)}
                    record['marker_found']=marker in text
                    native_guest=record['witness'] and record['witness']['guest']>0
                    record['passed']=(record.get('exit')==status and record['marker_found'] and
                                      (native_guest if jit else not native_guest))
                    print(arm, 'PASS' if record['passed'] else 'FAIL',flush=True)
                    report_path.write_text(json.dumps(report,indent=2)+'\n')
                    if not record['passed']:
                        print(text[-12000:],flush=True)
                        raise RuntimeError(arm+' did not satisfy guest correctness and native-execution gates')
        report['passed']=True
    finally:
        report_path.write_text(json.dumps(report,indent=2)+'\n')

if __name__=='__main__': main()
