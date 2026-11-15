/* SPDX-License-Identifier: GPL-2.0-or-later */
        .text
        .even
        .globl _nf_id,_nf_call
_nf_id:
        .word 0x7300
        rts
_nf_call:
        .word 0x7301
        rts

        .macro sentinels
        move.l #0x11111111,d1
        move.l #0x22222222,d2
        move.l #0x33333333,d3
        move.l #0x44444444,d4
        move.l #0x55555555,d5
        move.l #0x66666666,d6
        move.l #0x77777777,d7
        move.l #0x01234560,a0
        move.l #0x11234561,a1
        move.l #0x21234562,a2
        move.l #0x31234563,a3
        move.l #0x41234564,a4
        move.l #0x51234565,a5
        move.l #0x61234566,a6
        move.l sp,_bridge_before_sp
        move.w #21,ccr
        .endm

        .macro capture
        move.w sr,_bridge_sr
        movem.l d0-d7/a0-a7,_bridge_regs
        .endm

        .macro nfprobe name,opcode
        .globl \name
\name:
        move.l 4(sp),d0
        movem.l d1-d7/a0-a6,-(sp)
        move.l d0,-(sp)
        clr.l -(sp)
        sentinels
        .word \opcode
        capture
        addq.l #8,sp
        movem.l (sp)+,d1-d7/a0-a6
        rts
        .endm

        nfprobe _nf_probe_id,0x7300
        nfprobe _nf_probe_call,0x7301

        .globl _trap_probe,_bridge_trap_handler
_trap_probe:
        movem.l d1-d7/a0-a6,-(sp)
        move.l #.Lresume,_bridge_expected_pc
        move.l #0x10203040,d0
        sentinels
        trap #15
.Lresume:
        capture
        movem.l (sp)+,d1-d7/a0-a6
        rts

_bridge_trap_handler:
        movem.l d0-d7/a0-a6,-(sp)
        move.w 60(sp),_bridge_trap_sr
        move.l 62(sp),_bridge_trap_pc
        move.w 66(sp),_bridge_trap_format
        lea 60(sp),a0
        move.l a0,_bridge_frame_sp
        tst.w _bridge_trap_mode
        beq .Lhandler_done
        move.l _bridge_resume_pc,62(sp)
        cmpi.w #2,_bridge_trap_mode
        beq .Lleave_master
        ori.w #0x2000,60(sp)
        bra .Lhandler_done
.Lleave_master:
        andi.w #0xefff,60(sp)
.Lhandler_done:
        movem.l (sp)+,d0-d7/a0-a6
        rte

        .macro user_entry target
        move.w sr,_bridge_saved_sr
        movem.l d1-d7/a0-a6,-(sp)
        move.l sp,_bridge_super_sp
        move.l usp,a0
        move.l a0,_bridge_saved_bank
        lea _bridge_user_stack+256,a0
        move.l a0,_bridge_bank_top
        move.l a0,usp
        subq.l #8,sp
        move.w #0x0715,(sp)
        move.l #\target,2(sp)
        clr.w 6(sp)
        rte
        .endm

        .macro restore_user
        capture
        move.l usp,a0
        move.l a0,_bridge_after_bank
        move.l _bridge_saved_bank,a0
        move.l a0,usp
        move.w _bridge_saved_sr,sr
        movem.l (sp)+,d1-d7/a0-a6
        rts
        .endm

        .globl _trap_user_probe,_trap_master_probe,_privilege_probe,_illegal_probe
_trap_user_probe:
        move.l #.Luser_next,_bridge_expected_pc
        move.l #.Luser_super_resume,_bridge_resume_pc
        user_entry .Luser_start
.Luser_start:
        move.l #0x10203040,d0
        sentinels
        trap #15
.Luser_next:
        bra .Lunexpected
.Luser_super_resume:
        restore_user

_privilege_probe:
        move.l #.Lpriv_instruction,_bridge_expected_pc
        move.l #.Lpriv_super_resume,_bridge_resume_pc
        user_entry .Lpriv_start
.Lpriv_start:
        move.l #0x10203040,d0
        sentinels
.Lpriv_instruction:
        move.w sr,d0
        bra .Lunexpected
.Lpriv_super_resume:
        restore_user

_trap_master_probe:
        move.w sr,_bridge_saved_sr
        movem.l d1-d7/a0-a6,-(sp)
        move.l sp,_bridge_super_sp
        movec msp,d0
        move.l d0,_bridge_saved_bank
        lea _bridge_master_stack+256,a0
        move.l a0,_bridge_bank_top
        movec a0,msp
        move.l #.Lmaster_next,_bridge_expected_pc
        move.l #.Lmaster_super_resume,_bridge_resume_pc
        move.w #0x3715,sr
        move.l #0x10203040,d0
        sentinels
        trap #15
.Lmaster_next:
        bra .Lunexpected
.Lmaster_super_resume:
        capture
        movec msp,d0
        move.l d0,_bridge_after_bank
        /* Preserve the measured A7 above even if RTE selected the wrong bank.
         * Recover only the fixture's caller stack so C can report a failure.
         */
        move.w #0x2700,sr
        move.l _bridge_super_sp,sp
        move.l _bridge_saved_bank,d0
        movec d0,msp
        move.w _bridge_saved_sr,sr
        movem.l (sp)+,d1-d7/a0-a6
        rts

_illegal_probe:
        move.w sr,_bridge_saved_sr
        movem.l d1-d7/a0-a6,-(sp)
        move.l #.Lillegal_instruction,_bridge_expected_pc
        move.l #.Lillegal_resume,_bridge_resume_pc
        move.w #0x2715,sr
        move.l #0x10203040,d0
        sentinels
.Lillegal_instruction:
        .word 0x4afc
        bra .Lunexpected
.Lillegal_resume:
        capture
        move.w _bridge_saved_sr,sr
        movem.l (sp)+,d1-d7/a0-a6
        rts

.Lunexpected:
        move.l #20,-(sp)
        move.l _bridge_exit_id,-(sp)
        jsr _nf_call
        bra .Lunexpected
