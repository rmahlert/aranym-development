/* SPDX-License-Identifier: GPL-2.0-or-later
 * Equal work, no wrap: four additions, one SUBQ counter, one BNE each iteration.
 * SUBQ never borrows here, so X=0 at each loop edge in all three arms.
 */
.text
.even
.macro bench name,insn
.globl \name
\name:
 movem.l %d2-%d7/%a2-%a6,-(%sp)
 move.l 48(%sp),%d7
 moveq #1,%d0
 moveq #0,%d1
 move.w #0,%ccr
1:
 \insn
 \insn
 \insn
 \insn
 subq.l #1,%d7
 bne 1b
 move.l %d1,%d0
 movem.l (%sp)+,%d2-%d7/%a2-%a6
 rts
.endm
bench _bench_addx,"addx.l %d0,%d1"
bench _bench_add,"add.l %d0,%d1"
bench _bench_addq,"addq.l #1,%d1"
