#!/usr/bin/env python3
"""Precompute guest-independent integer/CCR oracles with Python integers.
SPDX-License-Identifier: GPL-2.0-or-later
"""
from pathlib import Path
import hashlib,json
ROOT=Path(__file__).resolve().parent
def oracle(op,width,src,dst,ccr):
    mod=1<<width;mask=mod-1;sign=mod>>1
    x=(ccr>>4)&1 if op in (0,1,6) else 0
    a,b=(dst,0) if op>=5 else (src,dst)
    subtract=op in (1,3,4,5,6)
    result=(b-a-x if subtract else b+a+x)&mask
    carry=(a+x>b) if subtract else (b+a+x>=mod)
    signed=lambda v:v-mod if v&sign else v
    math=signed(b)-signed(a)-x if subtract else signed(b)+signed(a)+x
    overflow=math < -sign or math>=sign
    z=result==0 and (bool(ccr&4) if op in (0,1,6) else True)
    flags=int(carry)|(int(overflow)<<1)|(int(z)<<2)|(int(bool(result&sign))<<3)|((ccr&16) if op==4 else (int(carry)<<4))
    fullsrc=(0x76543210&~mask)|src;fulldst=(0xfedcba98&~mask)|dst
    fullresult=fulldst if op==4 else (fulldst&~mask)|result
    return fullsrc,fulldst,fullresult,flags,op,(0 if width==8 else 1 if width==16 else 2),ccr
def main():
    assert oracle(0,8,255,0,20)[2:4]==(0xfedcba00,21)
    assert oracle(0,8,255,0,16)[3]==17
    assert oracle(1,8,255,0,20)[2:4]==(0xfedcba00,21)
    assert oracle(5,8,0,128,0)[3]==27
    assert oracle(4,8,1,0,16)[3]==25
    rows=[]
    for op in range(7):
        for width in (8,16,32):
            mask=(1<<width)-1;sign=(1<<width)>>1
            edges=[0,1,2,sign-1,sign,sign+1,mask-1,mask]
            for src in edges[:1] if op>=5 else edges:
                for dst in edges:
                    for ccr in range(32):rows.append(oracle(op,width,src,dst,ccr))
    assert len(rows)==32256
    text='/* Generated on the host; do not compute the oracle in the guest. */\n'
    text+='struct alu_case { unsigned long src,dst,result; unsigned short flags,op,size,ccr; };\n'
    text+='static const struct alu_case alu_cases[]={\n'
    text+=''.join('{0x%08xUL,0x%08xUL,0x%08xUL,%u,%u,%u,%u},\n'%r for r in rows)+'};\n'
    out=ROOT/'guest/alu-cases.h';out.write_text(text)
    print(json.dumps({'cases_per_pass':len(rows),'passes':2,'header_sha256':hashlib.sha256(out.read_bytes()).hexdigest()}))
if __name__=='__main__':main()
