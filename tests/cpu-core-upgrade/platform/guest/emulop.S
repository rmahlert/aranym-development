/* SPDX-License-Identifier: GPL-2.0-or-later */
.text
.globl _emulop_checks
_emulop_checks:
    movem.l %d2-%d7/%a2-%a6,-(%sp)
    move.w %sr,-(%sp)
    move.l %sp,_emulop_caller_sp
    move.w #0x2700,%sr
    move.l %usp,%a0
    move.l %a0,.saved_usp
    lea .user_stack+512,%a0
    move.l %a0,%usp
    move.l %a0,_emulop_user_top
    movec %vbr,%a0
    lea 32(%a0),%a0
    move.l %a0,.vector_address
    move.l (%a0),.old_vector
    move.l #.privilege_handler,(%a0)
    move.l #.privilege_instruction,_emulop_expected_pc
    subq.l #8,%sp
    move.w _emulop_ccr,%d0
    or.w #0x0700,%d0
    move.w %d0,(%sp)
    move.l #.user_entry,2(%sp)
    clr.w 6(%sp)
    rte
.user_entry:
    moveq #0,%d0
    move.l #0x11110001,%d1
    move.l #0x11110002,%d2
    move.l #0x11110003,%d3
    move.l #0x11110004,%d4
    move.l #0x11110005,%d5
    move.l #0x11110006,%d6
    move.l #0x11110007,%d7
    move.l #0x11110008,%a0
    move.l #0x11110009,%a1
    move.l #0x1111000a,%a2
    move.l #0x1111000b,%a3
    move.l #0x1111000c,%a4
    move.l #0x1111000d,%a5
    move.l #0x1111000e,%a6
    move.w _emulop_ccr,%ccr
    .word 0x713b /* original EmulOp JIT-state query, returns -1 in noJIT build */
    move.w %ccr,_emulop_reply_ccr
    movem.l %d0-%d7/%a0-%a7,_emulop_regs
.privilege_instruction:
    .word 0x712a /* original supervisor-only console EmulOp: no user output */
    move.l #9,-(%sp)
    move.l _emulop_exit_id,-(%sp)
    jsr _nf_call /* failure path if host wrongly accepts the user operation */
.super_resume:
    movem.l %d0-%d7/%a0-%a7,_emulop_after_regs
    move.l %usp,%a0
    move.l %a0,_emulop_after_usp
    move.w #0x2700,%sr
    move.l _emulop_caller_sp,%sp
    move.l .vector_address,%a0
    move.l .old_vector,(%a0)
    move.l .saved_usp,%a0
    move.l %a0,%usp
    move.w (%sp)+,%sr
    movem.l (%sp)+,%d2-%d7/%a2-%a6
    rts
.privilege_handler:
    movem.l %d0/%a0,-(%sp)
    move.w 8(%sp),_emulop_frame_sr
    move.l 10(%sp),_emulop_frame_pc
    move.w 14(%sp),_emulop_frame_format
    lea 8(%sp),%a0
    move.l %a0,_emulop_frame_sp
    move.l #.super_resume,10(%sp)
    or.w #0x2000,8(%sp)
    movem.l (%sp)+,%d0/%a0
    rte
.data
.balign 4
.old_vector: .long 0
.vector_address: .long 0
.saved_usp: .long 0
.globl _emulop_regs,_emulop_after_regs,_emulop_frame_pc,_emulop_expected_pc
.globl _emulop_frame_sp,_emulop_caller_sp,_emulop_user_top,_emulop_after_usp,_emulop_exit_id
_emulop_regs: .space 64
_emulop_after_regs: .space 64
_emulop_frame_pc: .long 0
_emulop_expected_pc: .long 0
_emulop_frame_sp: .long 0
_emulop_caller_sp: .long 0
_emulop_user_top: .long 0
_emulop_after_usp: .long 0
_emulop_exit_id: .long 0
.globl _emulop_ccr,_emulop_reply_ccr,_emulop_frame_sr,_emulop_frame_format
_emulop_ccr: .word 0
_emulop_reply_ccr: .word 0
_emulop_frame_sr: .word 0
_emulop_frame_format: .word 0
.balign 4
.user_stack: .space 640
