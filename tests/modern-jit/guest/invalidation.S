/* SPDX-License-Identifier: GPL-2.0-or-later */
.text
.even
.globl _jit_value,_jit_flush,_jit_patch
_jit_value:
    moveq #1,%d0
    rts
_jit_flush:
    cpusha %ic
    rts

_jit_patch:
    move.w 6(%sp),_jit_value
    rts
