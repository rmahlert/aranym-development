/* SPDX-License-Identifier: GPL-2.0-or-later */
.text
.globl _fpu_guard_benchmark
_fpu_guard_benchmark:
 fmove.l #0,%fpcr
 fmove.l #3,%fp0
 fmove.l #2,%fp1
 fmove.l #1,%fp2
 fmove.l #0,%fp3
 move.l 4(%sp),%d0
1:
 fmul.x %fp1,%fp0
 fadd.x %fp2,%fp0
 fsub.x %fp2,%fp0
 fdiv.x %fp1,%fp0
 fadd.x %fp2,%fp3
 subq.l #1,%d0
 bne 1b
 fmove.l %fp3,_guard_iterations
 fmove.l %fp0,%d0
 rts
