/* SPDX-License-Identifier: GPL-2.0-or-later */
.text
.even
.macro probe name,insn
.globl \name
\name:
    movem.l %d2-%d7/%a2-%a6,-(%sp)
    move.l %sp,_bus_before_sp
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
    move.l _bus_pointer,%a6
    move.l _bus_input,%d7
    move.w _bus_ccr,%ccr
    \insn
    move.w %ccr,_bus_reply_ccr
    movem.l %d0-%d7/%a0-%a7,_bus_registers
    movem.l (%sp)+,%d2-%d7/%a2-%a6
    rts
.endm
probe _bus_read_b,"move.b (%a6),%d7"
probe _bus_read_w,"move.w (%a6),%d7"
probe _bus_read_l,"move.l (%a6),%d7"
probe _bus_write_b,"move.b %d7,(%a6)"
probe _bus_write_w,"move.w %d7,(%a6)"
probe _bus_write_l,"move.l %d7,(%a6)"
