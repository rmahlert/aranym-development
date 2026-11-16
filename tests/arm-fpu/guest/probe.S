/* SPDX-License-Identifier: GPL-2.0-or-later */
.text
.globl _fpu_probe
_fpu_probe:
    movem.l %d2-%d7/%a2-%a6,-(%sp)
    move.l 48(%sp),%d0
    move.l 52(%sp),%a0
    move.l 56(%sp),%a1
    fmove.l %d0,%fpcr
    fmove.l #0,%fpsr
    fmove.l #0x12345678,%fpiar
    fmovem.x (%a0),%fp0-%fp7
.macro sample instruction, source, destination
    moveq #0,%d1
    move.w #0x14,%ccr
    \instruction \source,\destination
    move.w %ccr,%d1
    fmove.x \destination,(%a1)+
    fmove.l %fpsr,(%a1)+
    move.l %d1,(%a1)+
.endm
    sample fadd.x,%fp1,%fp0
    sample fsub.x,%fp2,%fp0
    sample fmul.x,%fp3,%fp0
    sample fdiv.x,%fp4,%fp0
    sample fsqrt.x,%fp5,%fp6
    sample fabs.x,%fp7,%fp7
    sample fneg.x,%fp1,%fp1
    sample fmove.x,%fp6,%fp2
    sample fintrz.x,%fp0,%fp6
    sample fint.x,%fp0,%fp6
    sample fsglmul.x,%fp4,%fp3
    sample fsgldiv.x,%fp4,%fp3
    sample fsadd.x,%fp1,%fp0
    sample fdsub.x,%fp2,%fp0
    sample fsin.x,%fp0,%fp7
    sample flogn.x,%fp5,%fp5
    sample frem.x,%fp2,%fp4
    sample fmod.x,%fp2,%fp4
    sample fscale.x,%fp1,%fp6
    sample fgetexp.x,%fp3,%fp2
    sample fgetman.x,%fp3,%fp2
    sample fsmove.x,%fp1,%fp0
    sample fdmove.x,%fp1,%fp0
    sample fssqrt.x,%fp1,%fp0
    sample fdsqrt.x,%fp1,%fp0
    sample fsabs.x,%fp1,%fp0
    sample fdabs.x,%fp1,%fp0
    sample fsneg.x,%fp1,%fp0
    sample fdneg.x,%fp1,%fp0
    sample fsdiv.x,%fp1,%fp0
    sample fddiv.x,%fp1,%fp0
    sample fsadd.x,%fp1,%fp0
    sample fdadd.x,%fp1,%fp0
    sample fsmul.x,%fp1,%fp0
    sample fdmul.x,%fp1,%fp0
    sample fssub.x,%fp1,%fp0
    sample fdsub.x,%fp1,%fp0
    fmove.l #0x005a88f8,%fpsr
    move.w #0x14,%ccr
    fcmp.x %fp3,%fp0
    fmove.l %fpsr,(%a1)+
    fmove.l #0x005a88f8,%fpsr
    move.w #0x14,%ccr
    ftst.x %fp7
    fmove.l %fpsr,(%a1)+
    fmovem.x %fp0-%fp7,(%a1)
    lea 96(%a1),%a1
    fmove.l %fpcr,(%a1)+
    fmove.l %fpsr,(%a1)+
    fmove.l %fpiar,(%a1)+
    fmove.l #0,%fpcr
    movem.l (%sp)+,%d2-%d7/%a2-%a6
    rts

.globl _fpu_benchmark
_fpu_benchmark:
    fmove.l #0,%fpcr
    fmove.l #3,%fp0
    fmove.l #2,%fp1
    fmove.l #1,%fp2
    fmove.l #0,%fp3
    move.l 4(%sp),%d0
1:  fmul.x %fp1,%fp0
    fadd.x %fp2,%fp0
    fsub.x %fp2,%fp0
    fdiv.x %fp1,%fp0
    fadd.x %fp2,%fp3
    subq.l #1,%d0
    bne 1b
    fmove.l %fp3,%d1
    move.l %d1,_fpu_benchmark_iterations
    fmove.l %fp0,%d0
    rts
.data
.balign 4
.globl _fpu_benchmark_iterations
_fpu_benchmark_iterations: .long 0
.text

.globl _fpu_rom_probe
_fpu_rom_probe:
    move.l 4(%sp),%d0
    move.l 8(%sp),%a0
    fmove.l %d0,%fpcr
    fmove.l #0,%fpsr
.macro rom code
    .word 0xf200,0x5c00+\code
    fmove.x %fp0,(%a0)+
    fmove.l %fpsr,(%a0)+
.endm
    rom 0x00
    rom 0x0b
    rom 0x0c
    rom 0x0d
    rom 0x0e
    rom 0x0f
    rom 0x30
    rom 0x31
    rom 0x32
    rom 0x33
    rom 0x34
    rom 0x35
    rom 0x36
    rom 0x37
    rom 0x38
    rom 0x39
    rom 0x3a
    rom 0x3b
    rom 0x3c
    rom 0x3d
    rom 0x3e
    rom 0x3f
    fmove.l #0,%fpcr
    rts

