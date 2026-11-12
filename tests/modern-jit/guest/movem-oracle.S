/* SPDX-License-Identifier: GPL-2.0-or-later
 * All setup/capture uses individual MOVE instructions, never MOVEM.
 * The full-format indirect JMP suppresses both index/base and preserves all
 * architectural registers and CCR while dispatching through a RAM pointer.
 */
.text
.even
.globl _movemo_setup,_movemo_capture
_movemo_setup:
 move.w %sr,_movemo_saved_sr
 ori.w #0x700,%sr
 move.l %d2,-(%sp)
 move.l %d3,-(%sp)
 move.l %d4,-(%sp)
 move.l %d5,-(%sp)
 move.l %d6,-(%sp)
 move.l %d7,-(%sp)
 move.l %a2,-(%sp)
 move.l %a3,-(%sp)
 move.l %a4,-(%sp)
 move.l %a5,-(%sp)
 move.l %a6,-(%sp)
 move.l %sp,_movemo_real_sp
 move.l _movemo_input,%d0
 move.l _movemo_input+4,%d1
 move.l _movemo_input+8,%d2
 move.l _movemo_input+12,%d3
 move.l _movemo_input+16,%d4
 move.l _movemo_input+20,%d5
 move.l _movemo_input+24,%d6
 move.l _movemo_input+28,%d7
 move.l _movemo_input+32,%a0
 move.l _movemo_input+36,%a1
 move.l _movemo_input+40,%a2
 move.l _movemo_input+44,%a3
 move.l _movemo_input+48,%a4
 move.l _movemo_input+52,%a5
 move.l _movemo_input+56,%a6
 move.l _movemo_input+60,%sp
 move.w _movemo_ccr,%ccr
 .word 0x4ef0,0x01f1
 .long _movemo_target_pointer
_movemo_capture:
 move.w %ccr,_movemo_reply
 move.l %d0,_movemo_output
 move.l %d1,_movemo_output+4
 move.l %d2,_movemo_output+8
 move.l %d3,_movemo_output+12
 move.l %d4,_movemo_output+16
 move.l %d5,_movemo_output+20
 move.l %d6,_movemo_output+24
 move.l %d7,_movemo_output+28
 move.l %a0,_movemo_output+32
 move.l %a1,_movemo_output+36
 move.l %a2,_movemo_output+40
 move.l %a3,_movemo_output+44
 move.l %a4,_movemo_output+48
 move.l %a5,_movemo_output+52
 move.l %a6,_movemo_output+56
 move.l %sp,_movemo_output+60
 move.l _movemo_real_sp,%sp
 move.l (%sp)+,%a6
 move.l (%sp)+,%a5
 move.l (%sp)+,%a4
 move.l (%sp)+,%a3
 move.l (%sp)+,%a2
 move.l (%sp)+,%d7
 move.l (%sp)+,%d6
 move.l (%sp)+,%d5
 move.l (%sp)+,%d4
 move.l (%sp)+,%d3
 move.l (%sp)+,%d2
 move.w _movemo_saved_sr,%sr
 rts
