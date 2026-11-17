#!/usr/bin/env python3
"""Build the independent finite-comparison FPU regression guest."""
import argparse,hashlib,json,subprocess
from pathlib import Path
HERE=Path(__file__).resolve().parent
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--cc',default='m68k-atari-mint-gcc');p.add_argument('--output',type=Path,required=True)
a=p.parse_args();out=a.output.resolve();out.mkdir(parents=True,exist_ok=True);commands=[]
def run(args):commands.append([str(x) for x in args]);subprocess.run(commands[-1],check=True)
run([a.cc,'-m68040','-c',HERE/'guest/compare.S','-o',out/'compare.o'])
run([a.cc,'-m68000','-msoft-float','-O2','-Wall','-Wextra',HERE/'guest/compare.c',out/'compare.o','-o',out/'FPUCOMP.PRG'])
(out/'FPUCOMP.hex').write_text((out/'FPUCOMP.PRG').read_bytes().hex()+'\n')
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
(out/'compare-provenance.json').write_text(json.dumps({'compiler':subprocess.check_output([a.cc,'--version'],text=True).splitlines()[0],'commands':commands,'source_sha256':{str(p.relative_to(HERE)):sha(p) for p in [HERE/'guest/compare.c',HERE/'guest/compare.S']},'guest_sha256':{'FPUCOMP.PRG':sha(out/'FPUCOMP.PRG')}},indent=2)+'\n')
