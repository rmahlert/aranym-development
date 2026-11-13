/* SPDX-License-Identifier: GPL-2.0-or-later */
.text
.even
.globl _iarb_probe,_iarb_begin,_iarb_end,_iarb_next,_iarb_vbl
_iarb_begin:
_iarb_probe:
 movem.l %d2-%d7/%a2-%a6,-(%sp)
 move.w %sr,-(%sp)
 ori.w #0x0700,%sr
 move.l 0x00fec008,_iarb_warm_before
 lea _iarb_marker,%a1
 moveq #1,%d0
 moveq #127,%d2
1:
 jsr _iarb_next
 dbra %d2,1b
 move.l 0x00fec008,_iarb_warm_after
 clr.l _iarb_marker
 movec %vbr,%d0
 move.l %d0,%a2
 move.l 0x70(%a2),_iarb_old_vbl
 move.l #_iarb_vbl,0x70(%a2)
 moveq #0,%d0
 move.b 0xfffffa17,%d0
 andi.l #0xf0,%d0
 addq.l #5,%d0
 lsl.l #2,%d0
 move.l %d0,_iarb_mfp_vector
 move.l 0(%a2,%d0.l),_iarb_old_mfp
 move.l #iarb_mfp,0(%a2,%d0.l)
 move.b 0xfffffa13,_iarb_old_imra
 move.b 0xfffffa15,_iarb_old_imrb
 move.b 0xfffffa07,_iarb_old_iera
 move.b 0xfffffa09,_iarb_old_ierb
 move.b 0xfffffa1d,_iarb_old_tcdcr
 move.b 0xfffffa23,_iarb_old_tcdata
 clr.b 0xfffffa13
 clr.b 0xfffffa15
 clr.b 0xfffffa07
 move.b #0x20,0xfffffa09
 /* Use a known 5ms TimerC source. Six separately observed pending events
  * guarantee at least one real 20ms VBL request under the same IPL7 mask. */
 move.b _iarb_old_tcdcr,%d0
 andi.b #15,%d0
 move.b %d0,0xfffffa1d
 move.b #192,0xfffffa23
 ori.b #0x50,%d0
 move.b %d0,0xfffffa1d
 moveq #5,%d2
2:
 move.b #0xdf,0xfffffa0d
 move.l #10000000,%d0
3:
 btst #5,0xfffffa0d
 bne 4f
 subq.l #1,%d0
 bne 3b
 move.l #1,_iarb_timeout
 bra 7f
4:
 addq.l #1,_iarb_ticks
 dbra %d2,2b
 move.b 0xfffffa0d,_iarb_pending
 move.w #0x2000,_iarb_release_sr
 move.l _iarb_sources,%d0
 btst #0,%d0
 bne 5f
 move.w #0x2400,_iarb_release_sr
5:
 btst #1,%d0
 beq 6f
 move.b #0x20,0xfffffa15
6:
 move.l 0x00fec008,_iarb_before
 lea _iarb_marker,%a1
 moveq #1,%d0
 bsr iarb_release
 move.l 0x00fec008,_iarb_after
7:
 ori.w #0x0700,%sr
 clr.b 0xfffffa13
 clr.b 0xfffffa15
 movec %vbr,%d0
 move.l %d0,%a2
 move.l _iarb_old_vbl,0x70(%a2)
 move.l _iarb_mfp_vector,%d0
 move.l _iarb_old_mfp,0(%a2,%d0.l)
 move.b _iarb_old_tcdcr,%d0
 andi.b #15,%d0
 move.b %d0,0xfffffa1d
 move.b _iarb_old_tcdata,0xfffffa23
 move.b _iarb_old_tcdcr,0xfffffa1d
 move.b _iarb_old_iera,0xfffffa07
 move.b _iarb_old_ierb,0xfffffa09
 move.b _iarb_old_imra,0xfffffa13
 move.b _iarb_old_imrb,0xfffffa15
 move.w (%sp)+,%sr
 movem.l (%sp)+,%d2-%d7/%a2-%a6
 rts
iarb_release:
 move.l %sp,_iarb_base_sp
 move.w _iarb_release_sr,%sr
_iarb_next:
 move.l %d0,(%a1)
 rts

/* Save the original GPRs before using any scratch register. The saved
 * exception frame is then exactly 60 bytes above this local save area. */
.macro record_irq level
 ori.w #0x0700,%sr
 movem.l %d0-%d7/%a0-%a6,-(%sp)
 move.l _iarb_count,%d0
 cmpi.l #4,%d0
 bcc 9f
 lsl.l #2,%d0
 lea _iarb_source,%a0
 move.l #\level,0(%a0,%d0.l)
 lea _iarb_pc,%a0
 move.l 62(%sp),0(%a0,%d0.l)
 lea _iarb_sr,%a0
 moveq #0,%d1
 move.w 60(%sp),%d1
 move.l %d1,0(%a0,%d0.l)
 lea _iarb_sp,%a0
 lea 60(%sp),%a1
 move.l %a1,0(%a0,%d0.l)
 lea _iarb_at_marker,%a0
 move.l _iarb_marker,0(%a0,%d0.l)
9:
 addq.l #1,_iarb_count
.endm
_iarb_vbl:
 record_irq 4
 /* This is the lowest selected source. Prevent a later periodic VBL from
  * perturbing observations while the disposable guest restores its vectors. */
 ori.w #0x0700,60(%sp)
 movem.l (%sp)+,%d0-%d7/%a0-%a6
 rte
iarb_mfp:
 record_irq 6
 clr.b 0xfffffa15
 move.b #0xdf,0xfffffa11
 move.l _iarb_sources,%d0
 btst #0,%d0
 bne 8f
 ori.w #0x0700,60(%sp)
8:
 movem.l (%sp)+,%d0-%d7/%a0-%a6
 rte
_iarb_end:
