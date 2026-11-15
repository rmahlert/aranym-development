/* Owned firmware/guest/host shutdown smoke test. GPL-2.0-or-later. */
        .text
        .globl _start
_start:
        pea supervisor(%pc)
        move.w #38,-(%sp)       /* XBIOS Supexec(callback) */
        trap #14
        addq.l #6,%sp
        clr.w -(%sp)            /* Failure: return to TOS; host must not pass. */
        trap #1
supervisor:
        pea stderr_name(%pc)
        bsr.w nf_id
        addq.l #4,%sp
        tst.l %d0
        beq.w failure
        move.l %d0,%d7
        pea marker(%pc)
        move.l %d7,-(%sp)
        bsr.w nf_call
        addq.l #8,%sp
        pea shutdown_name(%pc)
        bsr.w nf_id
        addq.l #4,%sp
        tst.l %d0
        beq.w failure
        clr.l -(%sp)
        move.l %d0,-(%sp)
        bsr.w nf_call           /* NF_SHUTDOWN(0), must terminate host with 0 */
        addq.l #8,%sp
failure:
        moveq #1,%d0
        rts
nf_id:
        .word 0x7300
        rts
nf_call:
        .word 0x7301
        rts
stderr_name:
        .asciz "NF_STDERR"
shutdown_name:
        .asciz "NF_SHUTDOWN"
marker:
        .asciz "ARANYM FIRMWARE GUEST requesting clean shutdown\r\n"
        .even
