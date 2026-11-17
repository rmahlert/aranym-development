#!/usr/bin/env python3
import argparse,hashlib,json,os,platform,re,shutil,statistics,subprocess,sys,tempfile,threading,time
from pathlib import Path
HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[1]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
 p=argparse.ArgumentParser()
 target=p.add_mutually_exclusive_group(required=True)
 target.add_argument('--emulator',type=Path)
 target.add_argument('--app',type=Path)
 p.add_argument('--firmware',type=Path)
 p.add_argument('--output',required=True,type=Path)
 p.add_argument('--architecture',choices=['arm64','arm32'],default='arm64')
 p.add_argument('--qemu',type=Path)
 p.add_argument('--qemu-sysroot',type=Path)
 p.add_argument('--qemu-library-path')
 p.add_argument('--benchmark',action='store_true')
 p.add_argument('--c-guard-reference',action='store_true',help='Use the previous ARM C operand guard for paired optimization measurements')
 a=p.parse_args()
 if a.qemu and not all([a.emulator,a.qemu_sysroot,a.qemu_library_path]):p.error('QEMU requires emulator,sysroot and target libraries')
 if not a.qemu:
  allowed=['aarch64','arm64'] if a.architecture=='arm64' else ['armv6l','armv7l','armv8l']
  if platform.machine().lower() not in allowed:p.error('matching native ARM host required,or explicit QEMU')
  if a.app and sys.platform!='darwin':p.error('app requires native Apple Silicon')
 if a.app:
  binary=(a.app/'Contents/MacOS/aranym').resolve(strict=True)
  rom=(a.app/'Contents/Resources/emutos-aranym.img').resolve(strict=True)
 else:
  if not a.firmware:p.error('emulator requires firmware')
  binary=a.emulator.resolve(strict=True);rom=a.firmware.resolve(strict=True)
 out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
 pins=json.loads((HERE/'fixtures/provenance.json').read_text())
 for name,digest in pins['source_sha256'].items():assert sha(HERE/name)==digest,('source',name)
 report={'platform':sys.platform,'host_arch':platform.machine(),'execution_arch':a.architecture,
   'native':not bool(a.qemu),'cross_emulated':bool(a.qemu),'binary_sha256':sha(binary),
   'firmware_sha256':sha(rom),'precision':'existing ARM IEEE64 profile','guard':'c-reference' if a.c_guard_reference else 'native-default','profiles':[],'passed':False}
 prefix=[]
 if a.qemu:prefix=[str(a.qemu.resolve(strict=True)),'-L',str(a.qemu_sysroot.resolve(strict=True)),
                   '-U','HOME','-E','LD_LIBRARY_PATH='+a.qemu_library_path]
 def run(label,fixture,jit,fpu,diagnostics=True):
  body=bytes.fromhex((HERE/'fixtures'/str(fixture+'.hex')).read_text())
  assert hashlib.sha256(body).hexdigest()==pins['guest_sha256'][fixture+'.PRG']
  with tempfile.TemporaryDirectory(prefix='ARAnyM ARM FP '+label+' ') as temporary:
   work=Path(temporary);guest=work/'CHECK.PRG';guest.write_bytes(body);config=work/'config'
   run_binary=binary;run_rom=rom
   if a.app:
    relocated=work/'ARAnyM FPU test.app';shutil.copytree(a.app.resolve(),relocated,symlinks=True)
    run_binary=relocated/'Contents/MacOS/aranym';run_rom=relocated/'Contents/Resources/emutos-aranym.img'
   config.write_text(f'[GLOBAL]\nFastRAM = 64\nEmuTOS = {run_rom}\nBootstrap = {guest}\nTOS =\nFloppy =\n'
    f'[JIT]\nJIT = {"Yes" if jit else "No"}\nJITSafe = Yes\nJITFPU = {"Yes" if fpu else "No"}\nJITCacheSize = 8192\nJITInline = No\nJITLazyFlush = 0\n'
    '[ETH0]\nType = none\n[STARTUP]\nGrabMouse = No\n[AUDIO]\nEnabled = No\n')
   env={k:v for k,v in os.environ.items() if k!='HOME' and not k.startswith(('ARANYM_','SDL_','DYLD_'))}
   if a.emulator:env.update(SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy')
   if diagnostics:env['ARANYM_ARM_FPU_DIAGNOSTICS']='1'
   if a.c_guard_reference:env['ARANYM_ARM_FPU_C_GUARD']='1'
   command=prefix+[str(run_binary),'-c',str(config)]
   elapsed=None
   if fixture=='FPUBENCH':
    process=subprocess.Popen(command,cwd=work,env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
    lines=[];times={}
    def capture():
     for line in process.stdout:
      if b'FPU-BENCH BEGIN' in line:times['begin']=time.monotonic()
      if b'FPU-BENCH ticks=' in line:times['end']=time.monotonic()
      lines.append(line)
    reader=threading.Thread(target=capture,daemon=True);reader.start()
    try:status=process.wait(timeout=180)
    except subprocess.TimeoutExpired:
     process.kill();process.wait();reader.join(timeout=3);raise
    reader.join(timeout=3);text=b''.join(lines).decode(errors='replace')
    assert 'begin' in times and 'end' in times,(label,'missing timed guest markers')
    elapsed=times['end']-times['begin']
   else:
    result=subprocess.run(command,cwd=work,env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=180)
    status=result.returncode;text=result.stdout.decode(errors='replace')
   (out/(label+'.log')).write_text(text)
   witness=re.search(r'ARM-FPU-JIT available=yes enabled=(yes|no) executed-native=(\d+) deoptimized=(\d+) precision=ieee64',text)
   record={'label':label,'jit':jit,'fpu_jit':fpu,'exit':status,'diagnostics':diagnostics,
      'guest_sha256':hashlib.sha256(body).hexdigest(),'config_sha256':sha(config),
      'executed_native_fpu':int(witness[2]) if witness else 0,'deoptimized':int(witness[3]) if witness else 0}
   if elapsed is not None:record['host_seconds']=elapsed
   report['profiles'].append(record)
   marker='ARM FPU JIT GUEST PASS' if fixture=='FPU' else 'FPU-BENCH ticks='
   assert status==0 and marker in text,(label,text[-7000:])
   if diagnostics:
    assert witness,(label,'missing compiled FPU capability')
    assert bool(record['executed_native_fpu'])==(jit and fpu),(label,'native witness mismatch')
   record['passed']=True;print(label,'PASS',flush=True)
   return text,record
 try:
  previous=None
  for label,jit,fpu in [('interpreter',False,False),('integer-jit',True,False),('fpu-jit',True,True)]:
   text,record=run('corpus-'+label,'FPU',jit,fpu)
   values={}
   for mode,case,words in re.findall(r'FPU-DATA mode=(\w+) case=(\w+) words=(\w+)',text):
    key=int(mode,16),int(case,16);assert key not in values
    values[key]=[int(words[i:i+8],16) for i in range(0,len(words),8)]
   assert len(values)==266 and len(re.findall('difference=00000000',text))==266
   normal=values[0,0]
   golden=[(0x40020000,0x80000000,0),(0x40010000,0xa0000000,0),
           (0x40030000,0xa0000000,0),(0x40010000,0x80000000,0),(0x40000000,0xc0000000,0)]
   for i,want in enumerate(golden):assert tuple(normal[i*5:i*5+3])==want,(label,'independent arithmetic',i)
   for i in range(37):assert normal[i*5+4]==0x14,(label,'CCR',i)
   assert normal[37*5]&0x00ffffff==0 and normal[37*5+1]&0x00ffffff==0,(label,'compare/test status not reset')
   assert normal[-1]==0x12345678,(label,'FPIAR changed')
   if previous is not None:assert values==previous,(label,'register/status outputs differ from interpreter')
   previous=values;record['exact_profiles']=266
  if a.benchmark:
   text,witness=run('benchmark-witness','FPUBENCH',True,True)
   assert witness['executed_native_fpu']>=40000000,'benchmark lacks native FP execution'
   samples={};host_samples={}
   for label,fpu in [('integer-jit',False),('fpu-jit',True)]:
    samples[label]=[];host_samples[label]=[]
    for repeat in range(3):
     text,record=run('timing-'+label+'-'+str(repeat),'FPUBENCH',True,fpu,False)
     m=re.search(r'FPU-BENCH ticks=(\w+) result=(\w+)',text);assert m and int(m[2],16)==3
     assert 'iterations=00989680' in text,(label,'counter-free iteration count mismatch')
     record['ticks_200hz']=int(m[1],16);samples[label].append(record['ticks_200hz']);host_samples[label].append(record['host_seconds'])
   medians={k:statistics.median(v) for k,v in host_samples.items()};assert all(x>0 for v in host_samples.values() for x in v)
   report['benchmark']={'samples_ticks_200hz':samples,'samples_host_seconds':host_samples,'medians_host_seconds':medians,
      'integer_jit_over_fpu_jit':medians['integer-jit']/medians['fpu-jit'],
      'scope':'Host monotonic time between guest BEGIN/result markers; ten million independently counted iterations; counters disabled. Guest200Hz ticks retained as diagnostics only; no whole-system claim.'}
   print(json.dumps(report['benchmark']),flush=True)
  report['passed']=True
 finally:(out/'arm-fpu-results.json').write_text(json.dumps(report,indent=2)+'\n')
if __name__=='__main__':main()
