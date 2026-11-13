/* SPDX-License-Identifier: GPL-2.0-or-later
 * Warm the same RAM-store PCs used to edit a live format7 header. The store
 * helper changes then restores PC, so a raw final-byte signature misses it.
 */
.text
.even
.globl _nfb_store,_nfb_begin,_nfb_end,_nfb_patch_target,_nfb_next_target
.globl _nfb_trigger,_nfb_vbr
_nfb_vbr:
 movec %vbr,%d0
 rts
_nfb_begin:
_nfb_store:
 move.l 4(%sp),%a0
 move.l 8(%sp),%d0
_nfb_patch_target:
 move.l %d0,2(%a0)
 rts
_nfb_trigger:
 movem.l %d2-%d7/%a2-%a6,-(%sp)
 move.w %sr,-(%sp)
 ori.w #0x0700,%sr
 move.l #_nfb_next_target,0x00fec008
 move.l 0x00fec008,_nfb_next_warm_before
 moveq #63,%d2
1:
 lea _nfb_warm,%a0
 lea _nfb_marker,%a1
 move.l #0x12345678,%d0
 bsr nfb_probe
 dbra %d2,1b
 move.l 0x00fec008,_nfb_next_warm_after
 movec %vbr,%d0
 move.l %d0,%a2
 move.l 8(%a2),_nfb_old_vector
 move.l #nfb_handler,8(%a2)
 move.l #0,0x00fed280
 move.l #0,0x00fed2a4
 lea _nfb_marker,%a1
 clr.l (%a1)
 move.l #0x00fed200,%a0
 move.l #0x12345678,%d0
 bsr nfb_probe
 move.l 0x00fec008,_nfb_next_after
 movec %vbr,%d0
 move.l %d0,%a2
 move.l _nfb_old_vector,8(%a2)
 move.w (%sp)+,%sr
 movem.l (%sp)+,%d2-%d7/%a2-%a6
 rts
nfb_probe:
 move.l %d0,(%a0)
_nfb_next_target:
 move.l %d0,(%a1)
 rts
nfb_handler:
 movem.l %d0-%d7/%a0-%a6,-(%sp)
 lea 60(%sp),%a2
 addq.l #1,_nfb_faults
 move.w 6(%a2),_nfb_format
 move.w 12(%a2),_nfb_ssw
 move.l 2(%a2),_nfb_pc_before
 move.l #_nfb_patch_target,0x00fec008
 move.l 0x00fec008,_nfb_patch_before
 move.l 2(%a2),%d0
 eori.l #2,%d0
 move.l %d0,-(%sp)
 move.l %a2,-(%sp)
 jsr _nfb_store
 addq.l #8,%sp
 move.l _nfb_pc_before,-(%sp)
 move.l %a2,-(%sp)
 jsr _nfb_store
 addq.l #8,%sp
 move.l 0x00fec008,_nfb_patch_after
 move.l 2(%a2),_nfb_pc_after
 move.l 24(%a2),%a0
 move.l 28(%a2),(%a0)
 clr.w 14(%a2)
 move.l #_nfb_next_target,0x00fec008
 move.l 0x00fec008,_nfb_next_before
 movem.l (%sp)+,%d0-%d7/%a0-%a6
 rte
_nfb_end:
