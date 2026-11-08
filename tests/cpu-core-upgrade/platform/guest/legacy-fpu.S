/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef LEGACY_FPU_EXPECT_SUM
#define LEGACY_FPU_EXPECT_SUM 11
#endif
.text
.globl _legacy_fpu_checks
_legacy_fpu_checks:
    movem.l %d2-%d7/%a2-%a6,-(%sp)
    move.w %sr,-(%sp)
    fmove.l %fpcr,-(%sp)
    fmovem.x %fp0-%fp3,-(%sp)
    move.l #2,%d7
    fmove.l #0,%fpcr
    fmove.l #7,%fp0
    fmove.l #5,%fp1
    fmul.l #3,%fp1
    fadd.x %fp1,%fp0
    fdiv.l #2,%fp0
    fmove.l %fp0,%d0
    cmp.l #LEGACY_FPU_EXPECT_SUM,%d0
    bne .done
    move.l #3,%d7
    fmove.d .threehalf,%fp0
    fmove.l #0x10,%fpcr /* round toward zero */
    fmove.l %fp0,%d0
    cmp.l #3,%d0
    bne .done
    fmove.l %fpcr,%d0
    cmp.l #0x10,%d0
    bne .done
    fmove.l #0,%fpcr
    fmove.l %fp0,%d0
    cmp.l #4,%d0
    bne .done
    move.l #4,%d7
    fmove.d %fp0,-(%sp)
    cmp.l #0x400c0000,(%sp)
    bne .bad_double
    tst.l 4(%sp)
    bne .bad_double
    addq.l #8,%sp
    move.l #5,%d7
    move.l %sp,%a2
    fsave -(%sp)
    cmp.l #0x41000000,(%sp) /* real 040 idle frame */
    bne .bad_save
    frestore (%sp)+
    cmpa.l %sp,%a2
    bne .done
    move.l #6,%d7
    move.l 0x1c,%a3
    move.l #.trap_handler,0x1c
    clr.l .trap_error
    move.w #0x271f,%sr
.trap_instruction:
    ftrapt
.trap_return:
    move.l %a3,0x1c
    move.l .trap_error,%d7
    bra .done
.bad_double:
    addq.l #8,%sp
    bra .done
.bad_save:
    move.l %a2,%sp
.done:
    fmovem.x (%sp)+,%fp0-%fp3
    fmove.l (%sp)+,%fpcr
    move.w (%sp)+,%sr
    move.l %d7,%d0
    movem.l (%sp)+,%d2-%d7/%a2-%a6
    rts
.trap_handler:
    movem.l %d0/%a0,-(%sp)
    cmp.w #0x271f,8(%sp)
    bne .trap_bad
    cmp.w #0x201c,14(%sp)
    bne .trap_bad
    move.l 10(%sp),%d0
    cmp.l #.trap_return,%d0
    bne .trap_bad
    move.l 16(%sp),%d0
    cmp.l #.trap_instruction,%d0
    bne .trap_bad
    bra .trap_done
.trap_bad:
    move.l #6,.trap_error
.trap_done:
    movem.l (%sp)+,%d0/%a0
    rte
.data
.balign 4
.threehalf: .long 0x400c0000,0x00000000
.trap_error: .long 0
