/* SPDX-License-Identifier: GPL-2.0-or-later
 * Actual guest format7 handlers: discard a completed-read/writeback fault,
 * or repair A0 and use genuine RTE/restart after a failed operand read.
 */
.text
.even
.globl _rmwf_enter,_rmwf_begin,_rmwf_end,_rmwf_target,_rmwf_next
_rmwf_enter:
 movem.l %d2-%d7/%a2-%a6,-(%sp)
 move.w %sr,-(%sp)
 ori.w #0x0700,%sr
 move.l 8,_rmwf_old_vector
 move.l #rmwf_handler,8
 move.l 0x00fec008,_rmwf_native_start
 moveq #63,%d2
1:
 move.w #0x7fff,_rmwf_ram
 move.l #_rmwf_ram,_rmwf_input+32
 bsr rmwf_probe
 dbra %d2,1b
 move.l 0x00fec008,_rmwf_native_before
 move.w #0x7fff,_rmwf_ram
 move.l #0x7fff,0x00fed084
 move.l #0x00fed080,_rmwf_input+32
 tst.w _rmwf_mode
 beq 2f
 move.l #0x00fed090,_rmwf_input+32
2:
 bsr rmwf_probe
 move.l 0x00fec008,_rmwf_native_after
 move.l _rmwf_old_vector,8
 move.w (%sp)+,%sr
 movem.l (%sp)+,%d2-%d7/%a2-%a6
 moveq #0,%d0
 rts
_rmwf_begin:
rmwf_probe:
 movem.l %d2-%d7/%a2-%a6,-(%sp)
 move.l %sp,_rmwf_target_sp
 movem.l _rmwf_input,%d0-%d7/%a0-%a6
 move.w _rmwf_ccr,%ccr
_rmwf_target:
 addq.w #1,(%a0)+
_rmwf_next:
 move.w %ccr,_rmwf_reply
 movem.l %d0-%d7/%a0-%a7,_rmwf_output
 movem.l (%sp)+,%d2-%d7/%a2-%a6
 rts
_rmwf_end:
rmwf_handler:
 movem.l %d0-%d7/%a0-%a7,_rmwf_fault_regs
 addq.l #1,_rmwf_fault_count
 move.w (%sp),_rmwf_frame_sr
 move.l 2(%sp),_rmwf_frame_pc
 move.w 6(%sp),_rmwf_format
 move.w 12(%sp),_rmwf_ssw
 move.w 14(%sp),_rmwf_wb3s
 move.l 20(%sp),_rmwf_fault_address
 move.l 24(%sp),_rmwf_wb3_address
 move.l 28(%sp),_rmwf_wb3_data
 tst.w _rmwf_mode
 bne 3f
 adda.w #60,%sp
 move.w _rmwf_frame_sr,%sr
 jmp _rmwf_next
3:
 lea _rmwf_ram,%a0
 rte
