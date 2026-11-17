#!/usr/bin/env python3
"""Build the absent-SFP004 read-fault probes with compiler provenance."""
import argparse,hashlib,json,subprocess
from pathlib import Path
HERE=Path(__file__).resolve().parent
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--cc',default='m68k-atari-mint-gcc')
p.add_argument('--output',required=True,type=Path)
a=p.parse_args();out=a.output.resolve();out.mkdir(parents=True,exist_ok=True)
commands=[]
def run(args):
 commands.append([str(x) for x in args]);subprocess.run(commands[-1],check=True)
for name,flags in [('NOPLESS',[]),('WITHNOP',['-DWITH_NOP=1'])]:
 run([a.cc,'-m68040',*flags,'-c',HERE/'guest/probe.S','-o',out/(name+'.o')])
 run([a.cc,'-m68000','-msoft-float','-O2',HERE/'guest/probe.c',out/(name+'.o'),'-o',out/(name+'.PRG')])
 (out/(name+'.hex')).write_text((out/(name+'.PRG')).read_bytes().hex()+'\n')
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
record={'compiler':subprocess.check_output([a.cc,'--version'],text=True).splitlines()[0],
 'commands':commands,'source_sha256':{str(p.relative_to(HERE)):sha(p) for p in sorted((HERE/'guest').iterdir())},
 'guest_sha256':{name+'.PRG':sha(out/(name+'.PRG')) for name in ['NOPLESS','WITHNOP']}}
(out/'provenance.json').write_text(json.dumps(record,indent=2)+'\n')
