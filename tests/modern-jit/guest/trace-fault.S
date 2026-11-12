/* SPDX-License-Identifier: GPL-2.0-or-later
 * Real guest access and trace handlers; individual MOVE capture, genuine RTE.
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
 move.l _trace_target,0x00fec008
 move.l 0x00fec008,\dest
 move.l _trace_next,0x00fec008
 move.l 0x00fec008,\dest+4
.endm
.globl _trace_access_handler,_trace_handler
_trace_access_handler:
 snapshot _trace_fault_regs
 tst.l _trace_fault_count
 bne 9f
 tst.l _trace_count
 bne 9f
 addq.l #1,_trace_fault_count
 .irp offset,0,4,8,12,16,20,24,28,32,36,40,44,48,52,56
 move.l \offset(%sp),_trace_fault_frame+\offset
 .endr
 witness _trace_fault_native
 btst #7,15(%sp)
 beq 8f
 move.l %a0,_trace_saved_a0
 move.l 24(%sp),%a0
 btst #5,15(%sp)
 bne 1f
 btst #6,15(%sp)
 bne 2f
 move.l 28(%sp),(%a0)
 bra 3f
1:
 move.b 31(%sp),(%a0)
 bra 3f
2:
 move.w 30(%sp),(%a0)
3:
 move.l _trace_saved_a0,%a0
 clr.w 14(%sp)
8:
 rte
_trace_handler:
 snapshot _trace_regs
 tst.l _trace_count
 bne 9f
 addq.l #1,_trace_count
 .irp offset,0,4,8
 move.l \offset(%sp),_trace_frame+\offset
 .endr
 move.l _trace_fault_count,_trace_faults_seen
 witness _trace_native
 andi.w #0x3fff,(%sp)
 rte
9:
 jsr _trace_repeat_fail
 illegal
