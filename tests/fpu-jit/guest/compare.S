/* SPDX-License-Identifier: GPL-2.0-or-later */
.text
.globl _fpu_compare_probe
_fpu_compare_probe:
 movem.l %d2/%a2,-(%sp)
 move.l 12(%sp),%a0
 move.l 16(%sp),%a1
 move.l 20(%sp),%d0
 move.l 24(%sp),%d2
 fmove.l %d0,%fpcr
 fmovem.x (%a0),%fp0-%fp1
 fmove.l #0x005a00f8,%fpsr
 moveq #0,%d0
 move.w %d2,%ccr
 fcmp.x %fp1,%fp0
 move.w %ccr,%d0
 fmove.l %fpsr,(%a1)+
 fmove.l %fpiar,(%a1)+
 move.l %d0,(%a1)+
 fmovem.x %fp0-%fp1,(%a1)
 fmove.l #0,%fpcr
 movem.l (%sp)+,%d2/%a2
 rts
