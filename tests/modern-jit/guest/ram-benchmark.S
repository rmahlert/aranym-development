/* SPDX-License-Identifier: GPL-2.0-or-later */
.text
.even
.macro begin name
.globl \name
\name:
    movem.l %d2-%d7/%a2-%a6,-(%sp)
    move.l 48(%sp),%d7
    subq.l #1,%d7
    moveq #0,%d5
.endm
.macro end
    move.l %d5,%d0
    movem.l (%sp)+,%d2-%d7/%a2-%a6
    rts
.endm
begin _ram_scalar
1:  lea _ram_source,%a0
    lea _ram_dest,%a1
    move.w #4095,%d6
2:
.rept 4
    move.l (%a0)+,%d0
    move.l %d0,(%a1)+
.endr
    dbra %d6,2b
    addq.l #1,%d5
    dbra %d7,1b
end
begin _ram_memory
1:  lea _ram_source,%a0
    lea _ram_dest,%a1
    move.w #4095,%d6
2:
.rept 4
    move.l (%a0)+,(%a1)+
.endr
    dbra %d6,2b
    addq.l #1,%d5
    dbra %d7,1b
end
begin _ram_fill
1:  lea _ram_dest,%a1
    move.l #0x76543210,%d0
    move.w #4095,%d6
2:
.rept 4
    move.l %d0,(%a1)+
.endr
    dbra %d6,2b
    addq.l #1,%d5
    dbra %d7,1b
end
