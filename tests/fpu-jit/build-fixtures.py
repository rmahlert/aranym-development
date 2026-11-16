#!/usr/bin/env python3
"""Build owned FPU JIT regression guests with exact source/compiler provenance."""
import argparse,hashlib,json,subprocess
from pathlib import Path
HERE=Path(__file__).resolve().parent
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--cc',default='m68k-atari-mint-gcc')
p.add_argument('--output',type=Path,required=True)
a=p.parse_args();out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
commands=[]
def run(command):
 commands.append([str(x) for x in command]);subprocess.run(commands[-1],check=True)
run([a.cc,'-m68040','-c',HERE/'guest/probe.S','-o',out/'probe.o'])
for name,c in [('FPU','probe'),('FPUBENCH','benchmark'),('FPUTRAP','traps')]:
 run([a.cc,'-m68000','-msoft-float','-O2','-Wall','-Wextra',HERE/('guest/'+c+'.c'),out/'probe.o','-o',out/(name+'.PRG')])
 body=(out/(name+'.PRG')).read_bytes()
 (out/(name+'.hex')).write_text(body.hex()+'\n')
sha=lambda x:hashlib.sha256(x.read_bytes()).hexdigest()
record={'compiler':subprocess.check_output([a.cc,'--version'],text=True).splitlines()[0],
 'commands':commands,'source_sha256':{str(x.relative_to(HERE)):sha(x) for x in sorted((HERE/'guest').iterdir())},
 'guest_sha256':{x.name:sha(x) for x in out.glob('*.PRG')}}
(out/'provenance.json').write_text(json.dumps(record,indent=2)+'\n')
print(out)
