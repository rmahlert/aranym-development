#ifndef ARANYM_FPU_CONTROL_H
#define ARANYM_FPU_CONTROL_H

#if defined(CPU_x86_64) && defined(MODERN040_FPU_JIT)
namespace {
bool modern040_fpu_code(uae_u32 address,unsigned size) {
    const auto end=uint64_t(address)+size;
    return !(address&1) && address>=0x800 &&
        (end<=STRAM_END || (address>=FastRAMBase && end<=uint64_t(FastRAMBase)+FastRAMSize)) &&
        test_ram_boundary(address,size,regs.s,false);
}

bool modern040_fpu_condition(unsigned code,uae_u32 flags) {
    const bool negative=flags&0x08000000,zero=flags&0x04000000,nan=flags&0x01000000;
    switch(code&15) {
    case 0:return false;
    case 1:return zero;
    case 2:return !nan&&!zero&&!negative;
    case 3:return zero||(!nan&&!negative);
    case 4:return negative&&!nan&&!zero;
    case 5:return zero||(negative&&!nan);
    case 6:return !nan&&!zero;
    case 7:return !nan;
    case 8:return nan;
    case 9:return nan||zero;
    case 10:return nan||(!negative&&!zero);
    case 11:return nan||zero||!negative;
    case 12:return nan||(negative&&!zero);
    case 13:return nan||zero||negative;
    case 14:return !zero;
    default:return true;
    }
}

bool modern040_fpu_control_step(uae_u32 opcode,uae_u32 pc) {
    const bool control=(opcode&0xfff8)==0xf200;
    const bool branch=(opcode&0xffa0)==0xf280;
    if(!control&&!branch)return false;
    static const bool reference=std::getenv("ARANYM_FPU_SOFTFLOAT_REFERENCE")!=nullptr;
    if(reference || legacy_fpu || CPUType!=4 || !aranym_modern_fpu_mode() || regs.t1 || regs.t0 ||
       aranym_modern_translation_active(regs.tc,regs.itt0,regs.itt1,regs.dtt0,regs.dtt1) ||
       modern040_restart_pending() || modern040_irq_restart_pending() || !modern040_fpu_code(pc,4))return false;
    if(control) {
        if(!modern040_fpu_code(pc+4,2))return false;
        const uae_u32 extra=phys_get_word(pc+2),selected=extra&0x1c00;
        if((extra&0xc3ff)!=0x8000 ||
           (selected!=0x1000 && selected!=0x0800 && selected!=0x0400))return false;
        auto &data=regs.regs[opcode&7];
        if(extra&0x2000) {
            if(selected==0x1000)data=fpu_get_fpcr();
            else if(selected==0x0800)data=(fpu.fpsr.condition_codes&0x0f000000u)|
                fpu.fpsr.quotient|fpu.fpsr.exception_status|fpu.fpsr.accrued_exception;
            else data=fpu.instruction_address;
        } else {
            if(selected==0x1000)fpu_set_fpcr(data&0xffff);
            else if(selected==0x0800) {
                fpu_set_fpsr(data);
                fpu.fpsr.condition_codes=data&0x0f000000u;
            } else fpu.instruction_address=data;
        }
        m68k_setpc(pc+4);
    } else {
        const bool wide=opcode&0x40;
        if(wide&&!modern040_fpu_code(pc,6))return false;
        const auto displacement=wide?uae_s32(phys_get_long(pc+2)):uae_s32(uae_s16(phys_get_word(pc+2)));
        const auto flags=fpu.fpsr.condition_codes;
        const bool bsun=(opcode&0x10)&&(flags&0x01000000);
        if(bsun&&(fpu.fpcr&0x8000))return false;
        const bool taken=modern040_fpu_condition(opcode,flags);
        const uae_u32 next=taken?pc+2+displacement:pc+(wide?6:4);
        if(!modern040_fpu_code(next,2))return false;
        if(bsun) {
            fpu.fpsr.exception_status|=0x8000;
            fpu.fpsr.accrued_exception|=0x80;
        }
        fpu.instruction_address=pc;
        m68k_setpc(next);
    }
    regs.fault_pc=pc;
    native_fpu_active=true;
    return true;
}
}
#endif

#endif
