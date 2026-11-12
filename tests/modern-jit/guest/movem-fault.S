/* SPDX-License-Identifier: GPL-2.0-or-later
 * Capture without clobbering any GPR. Read faults use real RTE/CM restart.
 * Write faults use an explicit probe-style discard: clear WB3/CM and advance
 * the stacked PC. This tests partial stores and metadata, not write restart.
 */
.text
.even
.globl _movemf_handler
_movemf_handler:
 tst.l _movemf_fault_count
 bne 2f
 move.l %d0,_movemf_fault_regs
 move.l %d1,_movemf_fault_regs+4
 move.l %d2,_movemf_fault_regs+8
 move.l %d3,_movemf_fault_regs+12
 move.l %d4,_movemf_fault_regs+16
 move.l %d5,_movemf_fault_regs+20
 move.l %d6,_movemf_fault_regs+24
 move.l %d7,_movemf_fault_regs+28
 move.l %a0,_movemf_fault_regs+32
 move.l %a1,_movemf_fault_regs+36
 move.l %a2,_movemf_fault_regs+40
 move.l %a3,_movemf_fault_regs+44
 move.l %a4,_movemf_fault_regs+48
 move.l %a5,_movemf_fault_regs+52
 move.l %a6,_movemf_fault_regs+56
 move.l %sp,_movemf_fault_regs+60
 .irp offset,0,4,8,12,16,20,24,28,32,36,40,44,48,52,56
 move.l \offset(%sp),_movemf_frame+\offset
 .endr
 addq.l #1,_movemf_fault_count
 tst.w _movemf_poison_pointer
 beq 3f
 move.l #0x89ab891b,_movemo_pointer
3:
 tst.w _movemf_write
 beq 1f
 move.l _movemf_next,2(%sp)
 andi.w #0xefff,12(%sp)
 clr.w 14(%sp)
1:
 rte
2:
 jsr _movemf_repeat_fail
 illegal
