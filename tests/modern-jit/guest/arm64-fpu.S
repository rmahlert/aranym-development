/* SPDX-License-Identifier: GPL-2.0-or-later */
.text
.globl _arm64_fpu_checks
_arm64_fpu_checks:
    movem.l %d2-%d3,-(%sp)
    move.w %sr,-(%sp)
    move.l #2000,%d3
.loop:
    move.w #0x14,%ccr
    fmove.l #6,%fp0
    fmove.l #2,%fp1
    fdiv.x %fp1,%fp0
    fmove.l %fp0,%d0
    move.w %ccr,%d1
    andi.w #0x1f,%d1
    cmpi.w #0x14,%d1
    bne .bad_flags
    cmpi.l #3,%d0
    bne .bad_result
    dbra %d3,.loop
    moveq #0,%d0
    bra .done
.bad_flags:
    moveq #1,%d0
    bra .done
.bad_result:
    moveq #2,%d0
.done:
    move.w (%sp)+,%sr
    movem.l (%sp)+,%d2-%d3
    rts
