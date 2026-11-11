/* SPDX-License-Identifier: GPL-2.0-or-later */
.text
.even
.globl _alu_probe_begin,_alu_probe_end
_alu_probe_begin:
.macro probe name,insn
.globl \name
\name:
    movem.l %d2-%d7/%a2-%a6,-(%sp)
    move.l %sp,_alu_before_sp
    move.l #0x11110002,%d2
    move.l #0x11110003,%d3
    move.l #0x11110004,%d4
    move.l #0x11110005,%d5
    move.l #0x11110006,%d6
    move.l #0x11110007,%d7
    move.l #0x22220000,%a0
    move.l #0x22220001,%a1
    move.l #0x22220002,%a2
    move.l #0x22220003,%a3
    move.l #0x22220004,%a4
    move.l #0x22220005,%a5
    move.l #0x22220006,%a6
    move.l _alu_src,%d0
    move.l _alu_dst,%d1
    move.w _alu_ccr,%ccr
    \insn
    move.w %ccr,_alu_reply_ccr
    movem.l %d0-%d7/%a0-%a7,_alu_registers
    movem.l (%sp)+,%d2-%d7/%a2-%a6
    rts
.endm
probe _alu_addx_b,"addx.b %d0,%d1"
probe _alu_addx_w,"addx.w %d0,%d1"
probe _alu_addx_l,"addx.l %d0,%d1"
probe _alu_subx_b,"subx.b %d0,%d1"
probe _alu_subx_w,"subx.w %d0,%d1"
probe _alu_subx_l,"subx.l %d0,%d1"
probe _alu_add_b,"add.b %d0,%d1"
probe _alu_add_w,"add.w %d0,%d1"
probe _alu_add_l,"add.l %d0,%d1"
probe _alu_sub_b,"sub.b %d0,%d1"
probe _alu_sub_w,"sub.w %d0,%d1"
probe _alu_sub_l,"sub.l %d0,%d1"
probe _alu_cmp_b,"cmp.b %d0,%d1"
probe _alu_cmp_w,"cmp.w %d0,%d1"
probe _alu_cmp_l,"cmp.l %d0,%d1"
probe _alu_neg_b,"neg.b %d1"
probe _alu_neg_w,"neg.w %d1"
probe _alu_neg_l,"neg.l %d1"
probe _alu_negx_b,"negx.b %d1"
probe _alu_negx_w,"negx.w %d1"
probe _alu_negx_l,"negx.l %d1"
_alu_probe_end:

.globl _alu_jit_query
_alu_jit_query:
    moveq #-1,%d0
    .word 0x713b
    rts
