/* SPDX-License-Identifier: GPL-2.0-or-later
 * Actual absent-port probe: explicitly discard WB3, never retry its write.
 * All frame/GPR capture uses individual MOVEs and genuine RTE.
 */
.text
.even
.macro snapshot dest
 .irp reg,0,1,2,3,4,5,6,7
 move.l %d\reg,\dest+4*\reg
 .endr
 .irp reg,0,1,2,3,4,5,6
 move.l %a\reg,\dest+32+4*\reg
 .endr
 move.l %sp,\dest+60
.endm
.macro witness dest
 move.l _moves_target,0x00fec008
 move.l 0x00fec008,\dest
 move.l _moves_next,0x00fec008
 move.l 0x00fec008,\dest+4
.endm
.globl _moves_save_fc,_moves_restore_fc,_moves_access,_moves_trace
_moves_save_fc:
 movec %sfc,%d0
 move.l %d0,_moves_saved_sfc
 movec %dfc,%d0
 move.l %d0,_moves_saved_dfc
 rts
_moves_restore_fc:
 move.l _moves_saved_sfc,%d0
 movec %d0,%sfc
 move.l _moves_saved_dfc,%d0
 movec %d0,%dfc
 rts
_moves_access:
 snapshot _moves_fault_regs
 tst.l _moves_fault_count
 bne 9f
 tst.l _moves_trace_count
 bne 9f
 addq.l #1,_moves_fault_count
 .irp off,0,4,8,12,16,20,24,28,32,36,40,44,48,52,56
 move.l \off(%sp),_moves_fault_frame+\off
 .endr
 witness _moves_fault_native
 /* A deliberate absent-hardware probe cannot complete this write. */
 clr.w 14(%sp)
 rte
_moves_trace:
 snapshot _moves_trace_regs
 tst.l _moves_trace_count
 bne 9f
 addq.l #1,_moves_trace_count
 .irp off,0,4,8
 move.l \off(%sp),_moves_trace_frame+\off
 .endr
 move.l _moves_fault_count,_moves_faults_at_trace
 witness _moves_trace_native
 andi.w #0x3fff,(%sp)
 rte
9:
 jsr _moves_repeat_fail
 illegal
