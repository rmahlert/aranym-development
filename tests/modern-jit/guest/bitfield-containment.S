/* SPDX-License-Identifier: GPL-2.0-or-later */
.text
.even
.macro testcase name,extra
.globl _bf_\name
_bf_\name:
 move.l %a0,-(%sp)
 lea 0x00fed200,%a0
.globl _bf_\name\()_instruction
_bf_\name\()_instruction:
 .word 0xead0,\extra
 move.l (%sp)+,%a0
 rts
.endm
testcase byte,0x0008
testcase three,0x01d1
testcase five,0x01c0
.globl _bf_access
_bf_access:
 move.l %d0,_bf_saved_d0
 move.l %a0,_bf_saved_a0
 addq.l #1,_bf_faults
 .irp off,0,4,8,12,16,20,24,28,32,36,40,44,48,52,56
 move.l \off(%sp),_bf_frame+\off
 .endr
 move.w 12(%sp),%d0
 andi.w #0x0100,%d0
 bne 4f
 move.l 24(%sp),%a0
 move.w 12(%sp),%d0
 andi.w #0x0060,%d0
 cmpi.w #0x0020,%d0
 beq 1f
 cmpi.w #0x0040,%d0
 beq 2f
 move.l 28(%sp),(%a0)
 bra 4f
1: move.b 31(%sp),(%a0)
 bra 4f
2: move.w 30(%sp),(%a0)
4:
 clr.w 14(%sp)
 move.l _bf_saved_a0,%a0
 move.l _bf_saved_d0,%d0
 rte
