#!/usr/bin/env python3
"""Run identical owned 68k correctness guests with ARM JIT on and off.
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
    parser.add_argument('--architecture', choices=('arm64','arm32'), default='arm64')
    parser.add_argument('--qemu',type=Path,help='explicitly marked cross-emulated debug execution only')
    parser.add_argument('--qemu-sysroot',type=Path)
    parser.add_argument('--qemu-library-path')
    parser.add_argument('--fpu-jit',action='store_true')
    args=parser.parse_args()
    if args.qemu and (not args.emulator or not args.qemu_sysroot or not args.qemu_library_path):
        parser.error('--qemu needs standalone emulator, sysroot and target library path')
    witness_arch='ARM32' if args.architecture=='arm32' else 'ARM64'
    prefix=[]
    if args.qemu:
        prefix=[str(args.qemu.resolve(strict=True)),'-L',str(args.qemu_sysroot.resolve(strict=True)),
                '-U','HOME','-E','LD_LIBRARY_PATH='+args.qemu_library_path]
    if args.app:
        if sys.platform!='darwin' or platform.machine()!='arm64': parser.error('native Apple Silicon required')
    elif not args.qemu and (sys.platform!='linux' or platform.machine() not in
            (('armv7l','armv8l','armv6l') if args.architecture=='arm32' else ('aarch64','arm64'))):
        parser.error('matching native Linux ARM userspace required, or explicitly use --qemu for debug')
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
    report={'host_arch':platform.machine(),'execution_arch':args.architecture,'native':not bool(args.qemu),
            'cross_emulated':bool(args.qemu),'platform':sys.platform,'fpu_jit':args.fpu_jit,'profiles':[], 'passed':False}
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
                        f'[JIT]\nJIT = {"Yes" if jit else "No"}\nJITFPU = {"Yes" if args.fpu_jit else "No"}\nJITCacheSize = 8192\nJITInline = No\n'
                        '[ETH0]\nType = none\n[STARTUP]\nGrabMouse = No\n[AUDIO]\nEnabled = No\n')
                    env={k:v for k,v in os.environ.items() if not k.startswith(('ARANYM_','DYLD_','SDL_'))}
                    env['ARANYM_'+witness_arch+'_JIT_DIAGNOSTICS']='1'
                    if args.emulator: env.update(SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy')
                    log=output/(arm+'.txt')
                    record={'name':arm,'jit':jit,'guest_sha256':digest(program),'binary_sha256':digest(binary),
                            'firmware_sha256':digest(rom),'config_sha256':digest(config),'passed':False}
                    report['profiles'].append(record)
                    with log.open('w') as stream:
                        process=subprocess.Popen(prefix+[str(binary),'-c',str(config)],cwd=work,env=env,stdout=stream,stderr=subprocess.STDOUT)
                        try: record['exit']=process.wait(timeout=90)
                        except subprocess.TimeoutExpired:
                            process.kill();process.wait();record['failure']='guest timed out'
                    text=log.read_text(errors='replace')
                    witness=re.search(witness_arch+r'-JIT executed-native=(\d+) executed-guest=(\d+) last-guest-pc=([0-9a-f]+)',text)
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
