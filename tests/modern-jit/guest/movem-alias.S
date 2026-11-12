/* SPDX-License-Identifier: GPL-2.0-or-later
 * Only the target uses MOVEM. Qualified individual MOVEs perform setup/capture
 * so a common MOVEM load/store defect cannot cancel inside the oracle.
 */
.text
.even
.globl _movema_begin,_movema_end
_movema_begin:
.macro probe name,opcode,mask
.globl _movema_\name,_movema_target_\name
_movema_\name:
 move.w %sr,_movema_saved_sr
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
 move.l %sp,_movema_real_sp
 move.l _movema_input,%d0
 move.l _movema_input+4,%d1
 move.l _movema_input+8,%d2
 move.l _movema_input+12,%d3
 move.l _movema_input+16,%d4
 move.l _movema_input+20,%d5
 move.l _movema_input+24,%d6
 move.l _movema_input+28,%d7
 move.l _movema_input+32,%a0
 move.l _movema_input+36,%a1
 move.l _movema_input+40,%a2
 move.l _movema_input+44,%a3
 move.l _movema_input+48,%a4
 move.l _movema_input+52,%a5
 move.l _movema_input+56,%a6
 move.l _movema_input+60,%sp
 move.w _movema_ccr,%ccr
_movema_target_\name:
 .word \opcode,\mask
 move.w %ccr,_movema_reply
 move.l %d0,_movema_output
 move.l %d1,_movema_output+4
 move.l %d2,_movema_output+8
 move.l %d3,_movema_output+12
 move.l %d4,_movema_output+16
 move.l %d5,_movema_output+20
 move.l %d6,_movema_output+24
 move.l %d7,_movema_output+28
 move.l %a0,_movema_output+32
 move.l %a1,_movema_output+36
 move.l %a2,_movema_output+40
 move.l %a3,_movema_output+44
 move.l %a4,_movema_output+48
 move.l %a5,_movema_output+52
 move.l %a6,_movema_output+56
 move.l %sp,_movema_output+60
 move.l _movema_real_sp,%sp
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
 move.w _movema_saved_sr,%sr
 rts
.endm
probe w2,0x48a2,0x0030
probe l2,0x48e2,0x0030
probe w3,0x48a2,0x0031
probe l3,0x48e2,0x0031
probe wa,0x48a2,0xffff
probe la,0x48e2,0xffff
probe pw2,0x4c9a,0x0c00
probe pl2,0x4cda,0x0c00
probe pw3,0x4c9a,0x8c00
probe pl3,0x4cda,0x8c00
probe pwa,0x4c9a,0xffff
probe pla,0x4cda,0xffff
_movema_end:
