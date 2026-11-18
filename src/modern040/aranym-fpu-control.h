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

bool modern040_fpu_movem(uae_u32 opcode,uae_u32 extra,uae_u32 pc) {
    const bool store=extra&0x2000;
    const unsigned mode=(opcode>>3)&7,reg=opcode&7,list_mode=(extra>>11)&3;
    if((extra&0x0700) || list_mode!=(store&&mode==4?0u:2u))return false;
    uae_u32 address=0,next=pc+4;
    if(mode>=2&&mode<=4) {
        if(!store&&mode==4)return false;
        address=regs.regs[8+reg];
    } else if(mode==5) {
        if(!modern040_fpu_code(pc,8))return false;
        address=regs.regs[8+reg]+uae_s16(phys_get_word(pc+4));
        next+=2;
    } else if(mode==7&&reg<=1) {
        if(!modern040_fpu_code(pc,reg?10:8))return false;
        address=reg?phys_get_long(pc+4):uae_u32(uae_s32(uae_s16(phys_get_word(pc+4))));
        next+=reg?4:2;
    } else return false;
    if(!modern040_fpu_code(next,2))return false;
    unsigned count=0;
    for(unsigned selected=extra&255;selected;selected&=selected-1)++count;
    const unsigned bytes=count*12;
    const uae_u32 begin=mode==4?address-bytes:address;
    const auto end=uint64_t(begin)+bytes;
    if(bytes && ((begin&1) || begin<0x800 ||
       !(end<=STRAM_END || (begin>=FastRAMBase && end<=uint64_t(FastRAMBase)+FastRAMSize)) ||
       !test_ram_boundary(begin,bytes,regs.s,store)))return false;
    const auto write=[&](uae_u32 at,uae_u32 value) {
        if(frame_irq_profile&&!frame_irq_skip_native_notify) {
            const m040_bus_request request{M040_ABI_VERSION,M040_WRITE,at,4,regs.s?5u:1u,0,0,0};
            checked(m040_frame_write_notify(&request),"invalidate FMOVEM RAM-store frame identity before transfer");
        }
        phys_put_long(at,value);
    };
    for(unsigned item=0,selected=extra&255;item<8;++item,selected<<=1) {
        if(!(selected&0x80))continue;
        const unsigned fpreg=mode==4?7-item:item;
        if(mode==4)address-=12;
        if(store) {
            uae_u32 bits[3]={};std::memcpy(bits,fpu_register_address(fpreg),10);
            write(address,(bits[2]&0xffff)<<16);
            write(address+4,bits[1]);
            write(address+8,bits[0]);
        } else {
            const uae_u32 high=phys_get_long(address),middle=phys_get_long(address+4),low=phys_get_long(address+8);
            const uae_u32 bits[3]={low,middle,high>>16};
            std::memcpy(fpu_register_address(fpreg),bits,10);
        }
        if(mode!=4)address+=12;
    }
    if(mode==3||mode==4)regs.regs[8+reg]=address;
    m68k_setpc(next);
    regs.fault_pc=pc;
    native_fpu_active=true;
    return true;
}

bool modern040_fpu_control_step(uae_u32 opcode,uae_u32 pc) {
    const bool control=(opcode&0xfff8)==0xf200;
    const bool arithmetic=(opcode&0xffc0)==0xf200;
    const bool branch=(opcode&0xffa0)==0xf280;
    if(!arithmetic&&!branch)return false;
    static const bool reference=std::getenv("ARANYM_FPU_SOFTFLOAT_REFERENCE")!=nullptr;
    if(reference || legacy_fpu || CPUType!=4 || !aranym_modern_fpu_mode() || regs.t1 || regs.t0 ||
       aranym_modern_translation_active(regs.tc,regs.itt0,regs.itt1,regs.dtt0,regs.dtt1) ||
       modern040_restart_pending() || modern040_irq_restart_pending() || !modern040_fpu_code(pc,4))return false;
    if(arithmetic) {
        const uae_u32 extra=phys_get_word(pc+2);
        if((extra&0xc000)==0xc000)return modern040_fpu_movem(opcode,extra,pc);
        if(!control)return false;
        if(!modern040_fpu_code(pc+4,2))return false;
        const uae_u32 selected=extra&0x1c00;
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
