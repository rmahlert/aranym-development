/* SPDX-License-Identifier: GPL-2.0-or-later */
.text
.even
.globl _probe,_probe_fault,_probe_access,_probe_handler
_probe:
 movem.l %d2-%d7/%a2-%a6,-(%sp)
 move.w %sr,-(%sp)
 ori.w #0x0700,%sr
 move.l %sp,_saved_sp
 move.l 8,_old_vector
 move.l #_probe_handler,8
 lea value,%a0
 move.w #127,%d2
1: bsr leaf
 dbra %d2,1b
 move.l _probe_address,%a0
 movem.l %d0-%d7/%a0-%a6,_expected_registers
 move.w #0x2710,%sr
 bsr leaf
 moveq #1,%d0
 bra finish
leaf:
 move.l #0x13579bdf,%d1
_probe_fault:
#ifdef WITH_NOP
 nop
#endif
_probe_access:
 move.l (%a0),%d0
#ifdef WITH_NOP
 nop
#endif
 addq.l #1,%d1
 rts
_probe_handler:
 movem.l %d0-%d7/%a0-%a6,_fault_registers
 move.l %sp,_fault_stack
 move.w (%sp),_fault_sr
 move.l 2(%sp),_fault_pc
 move.w 6(%sp),_fault_format
 move.w 12(%sp),_fault_ssw
 move.l 20(%sp),_fault_address
 move.l %d1,_fault_d1
 move.l _saved_sp,%sp
 moveq #0,%d0
finish:
 move.l _old_vector,8
 move.w (%sp)+,%sr
 movem.l (%sp)+,%d2-%d7/%a2-%a6
 rts
.data
value: .long 0x12345678
