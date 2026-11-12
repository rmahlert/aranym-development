/* SPDX-License-Identifier: GPL-2.0-or-later
 * MOVE-only capture. First data fault changes the live EA to mapped RAM and
 * arms one code-fetch fault; both real RTEs must retain the saved device EA.
 */
.text
.even
.macro snapshot slot
 move.l %d0,_movemx_fault_regs+(64*\slot)
 move.l %d1,_movemx_fault_regs+(64*\slot)+4
 move.l %d2,_movemx_fault_regs+(64*\slot)+8
 move.l %d3,_movemx_fault_regs+(64*\slot)+12
 move.l %d4,_movemx_fault_regs+(64*\slot)+16
 move.l %d5,_movemx_fault_regs+(64*\slot)+20
 move.l %d6,_movemx_fault_regs+(64*\slot)+24
 move.l %d7,_movemx_fault_regs+(64*\slot)+28
 move.l %a0,_movemx_fault_regs+(64*\slot)+32
 move.l %a1,_movemx_fault_regs+(64*\slot)+36
 move.l %a2,_movemx_fault_regs+(64*\slot)+40
 move.l %a3,_movemx_fault_regs+(64*\slot)+44
 move.l %a4,_movemx_fault_regs+(64*\slot)+48
 move.l %a5,_movemx_fault_regs+(64*\slot)+52
 move.l %a6,_movemx_fault_regs+(64*\slot)+56
 move.l %sp,_movemx_fault_regs+(64*\slot)+60
 .irp offset,0,4,8,12,16,20,24,28,32,36,40,44,48,52,56
 move.l \offset(%sp),_movemx_frames+(60*\slot)+\offset
 .endr
 move.l 0x00fec008,_movemx_native+(4*\slot)
 move.l 0x00fed284,_movemx_bus_counts+(4*\slot)
 move.l 0x00fed2b8,_movemx_fetch_counts+(4*\slot)
.endm
.globl _movemx_handler
_movemx_handler:
 cmpi.l #2,_movemx_fault_count
 bhs 3f
 tst.l _movemx_fault_count
 bne 1f
 snapshot 0
 addq.l #1,_movemx_fault_count
 move.l _movemx_live_pointer,_movemo_pointer
 move.l _movemx_live_a2,%a2
 move.l #1,0x00fed2b4
 bra 2f
1:
 snapshot 1
 addq.l #1,_movemx_fault_count
2:
 rte
3:
 jsr _movemx_repeat_fail
 illegal
