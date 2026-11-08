/* SPDX-License-Identifier: GPL-2.0-or-later */
.text
.globl _mfp_irq_checks
_mfp_irq_checks:
    movem.l %d2-%d7/%a2-%a6,-(%sp)
    move.w %sr,-(%sp)
    move.l %sp,_irq_caller_sp
    move.w #0x2700,%sr
    movec %msp,%d0
    move.l %d0,.saved_msp
    lea .master_stack+512,%a0
    move.l %a0,_irq_master_top
    movec %a0,%msp
    /* TimerC uses MFP source5, actual VR base and current VBR. */
    moveq #0,%d0
    move.b 0xfffffa17,%d0
    and.w #0xf0,%d0
    or.w #5,%d0
    lsl.w #2,%d0
    move.l %d0,_irq_vector_offset
    movec %vbr,%d1
    add.l %d1,%d0
    move.l %d0,%a0
    move.l %a0,.vector_address
    move.l (%a0),.old_vector
    move.l #.timer_handler,(%a0)
    move.l #.irq_return,_irq_expected_pc
    clr.l .observed
    /* IPL5 excludes VBL4 and permits real MFP6. STOP installs M=1 and the
     * exact saved CCR; only a real TimerC IRQ can reach .irq_return. */
    stop #0x3515
.irq_return:
    move.w %sr,_irq_after_sr
    move.l %sp,_irq_after_sp
    move.w #0x2700,%sr
    /* Capture first, then recover caller even on a measured bank mismatch. */
    move.l _irq_caller_sp,%sp
    move.l .vector_address,%a0
    move.l .old_vector,(%a0)
    move.l .saved_msp,%d0
    movec %d0,%msp
    move.w (%sp)+,%sr
    movem.l (%sp)+,%d2-%d7/%a2-%a6
    rts
.timer_handler:
    movem.l %d0-%d1/%a0-%a1,-(%sp)
    tst.l .observed
    bne .chain
    move.l #1,.observed
    move.w 16(%sp),_irq_saved_sr
    move.l 18(%sp),_irq_pc
    move.w 22(%sp),_irq_format
    lea 16(%sp),%a0
    move.l %a0,_irq_frame_sp
    movec %msp,%a0
    move.l %a0,_irq_master_frame_sp
    move.w (%a0),_irq_master_sr
    move.l 2(%a0),_irq_master_pc
    move.w 6(%a0),_irq_master_format
.chain:
    movem.l (%sp)+,%d0-%d1/%a0-%a1
    move.l .old_vector,-(%sp)
    rts /* chain unchanged ROM timer handler and its real RTE/EOI */
.data
.balign 4
.old_vector: .long 0
.vector_address: .long 0
.saved_msp: .long 0
.observed: .long 0
.globl _irq_pc,_irq_frame_sp,_irq_master_pc,_irq_master_frame_sp
.globl _irq_after_sp,_irq_expected_pc,_irq_master_top,_irq_vector_offset,_irq_caller_sp
_irq_pc: .long 0
_irq_frame_sp: .long 0
_irq_master_pc: .long 0
_irq_master_frame_sp: .long 0
_irq_after_sp: .long 0
_irq_expected_pc: .long 0
_irq_master_top: .long 0
_irq_vector_offset: .long 0
_irq_caller_sp: .long 0
.globl _irq_saved_sr,_irq_format,_irq_master_sr,_irq_master_format,_irq_after_sr
_irq_saved_sr: .word 0
_irq_format: .word 0
_irq_master_sr: .word 0
_irq_master_format: .word 0
_irq_after_sr: .word 0
.balign 4
.master_stack: .space 640
