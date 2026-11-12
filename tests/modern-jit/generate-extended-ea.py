#!/usr/bin/env python3
"""Real 040 EA instructions with independent literal address expectations.
M68000PRM tables2-1/2-2, PDF43-45, and extension-word PC base PDF63.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import argparse
import json
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True)
a=p.parse_args();a.output.mkdir(parents=True,exist_ok=True)
sources=['ind','post','pre','d16','brief-word','brief-long','brief-address','full-direct',
         'preindexed','postindexed','suppressed','negative-word','abs-long','abs-short','pc16','pcbrief','pcindirect']
destinations=[n for n in sources if n not in ['abs-short','pc16','pcbrief','pcindirect']]
forms=[]
for size in (1,2,4):
    for src in sources:
        for dst in destinations:forms.append((0,size,src,dst,0))
        forms.append((1,size,src,None,0))
        if size!=1:forms.append((2,size,src,None,0))
    for dst in destinations:
        forms.append((3,size,None,dst,0));forms.append((4,size,None,dst,0))
    for device in range(1,7):
        kind=1 if device<=3 else 3 if device<=5 else 0
        src='dev-pointer'if device in (2,3)else'dev-plain'if device in (1,6)else None
        dst='dev-pointer'if device==5 else'dev-plain'if device in (4,6)else None
        forms.append((kind,size,src,dst,device))
for src in sources+['abs-negative']:
    if src not in ['pre','post']:forms.append((5,4,src,None,0))

def operand(name,role,size,index):
    reg=2 if role=='s'else 3;base=64 if role=='s'else 160;target=96 if role=='s'else 192
    # mode,reg,extension assembly,expected space,offset,base-is-pointer,update
    if name is None:return (0,0,[],0,0,0,0)
    if name=='ind':return (2,reg,[],0,base,0,0)
    if name=='post':return (3,reg,[],0,base,0,size)
    if name=='pre':return (4,reg,[],0,base-size,0,-size)
    if name=='d16':return (5,reg,[f'.word {3 if role=="s"else -5}'],0,base+(3 if role=='s'else -5),0,0)
    if name=='brief-word':return (6,reg,['.word 0x24fd'],0,base-31,0,0)
    if name=='brief-long':return (6,reg,['.word 0x1e05'],0,base+37,0,0)
    if name=='brief-address':return (6,reg,['.word 0xd402'],0,base+14,0,0)
    if name=='full-direct':return (6,reg,['.word 0x1b30','.long -20'],0,base-12,0,0)
    if name=='preindexed':return (6,reg,['.word 0x1b21','.word -8'],0,target,1,0)
    if name=='postindexed':return (6,reg,['.word 0x1b26','.word 0','.word -5'],0,target+3,1,0)
    if name=='suppressed':
        offset=110 if role=='s'else 210
        return (6,reg,['.word 0x01f0',f'.long _ea_data+{offset}'],0,offset,0,0)
    if name=='negative-word':return (6,reg,['.word 0x2521','.word 28'],0,target,1,0)
    if name=='abs-long':
        offset=88 if role=='s'else 184
        return (7,1,[f'.long _ea_data+{offset}'],0,offset,0,0)
    if name=='abs-short':return (7,0,['.word 0x0600'],1,0x600,0,0)
    if name=='abs-negative':return (7,0,['.word 0x8000'],1,0xffff8000,0,0)
    if name=='pc16':return (7,2,[f'.word _ea_pc_data_{index}-.'],2,0,0,0)
    if name=='pcbrief':return (7,3,[f'.word 0x6a00+(_ea_pc_data_{index}-.)'],2,2,0,0)
    if name=='pcindirect':return (7,3,[f'_ea_pc_ext_{index}_{role}:','.word 0x1b22',f'.word _ea_pc_pointer_{index}-_ea_pc_ext_{index}_{role}','.word 0'],0,96,0,0)
    if name=='dev-plain':return (2,reg,[],0,base,0,0)
    if name=='dev-pointer':return (6,reg,['.word 0x0151'],0,target,1,0)
    raise ValueError(name)

asm=[r'''/* Real encoded probes, GPL-2.0-or-later. */
.text
.even
.globl _ea_extended_begin,_ea_extended_end
_ea_extended_begin:
.macro probe id,custom,insn
.globl _ea_extended_\id,_ea_extended_target_\id
_ea_extended_\id:
 movem.l %d2-%d7/%a2-%a6,-(%sp)
 move.l %sp,_ea_saved_sp
 move.w %sr,_ea_saved_sr
 ori.w #0x700,%sr
 movem.l _ea_input,%d0-%d7/%a0-%a6
 .if \custom==1
 move.l _ea_fake_sp,%sp
 .endif
 .if \custom==2
 move.l _ea_fake_sp,%sp
 .endif
 move.w _ea_ccr,%ccr
_ea_extended_target_\id:
 \insn
 move.w %ccr,_ea_reply
 movem.l %d0-%d7/%a0-%a7,_ea_output
 move.l _ea_saved_sp,%sp
 movem.l (%sp)+,%d2-%d7/%a2-%a6
 move.w _ea_saved_sr,%sr
 rts
.endm''']
h=['/* Literal expected offsets, not a copy of the emulator EA decoder. */',
   'struct ExtendedCase {unsigned kind,size,source_space;unsigned long source_offset;unsigned dest_offset,source_pointer,dest_pointer;int source_update,dest_update;unsigned device,custom;void (*fn)(void);unsigned char *pc,*pc_data;};']
rows=[]
for i,(kind,size,src,dst,device)in enumerate(forms):
    sm,sr,se,space,offset,sptr,supdate=operand(src,'s',size,i)
    dm,dr,de,_,doffset,dptr,dupdate=operand(dst,'d',size,i)
    if kind==1:dm,dr=0,0
    if kind==2:dm,dr=1,4
    if kind==4:sm,sr,se=7,4,(['.long 0x89abcdef']if size==4 else['.word 0xcdef'])
    if kind==5:opcode=0x41c0|(4<<9)|(sm<<3)|sr
    else:opcode=({1:1,2:3,4:2}[size]<<12)|(dr<<9)|(dm<<6)|(sm<<3)|sr
    instructions=[f'.word 0x{opcode:04x}',*se,*de]
    asm.append(f'probe {i},0,"'+ ';'.join(instructions)+'"')
    asm +=[f'.globl _ea_pc_data_{i}',f'_ea_pc_data_{i}:','.long 0x89abcdef,0x10203040',
           f'_ea_pc_pointer_{i}:','.long _ea_data+80,_ea_data+88,_ea_data+96']
    h.append(f'extern void ea_extended_{i}(void);extern unsigned char ea_extended_target_{i},ea_pc_data_{i};')
    rows.append(f'{{{kind},{size},{space},0x{offset:08x}UL,{doffset},{sptr},{dptr},{supdate},{dupdate},{device},0,ea_extended_{i},&ea_extended_target_{i},&ea_pc_data_{i}}},')
# Literal alias/wrap vectors are independent of the generic mode matrix.
# custom,kind,size,source-space,source-offset,destination-offset,source-pointer,
# source-update,raw instruction words. Every final transfer stays in owned RAM.
edges=[
    (1,0,1,0,64,35,0,0,'.word 0x1f9f,0x24fd'),
    (2,0,4,0,60,48,0,0,'.word 0x2fa7,0x1b30;.long -20'),
    (3,2,2,0,114,0,0,0,'.word 0x3872,0xc402'),
    (4,5,4,0,114,0,0,0,'.word 0x49f2,0xc402'),
    (5,0,4,3,16,40,1,4,'.word 0x259a,0x0151'),
    (6,0,4,3,12,24,1,-4,'.word 0x25a2,0x0151'),
    (7,1,4,0,64,0,0,0,'.word 0x2032,0xda00'),
    (8,5,4,0,64,0,0,0,'.word 0x49f2,0xde00'),
    (9,1,4,0,96,0,1,0,'.word 0x2032,0x1b21,14'),
]
for edge in edges:
    custom,kind,size,space,offset,doffset,sptr,update,code=edge;i=len(rows)
    asm.append(f'probe {i},{custom},"{code}"')
    asm +=[f'.globl _ea_pc_data_{i}',f'_ea_pc_data_{i}:','.long 0x89abcdef,0x10203040']
    h.append(f'extern void ea_extended_{i}(void);extern unsigned char ea_extended_target_{i},ea_pc_data_{i};')
    rows.append(f'{{{kind},{size},{space},{offset}UL,{doffset},{sptr},0,{update},0,0,{custom},ea_extended_{i},&ea_extended_target_{i},&ea_pc_data_{i}}},')
asm+=['_ea_extended_end:']
h+=['static const struct ExtendedCase extended_cases[]={',*rows,'};']
(a.output/'extended-probes.S').write_text('\n'.join(asm)+'\n')
(a.output/'extended-cases.h').write_text('\n'.join(h)+'\n')
(a.output/'oracle-meta.json').write_text(json.dumps({'forms':len(rows),'checks':len(rows)*32*2,
    'native_cases':(sum(not f[4]for f in forms)+len(edges))*32,'device_cases':sum(bool(f[4])for f in forms)*32},indent=2)+'\n')
print(len(rows),'MOVE/MOVEA/LEA forms including read-sensitive device deoptimization and A7/index/base alias/wrap edges')
