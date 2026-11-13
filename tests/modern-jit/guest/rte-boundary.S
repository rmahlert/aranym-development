/* SPDX-License-Identifier: GPL-2.0-or-later */
.text
.even
.globl _rb_execute
_rb_execute:
 ori.w #0x0700,%sr
 move.l %sp,rb_old_sp
 move.l 0x04fffff8,rb_tail
 move.l 0x04fffffc,rb_tail+4
 movec %vbr,%a0
 move.l 8(%a0),rb_old_vector
 move.l %a0,rb_vbr
 move.l #rb_handler,8(%a0)
 move.w #0x2700,0x04fffff8
 move.l #rb_return,0x04fffffa
#ifdef RTE_BOUNDARY_CONTROL
 move.w #0x0000,0x04fffffe
#else
 move.w #0x7008,0x04fffffe
#endif
 lea 0x04fffff8,%sp
.globl _rb_rte_instruction
_rb_rte_instruction:
 rte
rb_return:
 bsr rb_restore
 jmp _rb_success
rb_handler:
 bsr rb_restore
 jmp _rb_recursive
rb_restore:
 /* Pop our local BSR return before replacing the guest stack. */
 move.l (%sp)+,%a1
 move.l rb_old_sp,%sp
 move.l rb_tail,0x04fffff8
 move.l rb_tail+4,0x04fffffc
 move.l rb_vbr,%a0
 move.l rb_old_vector,8(%a0)
 jmp (%a1)
.bss
.even
rb_old_sp: .space 4
rb_old_vector: .space 4
rb_vbr: .space 4
rb_tail: .space 8
