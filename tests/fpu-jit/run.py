#!/usr/bin/env python3
"""Qualify actual x86-64 FPU translation against the pinned SoftFloat frontend."""
import argparse,hashlib,json,os,platform,re,statistics,subprocess,sys,tempfile,time
from pathlib import Path
HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[1]
def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def main():
 p=argparse.ArgumentParser(description=__doc__)
 p.add_argument('--emulator',required=True,type=Path)
 p.add_argument('--firmware',required=True,type=Path)
 p.add_argument('--output',required=True,type=Path)
 p.add_argument('--benchmark',action='store_true')
 p.add_argument('--cpu-regression',action='store_true')
 a=p.parse_args()
 if sys.platform not in ('linux','win32','darwin') or platform.machine().lower() not in ('x86_64','amd64'):
  p.error('native Linux, Windows or Intel macOS x86-64 is required')
 binary=a.emulator.resolve(strict=True);rom=a.firmware.resolve(strict=True)
 out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
 pins=json.loads((HERE/'fixtures/provenance.json').read_text())
 for name,digest in pins['source_sha256'].items():
  if sha(HERE/name)!=digest:raise RuntimeError('guest source changed: '+name)
 report={'platform':sys.platform,'architecture':platform.machine(),'native':True,
         'binary_sha256':sha(binary),'firmware_sha256':sha(rom),'profiles':[],'passed':False,
         'limitations':['Enabled FPU arithmetic exception/FPSP continuation remains explicitly unsupported.']}
 def run(label,fixture,fpu,reference=True,diagnostics=True,expected=0,marker='FPU JIT GUEST PASS'):
  fixture_dir=HERE/'fixtures';fixture_pins=pins
  if fixture=='FPUCOMP':
   fixture_pins=json.loads((fixture_dir/'compare-provenance.json').read_text())
   for name,digest in fixture_pins['source_sha256'].items():
    if sha(HERE/name)!=digest:raise RuntimeError('comparison guest source changed: '+name)
  if not (fixture_dir/(fixture+'.hex')).is_file():
   fixture_dir=ROOT/'tests/arm64-guest/fixtures'
   fixture_pins=json.loads((fixture_dir/'provenance.json').read_text())
   for name,digest in fixture_pins['source_sha256'].items():
    if sha(ROOT/name)!=digest:raise RuntimeError('CPU guest source mismatch: '+name)
  body=bytes.fromhex((fixture_dir/(fixture+'.hex')).read_text())
  if hashlib.sha256(body).hexdigest()!=fixture_pins['guest_sha256'][fixture+'.PRG']:raise RuntimeError('guest hash mismatch')
  with tempfile.TemporaryDirectory(prefix='ARAnyM FPU '+label+' ') as directory:
   work=Path(directory);program=work/'CHECK.PRG';program.write_bytes(body)
   if sys.platform=='darwin':(work/'Aranym_files').mkdir()
   config=work/'config'
   config.write_text(f'[GLOBAL]\nFastRAM = 64\nFixedMemoryOffset = 60000000\nEmuTOS = {rom.as_posix()}\nBootstrap = {program.as_posix()}\nTOS =\nFloppy =\n'
     f'[JIT]\nJIT = Yes\nJITSafe = Yes\nJITFPU = {"Yes" if fpu else "No"}\nJITInline = No\nJITLazyFlush = 0\nJITUpdatePC = Yes\n'
     '[ETH0]\nType = none\n[STARTUP]\nGrabMouse = No\n[AUDIO]\nEnabled = No\n')
   env={k:v for k,v in os.environ.items() if k!='HOME' and not k.startswith(('ARANYM_','SDL_'))}
   env.update(SDL_VIDEODRIVER='dummy',SDL_AUDIODRIVER='dummy')
   if reference:env['ARANYM_FPU_SOFTFLOAT_REFERENCE']='1'
   if diagnostics:env['ARANYM_UPSTREAM_JIT_DIAGNOSTICS']='1'
   start=time.monotonic()
   process=subprocess.run([str(binary),'-c',str(config)],cwd=work,env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=90)
   text=process.stdout.decode(errors='replace');(out/(label+'.log')).write_text(text)
   witness=re.search(r'FPU-JIT executed-guest=(\d+)',text)
   record={'label':label,'fpu_jit':fpu,'softfloat_reference':reference,'diagnostics':diagnostics,
           'status':process.returncode,'seconds_including_boot':time.monotonic()-start,
           'guest_sha256':hashlib.sha256(body).hexdigest(),'config_sha256':sha(config),
           'executed_native_guest_fpu':int(witness[1]) if witness else 0}
   report['profiles'].append(record)
   assert process.returncode==expected and marker in text,(label,text[-5000:])
   if reference:assert 'frontend=pinned-softfloat' in text,(label,'wrong reference frontend')
   if fpu:assert 'MODERN040_FPU_JIT available=yes enabled=yes' in text,(label,'FPU JIT disabled')
   record['passed']=True
   print(label,'PASS',flush=True)
   return text,record
 try:
  corpora=[]
  for enabled in (False,True):
   text,record=run('corpus-'+('jit' if enabled else 'reference'),'FPU',enabled)
   assert bool(record['executed_native_guest_fpu'])==enabled,'FPU translation witness mismatch'
   values={}
   for mode,case,words in re.findall(r'FPU-DATA mode=(\w+) case=(\w+) words=(\w+)',text):
    key=(int(mode,16),int(case,16));assert key not in values
    values[key]=[int(words[i:i+8],16) for i in range(0,len(words),8)]
   assert len(values)==208,'missing arithmetic/ROM modes'
   assert len(re.findall('difference=00000000',text))==208,'cold/hot result changed'
   # Independent exact-value checks, in addition to the full software oracle.
   normal=values[0,0]
   golden=[(0x40020000,0x80000000,0),(0x40010000,0xa0000000,0),
           (0x40030000,0xa0000000,0),(0x40010000,0x80000000,0),(0x40000000,0xc0000000,0)]
   for i,expected in enumerate(golden):assert tuple(normal[i*5:i*5+3])==expected,('exact arithmetic',i)
   for i in range(21):assert normal[i*5+4]==0x14,('integer CCR changed',i)
   assert values[0,0x10000][:3]==[0x40000000,0xc90fdaa2,0x2168c235],'PI ROM value'
   corpora.append(values)
   record['arithmetic_cases']=192;record['constant_rom_cases']=16
  assert corpora[0]==corpora[1],'native FPU register/status data differs from SoftFloat'
  report['bit_exact_profiles']=208
  comparisons=[]
  for enabled in (False,True):
   text,record=run('compare-'+('jit' if enabled else 'reference'),'FPUCOMP',enabled,marker='FPU COMPARE PASS')
   assert bool(record['executed_native_guest_fpu'])==enabled,'comparison native execution witness mismatch'
   values={}
   for mode,case,words in re.findall(r'FPU-COMP mode=(\w+) case=(\w+) words=(\w+)',text):
    key=(int(mode,16),int(case,16));assert key not in values and len(words)==72
    values[key]=[int(words[i:i+8],16) for i in range(0,72,8)]
   assert len(values)==768,'missing FCMP modes, operands or integer CCR profiles'
   for (mode,case),words in values.items():
    relation=[0,0x08000000,0x08000000,0x04000000,0,0][case//32]
    assert words[0]==(relation|0x005a00f8),'FCMP changed quotient/accrued bits or raised arithmetic status'
    assert words[2]==case%32,'FCMP changed integer CCR'
   comparisons.append(values);record['comparison_profiles']=len(values)
  assert comparisons[0]==comparisons[1],'native FCMP register/status data differs from SoftFloat'
  report['comparison_profiles']=768
  # Preserve the pre-existing explicit capability boundary. This is a known
  # unsupported case, not evidence of delivered guest FPU traps.
  run('unsupported-enabled-trap','FPUTRAP',True,expected=3,
      marker='floating-point arithmetic exception profile not integrated')
  if a.cpu_regression:
   for fixture,marker in [('MASTER','CPU BRIDGE PASS all checks'),('MVBL','MASTER IRQ PASS'),
                          ('MMFP','MASTER IRQ PASS'),('INVALID','JIT INVALIDATION GUEST PASS'),
                          ('TRACE','JIT TRACE GUEST PASS')]:
    run('cpu-'+fixture,fixture,True,marker=marker)
  if a.benchmark:
   # Prove this exact benchmark executes native FP, then time without counters.
   text,witness=run('benchmark-witness','FPUBENCH',True,marker='FPU-BENCH ticks=')
   assert witness['executed_native_guest_fpu']>=3000000,'benchmark did not run native FP'
   samples={}
   for label,fpu,reference in [('classic-ieee',False,False),('softfloat',False,True),('native-fpu',True,True)]:
    samples[label]=[]
    for repeat in range(3):
     text,record=run('timing-'+label+'-'+str(repeat),'FPUBENCH',fpu,reference,False,marker='FPU-BENCH ticks=')
     match=re.search(r'FPU-BENCH ticks=(\w+) result=(\w+)',text)
     assert match and int(match[2],16)==3,'benchmark result differs'
     record['guest_ticks_200hz']=int(match[1],16);samples[label].append(record['guest_ticks_200hz'])
   medians={k:statistics.median(v) for k,v in samples.items()}
   assert medians['native-fpu']>0,'timer resolution insufficient'
   report['benchmark']={'samples_ticks_200hz':samples,'medians_ticks_200hz':medians,
       'softfloat_over_native':medians['softfloat']/medians['native-fpu'],
       'classic_ieee_over_native':medians['classic-ieee']/medians['native-fpu'],
       'scope':'One exact arithmetic microbenchmark; not a whole-system speed claim. Counters disabled for timing.'}
   print(json.dumps(report['benchmark']),flush=True)
  report['passed']=True
 finally:(out/'fpu-jit-results.json').write_text(json.dumps(report,indent=2)+'\n')
if __name__=='__main__':main()
