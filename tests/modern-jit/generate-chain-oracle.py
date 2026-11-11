#!/usr/bin/env python3
"""Independent host-integer expectations for live/dead sticky-Z operation chains.
SPDX-License-Identifier: GPL-2.0-or-later
"""
from pathlib import Path
import hashlib,json,runpy
r=Path(__file__).resolve().parent
oracle=runpy.run_path(str(r/'generate-alu-oracle.py'))['oracle']
rows=[]
for family,operation in enumerate([0,1,6]):
 for dead in range(2):
  for size,width in enumerate([8,16,32]):
   mask=(1<<width)-1;sign=1<<(width-1)
   edges=[0,1,2,sign-1,sign,sign+1,mask-1,mask]
   for src in edges[:1] if operation==6 else edges:
    for dst in edges:
     for ccr in range(32):
      flags=ccr;value=dst
      for step in range(4):
       fullsrc,fulldst,result,flags,*_=oracle(operation,width,src,value,flags)
       value=result&mask
      # TST.L of positive D2 overwrites NZVC, leaving only final X.
      if dead:flags&=16
      rows.append((fullsrc,(0xfedcba98&~mask)|dst,result,flags,family*2+dead,size,ccr))
assert len(rows)==26112
text='/* Generated with independent host integer math, four operations per case. */\n'
text+='struct alu_case { unsigned long src,dst,result; unsigned short flags,op,size,ccr; };\n'
text+='static const struct alu_case alu_cases[]={\n'
text+=''.join('{0x%08xUL,0x%08xUL,0x%08xUL,%u,%u,%u,%u},\n'%row for row in rows)+'};\n'
out=r/'guest/chain-cases.h';out.write_text(text)
print(json.dumps({'cases_per_pass':len(rows),'header_sha256':hashlib.sha256(out.read_bytes()).hexdigest()}))
