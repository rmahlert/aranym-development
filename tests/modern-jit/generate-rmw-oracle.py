#!/usr/bin/env python3
"""Immutable host-integer expectations and real unary/quick/immediate probes.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import argparse
import json
from pathlib import Path

p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True)
p.add_argument('--flags-dead',action='store_true')
a=p.parse_args();out=a.output;out.mkdir(parents=True,exist_ok=True)
families=['neg','negx','clr','not','tst','addq1','subq1','addq8','subq8','ori','andi','eori','addi','subi','cmpi']
modes=['ind','post','pre','d16','brief-word','brief-long','brief-address','full-direct',
       'preindexed','postindexed','suppressed','negative-word','abs-long',
       'a7-ind','a7-post','a7-pre','a7-d16']

def operand(name,size,i):
    # Literal expected offsets, independent of the emulator's preflight.
    if name=='ind':return 2,2,[],64,0,0,0,0
    if name=='post':return 3,2,[],64,0,size,0,0
    if name=='pre':return 4,2,[],64-size,0,-size,0,0
    if name=='d16':return 5,2,['.word -5'],59,0,0,0,0
    if name=='brief-word':return 6,2,['.word 0x24fd'],33,0,0,0,0
    if name=='brief-long':return 6,2,['.word 0x1e05'],101,0,0,0,0
    if name=='brief-address':return 6,2,['.word 0xd402'],78,0,0,0,0
    if name=='full-direct':return 6,2,['.word 0x1b30','.long -20'],52,0,0,0,0
    if name=='preindexed':return 6,2,['.word 0x1b21','.word -8'],96,1,0,0,0
    if name=='postindexed':return 6,2,['.word 0x1b26','.word 0','.word -5'],99,1,0,0,0
    if name=='suppressed':return 6,2,['.word 0x01f0','.long _rmw_data+110'],110,0,0,0,0
    if name=='negative-word':return 6,2,['.word 0x2521','.word 28'],96,1,0,0,0
    if name=='abs-long':return 7,1,['.long _rmw_data+88'],88,0,0,0,0
    if name.startswith('a7-'):
        kind=name[3:];mode={'ind':2,'post':3,'pre':4,'d16':5}[kind]
        step=2 if size==1 else size;offset=64-step if kind=='pre'else 59 if kind=='d16'else 64
        update=step if kind=='post'else -step if kind=='pre'else 0
        return mode,7,['.word -5']if kind=='d16'else[],offset,0,update,1,0
    if name=='pc16':return 7,2,[f'.word _rmw_pc_data_{i}-.'],0,0,0,0,1
    if name=='pcbrief':return 7,3,[f'.word 0x6a00+(_rmw_pc_data_{i}-.)'],2,0,0,0,1
    if name=='pcindirect':return 7,3,[f'_rmw_pc_ext_{i}:','.word 0x1b22',f'.word _rmw_pc_pointer_{i}-_rmw_pc_ext_{i}','.word 0'],96,0,0,0,0
    if name=='device':return 2,2,[],64,0,0,0,0
    if name in ['pointer-device','device-pointer']:return 6,2,['.word 0x0151'],96,1,0,0,0
    raise ValueError(name)

def instruction(family,size,mode,reg):
    ordinal={1:0,2:1,4:2}[size];ea=(mode<<3)|reg
    unary={'neg':0x4400,'negx':0x4000,'clr':0x4200,'not':0x4600,'tst':0x4a00}
    immediate={'ori':0x0000,'andi':0x0200,'eori':0x0a00,'addi':0x0600,'subi':0x0400,'cmpi':0x0c00}
    if family in unary:return unary[family]|(ordinal<<6)|ea,[]
    if family in immediate:
        data=['.long 0x89abcdef']if size==4 else['.word 0xcdef']
        return immediate[family]|(ordinal<<6)|ea,data
    amount=1 if family.endswith('1')else 8
    return 0x5000|((amount&7)<<9)|(0x100 if family.startswith('sub')else 0)|(ordinal<<6)|ea,[]

def vector(family,size,value,ccr):
    mask=(1<<(size*8))-1;sign=1<<(size*8-1);arg=0x89abcdef&mask
    result=value;carry=False;overflow=False;extend=bool(ccr&16)
    if family=='neg':result=-value;carry=value!=0;overflow=value==sign;extend=carry
    elif family=='negx':
        result=-value-int(extend);carry=value!=0 or extend
        overflow=bool(value&result&sign);extend=carry
    elif family=='clr':result=0
    elif family=='not':result=~value
    elif family=='ori':result=value|arg
    elif family=='andi':result=value&arg
    elif family=='eori':result=value^arg
    elif family in ['addi','addq1','addq8']:
        operand=arg if family=='addi'else 1 if family=='addq1'else 8
        result=value+operand;carry=result>mask;overflow=bool(~(value^operand)&(value^result)&sign);extend=carry
    elif family in ['subi','cmpi','subq1','subq8']:
        operand=arg if family in ['subi','cmpi']else 1 if family=='subq1'else 8
        result=value-operand;carry=value<operand;overflow=bool((value^operand)&(value^result)&sign)
        if family!='cmpi':extend=carry
    elif family!='tst':raise ValueError(family)
    result&=mask
    zero=result==0 and (family!='negx' or bool(ccr&4))
    flags=(16 if extend else 0)|(8 if result&sign else 0)|(4 if zero else 0)|(2 if overflow else 0)|int(carry)
    return value if family in ['tst','cmpi']else result,flags

assembly=[r'''/* GPL-2.0-or-later: real encoded target instructions. */
.text
.even
.globl _rmw_probe_begin,_rmw_probe_end
_rmw_probe_begin:
.macro probe id,a7,insn
.globl _rmw_probe_\id,_rmw_target_\id
_rmw_probe_\id:
 movem.l %d2-%d7/%a2-%a6,-(%sp)
 move.l %sp,_rmw_saved_sp
 move.w %sr,_rmw_saved_sr
 ori.w #0x700,%sr
 movem.l _rmw_input,%d0-%d7/%a0-%a6
 .if \a7
 move.l _rmw_fake_sp,%sp
 .endif
 move.w _rmw_ccr,%ccr
_rmw_target_\id:
 \insn
 move.w %ccr,_rmw_reply
 movem.l %d0-%d7/%a0-%a7,_rmw_output
 move.l _rmw_saved_sp,%sp
 movem.l (%sp)+,%d2-%d7/%a2-%a6
 move.w _rmw_saved_sr,%sr
 rts
.endm''']
header=['/* Independent immutable host-integer results. GPL-2.0-or-later. */',
        f'#define RMW_FLAGS_DEAD {int(a.flags_dead)}',
        'struct RmwCase {unsigned family,size,space,offset,pointer,a7,device,full_values;int update;void(*fn)(void);unsigned char *pc,*pc_data;};']
rows=[];meta=[]
for family_index,family in enumerate(families):
    for size in [1,2,4]:
        selected=modes+(['pc16','pcbrief','pcindirect']if family in ['tst','cmpi']else[])+['device','pointer-device','device-pointer']
        for name in selected:
            i=len(rows);mode,reg,extensions,offset,pointer,update,a7,space=operand(name,size,i)
            opcode,immediate=instruction(family,size,mode,reg)
            code=[f'.word 0x{opcode:04x}',*immediate,*extensions]
            if a.flags_dead:
                # Both native register instructions overwrite the target's
                # output flags, including X. No unsupported CCR setter lies
                # between the target and the overwrite to force live flags.
                code+=['moveq #0,%d6','lsl.l #1,%d6']
            assembly.append(f'probe {i},{a7},"'+ ';'.join(code)+'"')
            assembly +=[f'.globl _rmw_pc_data_{i}',f'_rmw_pc_data_{i}:','.long 0,0',f'_rmw_pc_pointer_{i}:','.long _rmw_data+80,_rmw_data+88,_rmw_data+96']
            header.append(f'extern void rmw_probe_{i}(void);extern unsigned char rmw_target_{i},rmw_pc_data_{i};')
            device={'device':1,'pointer-device':2,'device-pointer':3}.get(name,0);full=int(name=='ind')
            rows.append(f'{{{family_index},{size},{space},{offset},{pointer},{a7},{device},{full},{update},rmw_probe_{i},&rmw_target_{i},&rmw_pc_data_{i}}},')
            meta.append(dict(family=family,size=size,mode=name,device=device,full_values=bool(full),opcode=f'{opcode:04x}'))
assembly.append('_rmw_probe_end:');header+=['static const struct RmwCase rmw_cases[]={',*rows,'};']
header+=['struct RmwVector {unsigned long input,output;unsigned short flags;};','static const struct RmwVector rmw_vectors[15][3][8][32]={']
for family in families:
    header.append('{')
    for size in [1,2,4]:
        mask=(1<<(size*8))-1;sign=1<<(size*8-1);arg=0x89abcdef&mask
        values=[0,1,mask,sign,sign-1,sign+1,(-arg)&mask,(sign-arg)&mask]
        header.append('{')
        for value in values:
            header.append('{')
            for ccr in range(32):
                result,flags=vector(family,size,value,ccr)
                header.append(f'{{0x{value:08x}UL,0x{result:08x}UL,0x{flags:02x}}},')
            header.append('},')
        header.append('},')
    header.append('},')
header.append('};')
(out/'rmw-probes.S').write_text('\n'.join(assembly)+'\n')
(out/'rmw-cases.h').write_text('\n'.join(header)+'\n')
counts=dict(forms=len(meta),checks=sum(8 if c['full_values']else 1 for c in meta)*32*2,
            native_cases=sum(8 if c['full_values']else 1 for c in meta if not c['device'])*32,
            device_cases=sum(bool(c['device'])for c in meta)*32)
counts['flags_dead']=a.flags_dead
(out/'oracle-meta.json').write_text(json.dumps(counts,indent=2)+'\n')
(out/'form-inventory.json').write_text(json.dumps(meta,indent=2)+'\n')
print(json.dumps(counts),flush=True)
