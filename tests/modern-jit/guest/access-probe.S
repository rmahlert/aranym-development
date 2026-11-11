/* SPDX-License-Identifier: GPL-2.0-or-later
 * Exact absent-SFP004 write from the laptop report. A real guest bus handler
 * inspects the format7 frame then discards it, like a hardware-presence probe.
 */
.text
.even
.globl _access_probe,_access_probe_next,_access_guard_probe,_guard_write_begin,_guard_write_end,_access_guard_next
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
.bss
.even
probe_ram:
    .space 4
