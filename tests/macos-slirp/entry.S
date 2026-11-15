/* Owned standalone TOS networking test, no guest disk or C runtime. GPL-2.0-or-later. */
        .text
        .globl _start
_start:
        pea _guest_main(%pc)
        move.w #38,-(%sp)
        trap #14
        addq.l #6,%sp
        clr.w -(%sp)
        trap #1
        .globl _nf_id
_nf_id:
        .word 0x7300
        rts
        .globl _nf_call
_nf_call:
        .word 0x7301
        rts
