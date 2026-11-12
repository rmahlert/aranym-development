/* SPDX-License-Identifier: GPL-2.0-or-later
 * Capture the original frame/GPRs without MOVEM. Software completes WB3 at
 * its actual width, clears validity, restores A0 and executes genuine RTE.
 */
.text
.even
.globl _writeback_handler
_writeback_handler:
 tst.l _writeback_fault_count
 bne 9f
 move.l %d0,_writeback_fault_regs
 move.l %d1,_writeback_fault_regs+4
 move.l %d2,_writeback_fault_regs+8
 move.l %d3,_writeback_fault_regs+12
 move.l %d4,_writeback_fault_regs+16
 move.l %d5,_writeback_fault_regs+20
 move.l %d6,_writeback_fault_regs+24
 move.l %d7,_writeback_fault_regs+28
 move.l %a0,_writeback_fault_regs+32
 move.l %a1,_writeback_fault_regs+36
 move.l %a2,_writeback_fault_regs+40
 move.l %a3,_writeback_fault_regs+44
 move.l %a4,_writeback_fault_regs+48
 move.l %a5,_writeback_fault_regs+52
 move.l %a6,_writeback_fault_regs+56
 move.l %sp,_writeback_fault_regs+60
 .irp offset,0,4,8,12,16,20,24,28,32,36,40,44,48,52,56
 move.l \offset(%sp),_writeback_frame+\offset
 .endr
 addq.l #1,_writeback_fault_count
 btst #7,15(%sp)
 beq 9f
 move.l _writeback_target,0x00fec008
 move.l 0x00fec008,_writeback_native_fault
 move.l _writeback_next,0x00fec008
 move.l 0x00fec008,_writeback_native_next_fault
 move.l %a0,_writeback_saved_a0
 move.l 24(%sp),%a0
 tst.w _writeback_skip_software
 bne 4f
 btst #5,15(%sp)
 bne 1f
 btst #6,15(%sp)
 bne 2f
 move.l 28(%sp),(%a0)
 bra 4f
1:
 move.b 31(%sp),(%a0)
 bra 4f
2:
 move.w 30(%sp),(%a0)
4:
 move.l _writeback_saved_a0,%a0
 tst.w _writeback_keep_valid
 bne 5f
 clr.w 14(%sp)
5:
 move.w 14(%sp),_writeback_status_after
 move.l 0x00fed284,_writeback_bus_after
 tst.w _writeback_poison
 beq 6f
 move.l _writeback_live_pointer,_movemo_pointer
 move.l _writeback_live_a2,%a2
6:
 rte
9:
 jsr _writeback_repeat_fail
 illegal
