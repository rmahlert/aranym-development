#!/usr/bin/env python3
"""Build the independently counted register-only FPU benchmark guest."""
import argparse,hashlib,json,subprocess
from pathlib import Path
HERE=Path(__file__).resolve().parent
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--cc',default='m68k-atari-mint-gcc');p.add_argument('--output',type=Path,required=True)
a=p.parse_args();out=a.output.resolve();out.mkdir(parents=True,exist_ok=True);commands=[]
def run(args):commands.append([str(x) for x in args]);subprocess.run(commands[-1],check=True)
run([a.cc,'-m68040','-c',HERE/'guest/guard-benchmark.S','-o',out/'guard-benchmark.o'])
run([a.cc,'-m68000','-msoft-float','-O2','-Wall','-Wextra',HERE/'guest/guard-benchmark.c',out/'guard-benchmark.o','-o',out/'GUARDBEN.PRG'])
(out/'GUARDBEN.hex').write_text((out/'GUARDBEN.PRG').read_bytes().hex()+'\n')
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
(out/'guard-benchmark-provenance.json').write_text(json.dumps({'compiler':subprocess.check_output([a.cc,'--version'],text=True).splitlines()[0],'commands':commands,'source_sha256':{str(p.relative_to(HERE)):sha(p) for p in [HERE/'guest/guard-benchmark.c',HERE/'guest/guard-benchmark.S']},'guest_sha256':{'GUARDBEN.PRG':sha(out/'GUARDBEN.PRG')}},indent=2)+'\n')
