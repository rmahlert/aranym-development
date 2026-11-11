/* SPDX-License-Identifier: GPL-2.0-or-later
 * Exact absent-SFP004 write from the laptop report. A real guest bus handler
 * inspects the format7 frame then discards it, like a hardware-presence probe.
 */
.text
.even
.globl _access_probe,_access_probe_next,_access_guard_probe,_guard_write_begin,_guard_write_end,_access_guard_next
.globl _access_read_restart_probe,_read_fault_instruction
_access_probe:
    movem.l %d2-%d7/%a2-%a6,-(%sp)
    move.w %sr,-(%sp)
    ori.w #0x0700,%sr
    move.l %sp,_probe_saved_sp
    move.l 8,_probe_old_vector
    move.l #probe_handler,8
    move.w #3,0xfffffa42
_access_probe_next:
    moveq #1,%d0
probe_return:
    move.l _probe_old_vector,8
    move.w (%sp)+,%sr
    movem.l (%sp)+,%d2-%d7/%a2-%a6
    rts
_access_guard_probe:
    movem.l %d2-%d7/%a2-%a6,-(%sp)
    move.w %sr,-(%sp)
    ori.w #0x0700,%sr
    move.l %sp,_probe_saved_sp
    move.l 8,_probe_old_vector
    move.l #probe_handler,8
    move.w #127,%d2
1:  pea probe_ram
    bsr _guard_write_begin
    addq.l #4,%sp
    dbra %d2,1b
    move.l 0x00fed050,_probe_native_writes
    pea 0xfffffa42
    bsr _guard_write_begin
    addq.l #4,%sp
    moveq #1,%d0
    bra probe_return
_guard_write_begin:
    move.l 4(%sp),%a0
    moveq #3,%d0
    move.w %d0,(%a0)
_access_guard_next:
    rts
_guard_write_end:
probe_handler:
    addq.l #1,_probe_count
    move.w (%sp),_probe_sr
    move.l 2(%sp),_probe_pc
    move.w 6(%sp),_probe_format
    move.w 12(%sp),_probe_ssw
    move.w 14(%sp),_probe_wb3s
    move.l 20(%sp),_probe_fault
    move.l 24(%sp),_probe_wb3a
    move.l 28(%sp),_probe_wb3d
    move.l _probe_saved_sp,%sp
    moveq #0,%d0
    bra probe_return
_access_read_restart_probe:
    movem.l %d2-%d7/%a2-%a6,-(%sp)
    move.w %sr,-(%sp)
    ori.w #0x0700,%sr
    move.l %sp,_read_saved_sp
    move.l %usp,%a0
    move.l %a0,_read_saved_usp
    move.l 8,_read_old_vector
    move.l 188,_read_old_trap
    move.l #read_fault_handler,8
    move.l #read_return_handler,188
    lea read_user_stack_end,%a0
    move.l %a0,%usp
    move.w #0,-(%sp)
    pea read_user_start
    move.w #0,-(%sp)
    rte
read_user_start:
    suba.l %a0,%a0
    move.l #0x12345678,%d1
    move.w #0,%ccr
_read_fault_instruction:
    move.b (%a0),%d1
    move.w %ccr,_read_reply
    move.l %d1,_read_result
    trap #15
read_fault_handler:
    addq.l #1,_read_count
    move.w (%sp),_read_sr
    move.l 2(%sp),_read_pc
    move.w 6(%sp),_read_format
    move.w 12(%sp),_read_ssw
    move.w 14(%sp),_read_wb3s
    move.l 20(%sp),_read_fault
    lea read_value,%a0
    rte
read_return_handler:
    move.l _read_saved_sp,%sp
    move.l _read_old_vector,8
    move.l _read_old_trap,188
    move.l _read_saved_usp,%a0
    move.l %a0,%usp
    moveq #0,%d0
    move.w (%sp)+,%sr
    movem.l (%sp)+,%d2-%d7/%a2-%a6
    rts
.data
read_value:
    .byte 0x80
.bss
.even
probe_ram:
    .space 4
read_user_stack:
    .space 128
read_user_stack_end:
