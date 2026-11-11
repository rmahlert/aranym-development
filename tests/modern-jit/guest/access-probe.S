/* SPDX-License-Identifier: GPL-2.0-or-later
 * Exact absent-SFP004 write from the laptop report. A real guest bus handler
 * inspects the format7 frame then discards it, like a hardware-presence probe.
 */
.text
.even
.globl _access_probe,_access_probe_next
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
