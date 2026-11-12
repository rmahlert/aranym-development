/* SPDX-License-Identifier: GPL-2.0-or-later
 * Observe a real MFP TimerC request, never inject a host/software IRQ.
 */
.text
.even
.globl _irqb_vbr,_irqb_access,_irqb_trace,_irqb_timer
_irqb_vbr:
 movec %vbr,%d0
 rts
.macro snapshot dest
 .irp reg,0,1,2,3,4,5,6,7
 move.l %d\reg,\dest+4*\reg
 .endr
 .irp reg,0,1,2,3,4,5,6
 move.l %a\reg,\dest+32+4*\reg
 .endr
 move.l %sp,\dest+60
.endm
_irqb_access:
 snapshot _irqb_fault_regs
 move.l #1,_irqb_failure_reason
 tst.l _irqb_fault_count
 bne 9f
 addq.l #1,_irqb_fault_count
 .irp off,0,4,8,12,16,20,24,28,32,36,40,44,48,52,56
 move.l \off(%sp),_irqb_fault_frame+\off
 .endr
 btst #7,15(%sp)
 beq 1f
 move.l %a0,_irqb_saved_a0
 move.l 24(%sp),%a0
 move.l 28(%sp),(%a0)
 move.l _irqb_saved_a0,%a0
 clr.w 14(%sp)
1:
 /* IPL7 hides real TimerC while it becomes pending. Reading IPRB does
  * not acknowledge. Observe the first IRQ; its handler masks further ones. */
 move.l %d0,_irqb_saved_d0
 move.l #1000000,%d0
2:
 btst #5,0xfffffa0d
 bne 3f
 subq.l #1,%d0
 bne 2b
 move.l #2,_irqb_failure_reason
 bra 9f
3:
 move.b 0xfffffa0d,_irqb_pending_before
 move.b #0x20,0xfffffa15
 move.l _irqb_saved_d0,%d0
 /* Admit MFP6 after RTE, still mask VBL4 and SCC5. */
 andi.w #0xf8ff,(%sp)
 ori.w #0x0500,(%sp)
 rte
_irqb_trace:
 /* This first instruction is the exact post-Trace9 eligibility witness. */
 move.l #1,_irqb_trace_started
 snapshot _irqb_trace_regs
 addq.l #1,_irqb_trace_count
 .irp off,0,4,8
 move.l \off(%sp),_irqb_trace_frame+\off
 .endr
 andi.w #0x3fff,(%sp)
 rte
_irqb_timer:
 /* ARAnyM queues TimerC catch-up ticks; IPRB may legitimately stay pending
  * after this acknowledgement. Mask later deliveries, without clearing IPRB. */
 move.b #0,0xfffffa15
 snapshot _irqb_irq_regs
 move.l #3,_irqb_failure_reason
 addq.l #1,_irqb_irq_count
 cmpi.l #1,_irqb_irq_count
 bne 9f
 move.l _irqb_trace_started,_irqb_trace_at_irq
 move.l _irqb_next_started,_irqb_next_at_irq
 move.b 0xfffffa0d,_irqb_pending_at_irq
 move.w (%sp),_irqb_irq_sr
 move.l 2(%sp),_irqb_irq_pc
 move.w 6(%sp),_irqb_irq_format
 /* Preserve the frame and all registers; ROM handles TimerC EOI/RTE. */
 move.l _irqb_old_timer,-(%sp)
 rts
9:
 jsr _irqb_timeout
 illegal
