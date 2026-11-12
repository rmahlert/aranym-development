#!/usr/bin/env python3
"""Generate integer products with host arbitrary precision, not emulator helpers.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import argparse
from pathlib import Path

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--output',type=Path,required=True)
a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True)
forms=[]
for signed in (0,1):
    for source in (0,1):
        for dead in (0,1):
            forms.append((signed,source,0,dead,0,0,0,f'mul{"s" if signed else "u"}.w %d{source},%d1'))
    for immediate in (0,1,0x7fff,0x8000,0xffff):
        for dead in (0,1):
            forms.append((signed,3,0,dead,0,0,immediate,f'mul{"s" if signed else "u"}.w #{immediate},%d1'))
for signed in (0,1):
    for wide in (0,1):
        for source in (0,1,2):
            for dead in (0,1):
                extra=0x1002|(signed<<11)|(wide<<10)
                forms.append((2+signed,source,wide,dead,0,0,0,f'.word 0x{0x4c00+source:04x},0x{extra:04x}'))
        for immediate in (0,1,0x7fffffff,0x80000000,0xffffffff):
            for dead in (0,1):
                extra=0x1002|(signed<<11)|(wide<<10)
                forms.append((2+signed,3,wide,dead,0,0,immediate,f'.word 0x4c3c,0x{extra:04x};.long 0x{immediate:08x}'))
exchanges=[(left,right)for left in range(8)for right in range(8)]
exchanges += [(left+8,right+8)for left in range(8)for right in range(8)]
exchanges += [(left,right+8)for left in range(8)for right in range(8)]
for left,right in exchanges:
    reg=lambda r:f'%d{r}' if r<8 else f'%a{r-8}'
    forms.append((4,0,0,0,left,right,0,f'exg {reg(left)},{reg(right)}'))
assembly=[r'''/* Generated real probes; GPL-2.0-or-later. */
.text
.even
.globl _scalar_begin,_scalar_end
_scalar_begin:
.macro probe id,dead,insn
.globl _scalar_\id,_scalar_target_\id
_scalar_\id:
 movem.l %d2-%d7/%a2-%a6,-(%sp)
 move.l %sp,_scalar_saved_sp
 move.w %sr,_scalar_saved_sr
 ori.w #0x700,%sr
 movem.l _scalar_input,%d0-%d7/%a0-%a6
 move.w _scalar_ccr,%ccr
_scalar_target_\id:
 \insn
 .if \dead
 moveq #0,%d7
 .endif
 move.w %ccr,_scalar_reply
 movem.l %d0-%d7/%a0-%a7,_scalar_output
 move.l _scalar_saved_sp,%sp
 movem.l (%sp)+,%d2-%d7/%a2-%a6
 move.w _scalar_saved_sr,%sr
 rts
.endm''']
header=['/* Host arbitrary-precision product expectations; GPL-2.0-or-later. */',
        'struct ScalarForm {unsigned kind,source,wide,dead,left,right;void (*fn)(void);unsigned char *pc;};']
for i,form in enumerate(forms):
    kind,source,wide,dead,left,right,immediate,insn=form
    assembly.append(f'probe {i},{dead},"{insn}"')
    header.append(f'extern void scalar_{i}(void);extern unsigned char scalar_target_{i};')
assembly+=['_scalar_end:','.globl _scalar_jit_query','_scalar_jit_query:','moveq #-1,%d0','.word 0x713b','rts']
header.append('static const struct ScalarForm forms[]={')
for i,(kind,source,wide,dead,left,right,immediate,_) in enumerate(forms):
    header.append(f'{{{kind},{source},{wide},{dead},{left},{right},scalar_{i},&scalar_target_{i}}},')
header+=['};','struct ScalarCase {unsigned form;unsigned long a,b,low,high,flags;};','static const struct ScalarCase scalar_cases[]={']
values=[0,1,0x7fff,0x8000,0xffff,0x7fffffff,0x80000000,0xffffffff]
def signed_value(v,bits):
    v&=(1<<bits)-1
    return v-(1<<bits) if v&(1<<(bits-1)) else v
case_count=0
for i,(kind,source,wide,dead,left,right,immediate,_) in enumerate(forms):
    pairs=[(x,y)for x in values for y in values]
    if source==3:pairs=[(immediate,y)for y in values]
    if kind==4:pairs=[(0,0),(1,0xffffffff),(0x80000000,0x7fffffff),(0xffffffff,0x80000000)]
    for x,y in pairs:
            lo=hi=flags=0
            if kind!=4:
                src=y if source==1 else x
                signed=kind in (1,3)
                bits=16 if kind<2 else 32
                product=(signed_value(src,bits)*signed_value(y,bits) if signed else (src&((1<<bits)-1))*(y&((1<<bits)-1)))
                lo=product&0xffffffff;hi=(product>>32)&0xffffffff
                result=product if wide else lo
                overflow=kind>=2 and not wide and (product!=signed_value(lo,32) if signed else hi!=0)
                flags=(8 if (hi if wide else lo)&0x80000000 else 0)|(4 if result==0 else 0)|(2 if overflow else 0)
                if dead:flags=4
            header.append(f'{{{i},0x{x:08x}UL,0x{y:08x}UL,0x{lo:08x}UL,0x{hi:08x}UL,{flags}}},')
            case_count+=1
header.append('};')
(a.output/'scalar-probes.S').write_text('\n'.join(assembly)+'\n')
(a.output/'scalar-cases.h').write_text('\n'.join(header)+'\n')
print(len(forms),'forms;',case_count,'immutable product/register cases')
