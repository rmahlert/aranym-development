/* SPDX-License-Identifier: GPL-2.0-or-later */
.text
.even
.globl _ea_probe_begin,_ea_probe_end
_ea_probe_begin:
.macro probe name,insn
.globl \name
\name:
    movem.l %d2-%d7/%a2-%a6,-(%sp)
    move.l %sp,_ea_before_sp
    move.l #0x11110000,%d0
    move.l #0x11110001,%d1
    move.l #0x11110002,%d2
    move.l #0x11110003,%d3
    move.l #0x11110004,%d4
    move.l #0x11110005,%d5
    move.l #0x11110006,%d6
    move.l #0x22220000,%a0
    move.l #0x22220001,%a1
    move.l #0x22220002,%a2
    move.l #0x22220003,%a3
    move.l #0x22220004,%a4
    move.l #0x22220005,%a5
    move.l _ea_pointer,%a6
    move.l _ea_input,%d7
    move.w _ea_ccr,%ccr
    \insn
    move.w %ccr,_ea_reply_ccr
    movem.l %d0-%d7/%a0-%a7,_ea_registers
    movem.l (%sp)+,%d2-%d7/%a2-%a6
    rts
.endm
probe _ea_read_b_0,"move.b -32768(%a6),%d7"
probe _ea_read_w_0,"move.w -32768(%a6),%d7"
probe _ea_read_l_0,"move.l -32768(%a6),%d7"
probe _ea_write_b_0,"move.b %d7,-32768(%a6)"
probe _ea_write_w_0,"move.w %d7,-32768(%a6)"
probe _ea_write_l_0,"move.l %d7,-32768(%a6)"
probe _ea_movea_w_0,"movea.w -32768(%a6),%a1"
probe _ea_movea_l_0,"movea.l -32768(%a6),%a1"
probe _ea_alias_w_0,"movea.w -32768(%a6),%a6"
probe _ea_alias_l_0,"movea.l -32768(%a6),%a6"
probe _ea_read_b_1,"move.b -1(%a6),%d7"
probe _ea_read_w_1,"move.w -1(%a6),%d7"
probe _ea_read_l_1,"move.l -1(%a6),%d7"
probe _ea_write_b_1,"move.b %d7,-1(%a6)"
probe _ea_write_w_1,"move.w %d7,-1(%a6)"
probe _ea_write_l_1,"move.l %d7,-1(%a6)"
probe _ea_movea_w_1,"movea.w -1(%a6),%a1"
probe _ea_movea_l_1,"movea.l -1(%a6),%a1"
probe _ea_alias_w_1,"movea.w -1(%a6),%a6"
probe _ea_alias_l_1,"movea.l -1(%a6),%a6"
probe _ea_read_b_2,"move.b (%a6),%d7"
probe _ea_read_w_2,"move.w (%a6),%d7"
probe _ea_read_l_2,"move.l (%a6),%d7"
probe _ea_write_b_2,"move.b %d7,(%a6)"
probe _ea_write_w_2,"move.w %d7,(%a6)"
probe _ea_write_l_2,"move.l %d7,(%a6)"
probe _ea_movea_w_2,"movea.w (%a6),%a1"
probe _ea_movea_l_2,"movea.l (%a6),%a1"
probe _ea_alias_w_2,"movea.w (%a6),%a6"
probe _ea_alias_l_2,"movea.l (%a6),%a6"
probe _ea_read_b_3,"move.b 1(%a6),%d7"
probe _ea_read_w_3,"move.w 1(%a6),%d7"
probe _ea_read_l_3,"move.l 1(%a6),%d7"
probe _ea_write_b_3,"move.b %d7,1(%a6)"
probe _ea_write_w_3,"move.w %d7,1(%a6)"
probe _ea_write_l_3,"move.l %d7,1(%a6)"
probe _ea_movea_w_3,"movea.w 1(%a6),%a1"
probe _ea_movea_l_3,"movea.l 1(%a6),%a1"
probe _ea_alias_w_3,"movea.w 1(%a6),%a6"
probe _ea_alias_l_3,"movea.l 1(%a6),%a6"
probe _ea_read_b_4,"move.b 32767(%a6),%d7"
probe _ea_read_w_4,"move.w 32767(%a6),%d7"
probe _ea_read_l_4,"move.l 32767(%a6),%d7"
probe _ea_write_b_4,"move.b %d7,32767(%a6)"
probe _ea_write_w_4,"move.w %d7,32767(%a6)"
probe _ea_write_l_4,"move.l %d7,32767(%a6)"
probe _ea_movea_w_4,"movea.w 32767(%a6),%a1"
probe _ea_movea_l_4,"movea.l 32767(%a6),%a1"
probe _ea_alias_w_4,"movea.w 32767(%a6),%a6"
probe _ea_alias_l_4,"movea.l 32767(%a6),%a6"
_ea_probe_end:
.globl _ea_jit_query
_ea_jit_query:
moveq #-1,%d0
.word 0x713b
rts
