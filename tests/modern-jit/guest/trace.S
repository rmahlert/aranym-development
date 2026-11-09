/* SPDX-License-Identifier: GPL-2.0-or-later */
.text
.even
.globl _jit_trace_cycle,_jit_trace_handler,_jit_trace_nop,_jit_trace_next
_jit_trace_cycle:
    move.l %sp,_jit_trace_before
    move.w _jit_trace_mode,%sr
_jit_trace_nop:
    nop
_jit_trace_next:
    move.w #0x2715,%sr
    move.l %sp,_jit_trace_after
    rts
_jit_trace_handler:
    movem.l %d0/%a0,-(%sp)
    addq.l #1,_jit_trace_count
    move.w 8(%sp),_jit_trace_sr
    move.l 10(%sp),_jit_trace_pc
    move.w 14(%sp),_jit_trace_format
    move.l 16(%sp),_jit_trace_instruction
    lea 8(%sp),%a0
    move.l %a0,_jit_trace_frame
    andi.w #0x3fff,8(%sp)
    movem.l (%sp)+,%d0/%a0
    rte
.data
.balign 4
.globl _jit_trace_mode,_jit_trace_sr,_jit_trace_format,_jit_trace_count
.globl _jit_trace_pc,_jit_trace_instruction,_jit_trace_before,_jit_trace_after,_jit_trace_frame
_jit_trace_mode:.word 0x2715
_jit_trace_sr:.word 0
_jit_trace_format:.word 0
.word 0
_jit_trace_count:.long 0
_jit_trace_pc:.long 0
_jit_trace_instruction:.long 0
_jit_trace_before:.long 0
_jit_trace_after:.long 0
_jit_trace_frame:.long 0
