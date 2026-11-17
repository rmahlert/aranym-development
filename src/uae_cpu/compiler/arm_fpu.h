// SPDX-License-Identifier: GPL-2.0-or-later
#ifndef ARANYM_ARM_FPU_H
#define ARANYM_ARM_FPU_H
#include "fpu/core.h"
#include <cstring>

static_assert(sizeof(fpu_register)==8,"ARM native FPU requires the IEEE double register profile");
static void *popall_arm_fpu_deopt;
static uae_u32 arm_fpu_executed,arm_fpu_deoptimized;
static bool arm_fpu_diagnostics() {
    static const bool value=[](){const char *p=getenv("ARANYM_ARM_FPU_DIAGNOSTICS");return p&&strcmp(p,"1")==0;}();
    return value;
}
static struct arm_fpu_reporter {
    ~arm_fpu_reporter() {
        if(arm_fpu_diagnostics())
            fprintf(stderr,"ARM-FPU-JIT available=yes enabled=%s executed-native=%u deoptimized=%u precision=ieee64\n",
                bx_options.jit.jit&&bx_options.jit.jitfpu?"yes":"no",arm_fpu_executed,arm_fpu_deoptimized);
    }
} arm_fpu_report;
static int arm_fpu_operation(unsigned extra) {
    switch(extra&127) {
    case 0x00:case 0x40:case 0x44:return 0x00;
    case 0x04:case 0x41:case 0x45:return 0x04;
    case 0x18:case 0x58:case 0x5c:return 0x18;
    case 0x1a:case 0x5a:case 0x5e:return 0x1a;
    case 0x20:case 0x60:case 0x64:return 0x20;
    case 0x22:case 0x62:case 0x66:return 0x22;
    case 0x23:case 0x63:case 0x67:return 0x23;
    case 0x28:case 0x68:case 0x6c:return 0x28;
    case 0x38:return 0x38;
    case 0x3a:return 0x3a;
    default:return -1;
    }
}
static unsigned arm_fpu_word(const uae_u8 *pc) {return (unsigned(pc[0])<<8)|pc[1];}
static bool arm_fpu_candidate(const uae_u8 *pc) {
    if(!bx_options.jit.jitfpu || (arm_fpu_word(pc)&0xffc0)!=0xf200)return false;
    const unsigned extra=arm_fpu_word(pc+2);
    return (extra&0xe000)==0 && arm_fpu_operation(extra)>=0;
}
static bool arm_fpu_bounded(unsigned reg) {
    uae_u64 bits;memcpy(&bits,&fpu.registers[reg],8);
    const unsigned exponent=(bits>>52)&2047;
    return (exponent>=0x300 && exponent<=0x4ff) || (bits&0x7fffffffffffffffULL)==0;
}
static uae_u32 REGPARAM2 arm_fpu_guard(uae_u32 extra) {
    const unsigned source=(extra>>10)&7,destination=(extra>>7)&7;
    const int op=arm_fpu_operation(extra);
    bool safe=bx_options.jit.jitfpu && fpu.fpcr==0 && arm_fpu_bounded(source);
    if(op==0x20 || op==0x22 || op==0x23 || op==0x28 || op==0x38)
        safe=safe&&arm_fpu_bounded(destination);
    uae_u64 bits;memcpy(&bits,&fpu.registers[source],8);
    if(op==0x20)safe=safe&&(bits&0x7fffffffffffffffULL)!=0;
    if(op==0x04)safe=safe&&!(bits>>63);
#if defined(ARANYM_ARM64)
    uae_u64 control;__asm__ volatile("mrs %0, fpcr":"=r"(control));
    safe=safe&&control==0;
#else
    uae_u32 control;__asm__ volatile("vmrs %0, fpscr":"=r"(control));
    safe=safe&&(control&0x07ff9f00u)==0;
#endif
    if(!safe && arm_fpu_diagnostics())++arm_fpu_deoptimized;
    return safe;
}
static void arm_fpu_deopt() {
    regs.fault_pc=m68k_getpc();
    const uae_u32 opcode=GET_OPCODE;
    cpufunctbl[opcode](opcode);
    cpu_check_ticks();
}
static void arm_fpu_address(uintptr address) {
#if defined(ARANYM_ARM64)
    LOAD_U64(REG_WORK1,address);
#else
    compemu_raw_mov_l_ri(REG_WORK1,address);
#endif
}
static bool arm_fpu_inline_guard_enabled() {
    static const bool enabled=getenv("ARANYM_ARM_FPU_C_GUARD")==nullptr;
    return enabled;
}
#if defined(ARANYM_ARM64)
static void arm_fpu_inline_guard(unsigned extra) {
    const unsigned source=(extra>>10)&7,destination=(extra>>7)&7;
    const int op=arm_fpu_operation(extra);
    uae_u32 *failures[8];unsigned count=0;
    arm_fpu_address((uintptr)&bx_options.jit.jitfpu);LDRB_wXi(REG_WORK2,REG_WORK1,0);
    failures[count++]=(uae_u32*)get_target();CBZ_wi(REG_WORK2,0);
    LOAD_U64(REG_WORK4,(uintptr)&fpu);LDR_wXi(REG_WORK2,REG_WORK4,offsetof(fpu_t,fpcr));
    failures[count++]=(uae_u32*)get_target();CBNZ_wi(REG_WORK2,0);
    MRS_FPCR_x(REG_WORK2);
    failures[count++]=(uae_u32*)get_target();CBNZ_xi(REG_WORK2,0);
    auto bounded=[&](unsigned reg,unsigned bits) {
        LDR_xXi(bits,REG_WORK4,offsetof(fpu_t,registers)+reg*sizeof(fpu_register));
        UBFX_xxii(REG_WORK2,bits,52,11);SUB_wwi(REG_WORK2,REG_WORK2,0x300);
        CMP_wi(REG_WORK2,0x1ff);
        uae_u32 *normal=(uae_u32*)get_target();BLS_i(0);
        LSL_xxi(REG_WORK2,bits,1);
        failures[count++]=(uae_u32*)get_target();CBNZ_xi(REG_WORK2,0);
        write_jmp_target(normal,(uintptr)get_target());
    };
    bounded(source,0);
    if(op==0x20 || op==0x22 || op==0x23 || op==0x28 || op==0x38)
        bounded(destination,1);
    if(op==0x20) {
        LSL_xxi(REG_WORK2,0,1);
        failures[count++]=(uae_u32*)get_target();CBZ_xi(REG_WORK2,0);
    }
    if(op==0x04) {
        failures[count++]=(uae_u32*)get_target();TBNZ_xii(0,63,0);
    }
    uae_u32 *ready=(uae_u32*)get_target();B_i(0);
    for(unsigned i=0;i<count;++i)write_jmp_target(failures[i],(uintptr)get_target());
    if(arm_fpu_diagnostics()) {
        arm_fpu_address((uintptr)&arm_fpu_deoptimized);LDR_wXi(REG_WORK2,REG_WORK1,0);
        ADD_wwi(REG_WORK2,REG_WORK2,1);STR_wXi(REG_WORK2,REG_WORK1,0);
    }
    compemu_raw_jmp((uintptr)popall_arm_fpu_deopt);
    write_jmp_target(ready,(uintptr)get_target());
}
#else
static void arm_fpu_patch_branch(uae_u32 *branch) {
    const uintptr offset=((uintptr)get_target()-(uintptr)branch-8)>>2;
    *branch=(*branch&0xff000000u)|(offset&0xffffffu);
}
static void arm_fpu_inline_guard(unsigned extra) {
    const unsigned source=(extra>>10)&7,destination=(extra>>7)&7;
    const int op=arm_fpu_operation(extra);
    uae_u32 *failures[8];unsigned count=0;
    arm_fpu_address((uintptr)&bx_options.jit.jitfpu);LDRB_rR(REG_WORK2,REG_WORK1);
    CMP_ri(REG_WORK2,0);failures[count++]=(uae_u32*)get_target();BEQ_i(0);
    arm_fpu_address((uintptr)&fpu.fpcr);LDR_rR(REG_WORK2,REG_WORK1);
    CMP_ri(REG_WORK2,0);failures[count++]=(uae_u32*)get_target();BNE_i(0);
    emit_long(0xeef13a10u); // VMRS r3,FPSCR
    compemu_raw_mov_l_ri(REG_WORK1,0x07ff9f00u);AND_rrr(REG_WORK2,REG_WORK2,REG_WORK1);
    CMP_ri(REG_WORK2,0);failures[count++]=(uae_u32*)get_target();BNE_i(0);
    auto bounded=[&](unsigned reg,unsigned fp) {
        arm_fpu_address((uintptr)&fpu.registers[reg]);
        emit_long(fp?0xed921b00u:0xed920b00u); // VLDR d1/d0,[r2]
        emit_long(fp?0xec510b11u:0xec510b10u); // VMOV r0,r1,d1/d0
        BIC_rri(REG_WORK2,1,0x80000000u);MOV_rrLSRi(REG_WORK2,REG_WORK2,20);
        SUB_rri(REG_WORK2,REG_WORK2,0x300);CMP_ri(REG_WORK2,0x200);
        uae_u32 *normal=(uae_u32*)get_target();BCC_i(0);
        BIC_rri(REG_WORK2,1,0x80000000u);ORR_rrr(REG_WORK2,REG_WORK2,0);
        CMP_ri(REG_WORK2,0);failures[count++]=(uae_u32*)get_target();BNE_i(0);
        arm_fpu_patch_branch(normal);
    };
    bounded(source,1);
    if(op==0x20) {
        BIC_rri(REG_WORK2,1,0x80000000u);ORR_rrr(REG_WORK2,REG_WORK2,0);
        CMP_ri(REG_WORK2,0);failures[count++]=(uae_u32*)get_target();BEQ_i(0);
    }
    if(op==0x04) {
        CMP_ri(1,0);failures[count++]=(uae_u32*)get_target();BMI_i(0);
    }
    if(op==0x20 || op==0x22 || op==0x23 || op==0x28 || op==0x38)
        bounded(destination,0);
    uae_u32 *ready=(uae_u32*)get_target();B_i(0);
    for(unsigned i=0;i<count;++i)arm_fpu_patch_branch(failures[i]);
    if(arm_fpu_diagnostics()) {
        arm_fpu_address((uintptr)&arm_fpu_deoptimized);LDR_rR(REG_WORK2,REG_WORK1);
        ADD_rri(REG_WORK2,REG_WORK2,1);STR_rR(REG_WORK2,REG_WORK1);
    }
    compemu_raw_jmp((uintptr)popall_arm_fpu_deopt);
    arm_fpu_patch_branch(ready);
}
#endif
static void arm_fpu_emit(uae_u8 *pc) {
    const unsigned extra=arm_fpu_word(pc+2),raw_op=extra&127;
    const unsigned source=(extra>>10)&7,destination=(extra>>7)&7;
    const int op=arm_fpu_operation(extra);
    const bool single=raw_op==0x40 || raw_op==0x41 || raw_op==0x58 || raw_op==0x5a ||
        raw_op==0x60 || raw_op==0x62 || raw_op==0x63 || raw_op==0x68;
    flush(1);
#if defined(ARANYM_ARM64)
    compemu_raw_set_pc_i((uintptr)pc);
#else
    compemu_raw_mov_l_mi((uintptr)&regs.pc_p,(uintptr)pc);
    compemu_raw_mov_l_mi((uintptr)&regs.pc_oldp,(uintptr)pc);
    compemu_raw_mov_l_mi((uintptr)&regs.pc,(uintptr)pc-MEMBaseDiff);
    compemu_raw_mov_l_mi((uintptr)&regs.fault_pc,(uintptr)pc-MEMBaseDiff);
#endif
#if defined(ARANYM_ARM64)
    if(arm_fpu_inline_guard_enabled())arm_fpu_inline_guard(extra);
    else {
        compemu_raw_mov_l_ri(REG_PAR1,extra);
        compemu_raw_call((uintptr)arm_fpu_guard);
        uae_u32 *ready=(uae_u32*)get_target();CBNZ_wi(REG_RESULT,0);
        compemu_raw_jmp((uintptr)popall_arm_fpu_deopt);
        write_jmp_target(ready,(uintptr)get_target());
    }
    if(arm_fpu_inline_guard_enabled()) {
        FMOV_dx(1,0);
        if(op==0x20 || op==0x22 || op==0x23 || op==0x28 || op==0x38)FMOV_dx(0,1);
    } else {
        arm_fpu_address((uintptr)&fpu.registers[source]);LDR_dXi(1,REG_WORK1,0);
        arm_fpu_address((uintptr)&fpu.registers[destination]);LDR_dXi(0,REG_WORK1,0);
    }
    switch(op) {
    case 0x00:case 0x3a:FMOV_dd(0,1);break;
    case 0x04:FSQRT_dd(0,1);break;
    case 0x18:FABS_dd(0,1);break;
    case 0x1a:FNEG_dd(0,1);break;
    case 0x20:FDIV_ddd(0,0,1);break;
    case 0x22:FADD_ddd(0,0,1);break;
    case 0x23:FMUL_ddd(0,0,1);break;
    case 0x28:case 0x38:FSUB_ddd(0,0,1);break;
    }
    if(single){FCVT_sd(0,0);FCVT_ds(0,0);}
    if(arm_fpu_inline_guard_enabled()) {
        if(op!=0x38 && op!=0x3a)STR_dXi(0,REG_WORK4,offsetof(fpu_t,registers)+destination*sizeof(fpu_register));
        STR_dXi(0,REG_WORK4,offsetof(fpu_t,result));
    } else {
        if(op!=0x38 && op!=0x3a){arm_fpu_address((uintptr)&fpu.registers[destination]);STR_dXi(0,REG_WORK1,0);}
        arm_fpu_address((uintptr)&fpu.result);STR_dXi(0,REG_WORK1,0);
    }
    if(op==0x38 || op==0x3a) {
        compemu_raw_mov_l_mi((uintptr)&fpu.fpsr.quotient,0);
        compemu_raw_mov_l_mi((uintptr)&fpu.fpsr.exception_status,0);
        compemu_raw_mov_l_mi((uintptr)&fpu.fpsr.accrued_exception,0);
    }
    if(arm_fpu_diagnostics()) {
        arm_fpu_address((uintptr)&arm_fpu_executed);LDR_wXi(REG_WORK2,REG_WORK1,0);
        ADD_wwi(REG_WORK2,REG_WORK2,1);STR_wXi(REG_WORK2,REG_WORK1,0);
    }
    compemu_raw_set_pc_i((uintptr)(pc+4));
#else
    if(arm_fpu_inline_guard_enabled())arm_fpu_inline_guard(extra);
    else {
        compemu_raw_mov_l_ri(REG_PAR1,extra);
        compemu_raw_call((uintptr)arm_fpu_guard);
        CMP_ri(REG_RESULT,0);
        uae_u32 *ready=(uae_u32*)get_target();BNE_i(0);
        compemu_raw_jmp((uintptr)popall_arm_fpu_deopt);
        arm_fpu_patch_branch(ready);
        arm_fpu_address((uintptr)&fpu.registers[source]);emit_long(0xed921b00u);
        arm_fpu_address((uintptr)&fpu.registers[destination]);emit_long(0xed920b00u);
    }
    switch(op) {
    case 0x00:case 0x3a:emit_long(0xeeb00b41u);break;
    case 0x04:emit_long(0xeeb10bc1u);break;
    case 0x18:emit_long(0xeeb00bc1u);break;
    case 0x1a:emit_long(0xeeb10b41u);break;
    case 0x20:emit_long(0xee800b01u);break;
    case 0x22:emit_long(0xee300b01u);break;
    case 0x23:emit_long(0xee200b01u);break;
    case 0x28:case 0x38:emit_long(0xee300b41u);break;
    }
    if(single){emit_long(0xeeb70bc0u);emit_long(0xeeb70ac0u);}
    if(op!=0x38 && op!=0x3a){arm_fpu_address((uintptr)&fpu.registers[destination]);emit_long(0xed820b00u);}
    arm_fpu_address((uintptr)&fpu.result);emit_long(0xed820b00u);
    if(op==0x38 || op==0x3a) {
        compemu_raw_mov_l_mi((uintptr)&fpu.fpsr.quotient,0);
        compemu_raw_mov_l_mi((uintptr)&fpu.fpsr.exception_status,0);
        compemu_raw_mov_l_mi((uintptr)&fpu.fpsr.accrued_exception,0);
    }
    if(arm_fpu_diagnostics()) {
        arm_fpu_address((uintptr)&arm_fpu_executed);LDR_rR(REG_WORK2,REG_WORK1);
        ADD_rri(REG_WORK2,REG_WORK2,1);STR_rR(REG_WORK2,REG_WORK1);
    }
    compemu_raw_mov_l_mi((uintptr)&regs.pc_p,(uintptr)(pc+4));
    compemu_raw_mov_l_mi((uintptr)&regs.pc_oldp,(uintptr)(pc+4));
    compemu_raw_mov_l_mi((uintptr)&regs.pc,(uintptr)(pc+4)-MEMBaseDiff);
#endif
    comp_pc_p=pc+4;
    init_comp();
}
#endif
