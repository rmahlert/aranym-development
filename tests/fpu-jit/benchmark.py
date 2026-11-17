#!/usr/bin/env python3
"""Time a counted register-arithmetic guest loop with native execution proof."""
import argparse,hashlib,json,os,platform,re,statistics,subprocess,sys,tempfile,threading,time
from pathlib import Path
HERE=Path(__file__).resolve().parent
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
def main():
 p=argparse.ArgumentParser(description=__doc__)
 p.add_argument('--emulator',required=True,type=Path);p.add_argument('--firmware',required=True,type=Path);p.add_argument('--output',required=True,type=Path)
 p.add_argument('--compare-with',type=Path,help='Optional previous x86-64 emulator for a paired native comparison')
 p.add_argument('--samples',type=int,default=3);a=p.parse_args()
 if sys.platform not in ('linux','win32','darwin') or platform.machine().lower() not in ('x86_64','amd64'):p.error('native x86-64 host required')
 if not 1<=a.samples<=9:p.error('samples must be1..9')
 binary=a.emulator.resolve(strict=True);rom=a.firmware.resolve(strict=True);out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
 pins=json.loads((HERE/'fixtures/guard-benchmark-provenance.json').read_text())
 for name,digest in pins['source_sha256'].items():
  if sha(HERE/name)!=digest:raise RuntimeError('counted benchmark source changed: '+name)
 body=bytes.fromhex((HERE/'fixtures/GUARDBEN.hex').read_text())
 if hashlib.sha256(body).hexdigest()!=pins['guest_sha256']['GUARDBEN.PRG']:raise RuntimeError('counted guest hash mismatch')
 report={'platform':sys.platform,'architecture':platform.machine(),'native':True,'binary_sha256':sha(binary),'firmware_sha256':sha(rom),'guest_sha256':hashlib.sha256(body).hexdigest(),'iterations':10000000,'profiles':[],'passed':False,'scope':'One register-arithmetic guest loop with independent FP iteration count; not a whole-system or desktop benchmark.'}
 def run(label,emulator,fpu,reference,diagnostics):
  with tempfile.TemporaryDirectory(prefix='aranym-counted-fpu-') as directory:
   work=Path(directory);guest=work/'CHECK.PRG';guest.write_bytes(body)
   if sys.platform=='darwin':(work/'Aranym_files').mkdir()
   config=work/'config';config.write_text(f'[GLOBAL]\nFastRAM = 64\nFixedMemoryOffset = 60000000\nEmuTOS = {rom.as_posix()}\nBootstrap = {guest.as_posix()}\nTOS =\nFloppy =\n[JIT]\nJIT = Yes\nJITSafe = Yes\nJITFPU = {"Yes" if fpu else "No"}\nJITCacheSize = 8192\nJITLazyFlush = 0\nJITInline = No\nJITUpdatePC = Yes\n[ETH0]\nType = none\n[AUDIO]\nEnabled = No\n[STARTUP]\nGrabMouse = No\n')
   env={k:v for k,v in os.environ.items() if k!='HOME' and not k.startswith(('ARANYM_','SDL_'))};env.update(SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy')
   if reference:env['ARANYM_FPU_SOFTFLOAT_REFERENCE']='1'
   if diagnostics:env['ARANYM_UPSTREAM_JIT_DIAGNOSTICS']='1'
   process=subprocess.Popen([str(emulator),'-c',str(config)],cwd=work,env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,errors='replace',bufsize=1)
   deadline=threading.Timer(150,process.kill);deadline.start();lines=[];stamps={}
   try:
    for line in process.stdout:
     now=time.monotonic_ns();lines.append(line)
     if 'FPU GUARDBENCH BEGIN' in line:
      if 'begin' in stamps:raise RuntimeError('duplicate benchmark begin')
      stamps['begin']=now
     if 'FPU GUARDBENCH PASS result=3 iterations=10000000' in line:
      if 'end' in stamps:raise RuntimeError('duplicate benchmark result')
      stamps['end']=now
    status=process.wait(timeout=5)
   finally:
    deadline.cancel()
    if process.poll() is None:process.kill();process.wait()
   text=''.join(lines);(out/(label+'.log')).write_text(text)
   native=re.search(r'FPU-JIT executed-guest=(\d+)',text)
   record={'label':label,'binary_sha256':sha(emulator),'fpu_jit':fpu,'softfloat_reference':reference,'diagnostics':diagnostics,'exit':status,'executed_native_fpu':int(native[1]) if native else 0,'seconds':(stamps['end']-stamps['begin'])/1e9 if len(stamps)==2 else None}
   report['profiles'].append(record)
   assert status==0 and record['seconds'] is not None and record['seconds']>0,(label,text[-4000:])
   if diagnostics:assert record['executed_native_fpu']>=49000000,'counted loop did not execute native FP'
   record['passed']=True;print(label,'PASS',record['seconds'],flush=True);return record['seconds']
 try:
  run('native-witness',binary,True,True,True)
  profiles=[('classic-ieee',binary,False,False),('softfloat',binary,False,True),('native-fpu',binary,True,True)]
  if a.compare_with:profiles.insert(2,('previous-native',a.compare_with.resolve(strict=True),True,True))
  samples={label:[] for label,*_ in profiles}
  for repeat in range(a.samples):
   for label,emulator,fpu,reference in profiles:samples[label].append(run(label+'-'+str(repeat),emulator,fpu,reference,False))
  medians={label:statistics.median(values) for label,values in samples.items()};report['benchmark']={'samples_seconds':samples,'medians_seconds':medians,'softfloat_over_native':medians['softfloat']/medians['native-fpu'],'classic_ieee_over_native':medians['classic-ieee']/medians['native-fpu']}
  if a.compare_with:report['benchmark']['previous_native_over_current']=medians['previous-native']/medians['native-fpu']
  report['passed']=True;print(json.dumps(report['benchmark']),flush=True)
 finally:(out/'counted-fpu-benchmark.json').write_text(json.dumps(report,indent=2)+'\n')
if __name__=='__main__':main()
