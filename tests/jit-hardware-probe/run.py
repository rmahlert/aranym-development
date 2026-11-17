#!/usr/bin/env python3
"""Check instruction-PC and register recovery from an absent hardware read."""
import argparse,hashlib,json,os,re,shlex,subprocess,tempfile
from pathlib import Path
HERE=Path(__file__).resolve().parent
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
def main():
 p=argparse.ArgumentParser(description=__doc__)
 p.add_argument('--emulator',required=True,type=Path)
 p.add_argument('--firmware',required=True,type=Path)
 p.add_argument('--output',required=True,type=Path)
 p.add_argument('--runner',default='',help='Optional emulator prefix, for example qemu-arm -L SYSROOT')
 p.add_argument('--native-nopless',action='store_true',help='Also require precise cached faults without NOPs; known to fail on x86-64 and ARM32')
 p.add_argument('--strict-frame',action='store_true',help='Also require full effective address and supervisor function code; older classic signal handlers fail this check')
 a=p.parse_args();binary=a.emulator.resolve(strict=True);rom=a.firmware.resolve(strict=True)
 out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
 pins=json.loads((HERE/'fixtures/provenance.json').read_text())
 for name,digest in pins['source_sha256'].items():
  if sha(HERE/name)!=digest:raise RuntimeError('fixture source mismatch: '+name)
 profiles=[('interpreter','NOPLESS',False,8192),('cache0-nopless','NOPLESS',True,0),
           ('cache0-nop','WITHNOP',True,0),('cached-nop','WITHNOP',True,8192)]
 if a.native_nopless:profiles.append(('cached-nopless','NOPLESS',True,8192))
 report={'binary_sha256':sha(binary),'firmware_sha256':sha(rom),'profiles':[],
         'native_nopless_qualified':False,'full_frame_qualified':False,
         'limitations':['The default profiles do not qualify cached native faults without NOP boundaries.',
                         'The default profiles record, but do not qualify, full effective-address and function-code fields.']}
 for label,fixture,jit,cache in profiles:
  body=bytes.fromhex((HERE/('fixtures/'+fixture+'.hex')).read_text())
  if hashlib.sha256(body).hexdigest()!=pins['guest_sha256'][fixture+'.PRG']:raise RuntimeError('guest hash mismatch')
  with tempfile.TemporaryDirectory(prefix='aranym-hardware-probe-') as d:
   work=Path(d);guest=work/'PROBE.PRG';guest.write_bytes(body);config=work/'config'
   config.write_text(f'[GLOBAL]\nFastRAM = 64\nFixedMemoryOffset = 60000000\nEmuTOS = {rom.as_posix()}\nBootstrap = {guest.as_posix()}\nTOS =\nFloppy =\n'
      f'[JIT]\nJIT = {"Yes" if jit else "No"}\nJITFPU = No\nJITSafe = Yes\nJITCacheSize = {cache}\n'
      '[ETH0]\nType = none\n[AUDIO]\nEnabled = No\n[STARTUP]\nGrabMouse = No\n')
   env={k:v for k,v in os.environ.items() if k!='HOME' and not k.startswith(('SDL_','ARANYM_'))}
   env.update(SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy',ARANYM_UPSTREAM_JIT_DIAGNOSTICS='1',ARANYM_ARM32_JIT_DIAGNOSTICS='1',ARANYM_ARM64_JIT_DIAGNOSTICS='1')
   r=subprocess.run([*shlex.split(a.runner),str(binary),'-c',str(config)],cwd=work,env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=45)
   text=r.stdout.decode(errors='replace');(out/(label+'.log')).write_text(text)
   details=[l for l in text.splitlines() if l.startswith('HARDWARE PROBE ')]
   native=re.search(r'executed-guest=(\d+)',text)
   passed=r.returncode==0 and len(details)==2 and all(line.endswith(' PASS') for line in details)
   frames=[re.search(r'ssw=0105 address=([0-9a-f]+) expectedaddress=([0-9a-f]+) ',line) for line in details]
   full_frame=len(frames)==2 and all(frame is not None and frame[1]==frame[2] for frame in frames)
   if a.strict_frame:passed=passed and full_frame
   if label=='cached-nop':passed=passed and native is not None and int(native[1])>0
   report['profiles'].append({'name':label,'exit':r.returncode,'detail':details,'full_frame':full_frame,'passed':passed})
   print(label,'PASS' if passed else 'FAIL',flush=True)
 report['passed']=all(r['passed'] for r in report['profiles'])
 report['native_nopless_qualified']=a.native_nopless and report['passed']
 report['full_frame_qualified']=a.strict_frame and report['passed']
 (out/'result.json').write_text(json.dumps(report,indent=2)+'\n')
 return 0 if report['passed'] else 1
if __name__=='__main__':raise SystemExit(main())
