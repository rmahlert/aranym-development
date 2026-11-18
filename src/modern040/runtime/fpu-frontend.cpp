/* SPDX-License-Identifier: GPL-2.0-or-later
 * Isolated 68040 FPU guest-instruction frontend.  Arithmetic is performed by
 * the exact pinned WinUAE SoftFloat backend through its canonical 96-bit seam.
 */
#include "core.hpp"
#include "softfloat-adapter.hpp"

namespace modern040 {
namespace {
struct Source { Extended96 words{}; };
std::uint32_t condition(const Extended96&words) {
    const auto exponent=(words[0]>>16)&0x7fff;
    const auto significand=(std::uint64_t(words[1])<<32)|words[2];
    const bool nan=exponent==0x7fff&&(significand<<1)!=0;
    const bool infinity=exponent==0x7fff&&(significand<<1)==0;
    const bool zero=exponent==0&&significand==0;
    return (words[0]&0x80000000?0x08000000u:0)|(zero?0x04000000u:0)|
           (infinity?0x02000000u:0)|(nan?0x01000000u:0);
}
std::uint32_t accrued(std::uint32_t status) {
    std::uint32_t value=regs.fpsr&0xf8u;
    if(status&0x00006000u)value|=0x80;
    if(status&0x00001000u)value|=0x40;
    if(status&0x00000800u)value|=0x20;
    if(status&0x00000400u)value|=0x10;
    if(status&0x00000300u)value|=0x08;
    return value;
}
int arithmetic_vector(std::uint32_t status) {
    static constexpr int vectors[8]={49,49,50,51,53,52,54,48};
    const auto enabled=(status&regs.fpcr&0xff00u)>>8;
    for(int bit=7;bit>=0;--bit)if(enabled&(1u<<bit))return vectors[bit];
    return 0;
}
bool deliver_bsun(bool enabled) {
    if(!enabled)return false;
    regs.fpiar=regs.instruction_pc;regs.fpu_state=1;Exception(48);return true;
}
bool fpu_condition(unsigned code,bool&enabled_bsun) {
    code&=31;const bool n=regs.fpsr&0x08000000,z=regs.fpsr&0x04000000,nan=regs.fpsr&0x01000000;
    enabled_bsun=false;
    if((code&0x10)&&nan) {
        regs.fpsr|=0x00008080; // BSUN status and accrued invalid operation.
        enabled_bsun=bool(regs.fpcr&0x8000);
    }
    switch(code&15) {
    case 0:return false;case 1:return z;case 2:return !nan&&!z&&!n;
    case 3:return z||(!nan&&!n);case 4:return n&&!nan&&!z;
    case 5:return z||(n&&!nan);case 6:return !nan&&!z;case 7:return !nan;
    case 8:return nan;case 9:return nan||z;case 10:return nan||(!n&&!z);
    case 11:return nan||z||!n;case 12:return nan||(n&&!z);case 13:return nan||z||n;
    case 14:return !z;default:return true;
    }
}
bool source_operand(uae_u16 opcode,uae_u16 extra,Source&out) {
    const unsigned format=(extra>>10)&7,mode=(opcode>>3)&7,reg=opcode&7;
    if(format==3||format==7)return false; // packed/undefined are not 68040 hardware datatypes.
    uae_u32 cursor=regs.instruction_pc+4,address=0;bool immediate=false;
    const unsigned sizes[8]={4,4,12,12,2,8,1,0};
    if(mode==0) {
        const auto value=regs.regs[reg];
        if(format==0)out.words=softfloat_from_integer(std::int32_t(value)).words;
        else if(format==1)out.words=softfloat_from_single(value).words;
        else if(format==4)out.words=softfloat_from_integer(std::int16_t(value)).words;
        else if(format==6)out.words=softfloat_from_integer(std::int8_t(value)).words;
        else return false;
        regs.pc=cursor;return true;
    }
    if(mode==1)return false;
    switch(mode) {
    case 2:address=regs.regs[8+reg];break;
    case 3:address=regs.regs[8+reg];regs.regs[8+reg]+=reg==7&&sizes[format]<2?2:sizes[format];break;
    case 4:regs.regs[8+reg]-=reg==7&&sizes[format]<2?2:sizes[format];address=regs.regs[8+reg];break;
    case 5:address=regs.regs[8+reg]+std::int16_t(read(AccessKind::Fetch,cursor,2));cursor+=2;break;
    case 6:
        regs.pc=cursor;address=get_disp_ea_020(regs.regs[8+reg],0);cursor=regs.pc;break;
    case 7:
        switch(reg) {
        case 0:address=std::int16_t(read(AccessKind::Fetch,cursor,2));cursor+=2;break;
        case 1:address=read(AccessKind::Fetch,cursor,4);cursor+=4;break;
        case 2:{const auto base=cursor;address=base+std::int16_t(read(AccessKind::Fetch,cursor,2));cursor+=2;break;}
        case 3:{const auto base=cursor;regs.pc=cursor;address=get_disp_ea_020(base,0);cursor=regs.pc;break;}
        case 4:address=cursor;immediate=true;cursor+=sizes[format];if(format==4||format==6)cursor=regs.instruction_pc+6;break;
        default:return false;
        }
        break;
    }
    const auto kind=immediate?AccessKind::Fetch:AccessKind::Read;
    switch(format) {
    case 0:out.words=softfloat_from_integer(std::int32_t(read(kind,address,4))).words;break;
    case 1:out.words=softfloat_from_single(read(kind,address,4)).words;break;
    case 2:{const auto a=read(kind,address,4),b=read(kind,address+4,4),c=read(kind,address+8,4);out.words=softfloat_from_extended(a,b,c).words;break;}
    case 4:out.words=softfloat_from_integer(std::int16_t(read(kind,address,2))).words;break;
    case 5:{const auto high=read(kind,address,4),low=read(kind,address+4,4);out.words=softfloat_from_double(high,low).words;break;}
    case 6:out.words=softfloat_from_integer(std::int8_t(read(kind,address+(immediate?1:0),1))).words;break;
    default:return false;
    }
    regs.pc=cursor;return true;
}
bool byte_destination(uae_u16 opcode,uae_u32&address) {
    const unsigned mode=(opcode>>3)&7,reg=opcode&7;uae_u32 cursor=regs.instruction_pc+4;
    switch(mode) {
    case 2:address=regs.regs[8+reg];break;
    case 3:address=regs.regs[8+reg];regs.regs[8+reg]+=reg==7?2:1;break;
    case 4:regs.regs[8+reg]-=reg==7?2:1;address=regs.regs[8+reg];break;
    case 5:address=regs.regs[8+reg]+std::int16_t(read(AccessKind::Fetch,cursor,2));cursor+=2;break;
    case 6:regs.pc=cursor;address=get_disp_ea_020(regs.regs[8+reg],0);cursor=regs.pc;break;
    case 7:
        if(reg==0){address=std::int16_t(read(AccessKind::Fetch,cursor,2));cursor+=2;}
        else if(reg==1){address=read(AccessKind::Fetch,cursor,4);cursor+=4;}
        else return false;
        break;
    default:return false;
    }
    regs.pc=cursor;return true;
}
bool frame_address(uae_u16 opcode,bool save,uae_u32&address) {
    const unsigned mode=(opcode>>3)&7,reg=opcode&7;uae_u32 cursor=regs.instruction_pc+2;
    switch(mode) {
    case 2:address=regs.regs[8+reg];break;
    case 3:if(save)return false;address=regs.regs[8+reg];break;
    case 4:if(!save)return false;address=regs.regs[8+reg];break;
    case 5:address=regs.regs[8+reg]+std::int16_t(read(AccessKind::Fetch,cursor,2));cursor+=2;break;
    case 6:regs.pc=cursor;address=get_disp_ea_020(regs.regs[8+reg],0);cursor=regs.pc;break;
    case 7:
        if(reg==0){address=std::int16_t(read(AccessKind::Fetch,cursor,2));cursor+=2;}
        else if(reg==1){address=read(AccessKind::Fetch,cursor,4);cursor+=4;}
        else if(!save&&reg==2){const auto base=cursor;address=base+std::int16_t(read(AccessKind::Fetch,cursor,2));cursor+=2;}
        else if(!save&&reg==3){const auto base=cursor;regs.pc=cursor;address=get_disp_ea_020(base,0);cursor=regs.pc;}
        else return false;
        break;
    default:return false;
    }
    regs.pc=cursor;return true;
}
bool store_address(uae_u16 opcode,unsigned size,uae_u32&address) {
    const unsigned mode=(opcode>>3)&7,reg=opcode&7;uae_u32 cursor=regs.instruction_pc+4;
    switch(mode) {
    case 2:address=regs.regs[8+reg];break;
    case 3:address=regs.regs[8+reg];regs.regs[8+reg]+=reg==7&&size<2?2:size;break;
    case 4:regs.regs[8+reg]-=reg==7&&size<2?2:size;address=regs.regs[8+reg];break;
    case 5:address=regs.regs[8+reg]+std::int16_t(read(AccessKind::Fetch,cursor,2));cursor+=2;break;
    case 6:regs.pc=cursor;address=get_disp_ea_020(regs.regs[8+reg],0);cursor=regs.pc;break;
    case 7:
        if(reg==0){address=std::int16_t(read(AccessKind::Fetch,cursor,2));cursor+=2;}
        else if(reg==1){address=read(AccessKind::Fetch,cursor,4);cursor+=4;}
        else return false;
        break;
    default:return false;
    }
    regs.pc=cursor;return true;
}
bool control_address(uae_u16 opcode,bool to_memory,uae_u32&address) {
    const unsigned mode=(opcode>>3)&7,reg=opcode&7;uae_u32 cursor=regs.instruction_pc+4;
    switch(mode) {
    case 2:case 3:case 4:address=regs.regs[8+reg];break;
    case 5:address=regs.regs[8+reg]+std::int16_t(read(AccessKind::Fetch,cursor,2));cursor+=2;break;
    case 6:regs.pc=cursor;address=get_disp_ea_020(regs.regs[8+reg],0);cursor=regs.pc;break;
    case 7:
        if(reg==0){address=std::int16_t(read(AccessKind::Fetch,cursor,2));cursor+=2;}
        else if(reg==1){address=read(AccessKind::Fetch,cursor,4);cursor+=4;}
        else if(!to_memory&&reg==2){const auto base=cursor;address=base+std::int16_t(read(AccessKind::Fetch,cursor,2));cursor+=2;}
        else if(!to_memory&&reg==3){const auto base=cursor;regs.pc=cursor;address=get_disp_ea_020(base,0);cursor=regs.pc;}
        else return false;
        break;
    default:return false;
    }
    regs.pc=cursor;return true;
}
}

bool execute_modern_fpu(uae_u16 opcode) {
    /* The arithmetic family is F200..F23f.  Other F-line families (FBcc,
     * FScc/FDBcc/FTRAPcc and FSAVE/FRESTORE) have distinct extension and
     * frame rules and are admitted only when their own frontend is present. */
    const bool save_family=(opcode&0xffc0)==0xf300,restore_family=(opcode&0xffc0)==0xf340;
    if(save_family||restore_family) {
        if(!regs.s){Exception(8);return true;}
        uae_u32 address=0;if(!frame_address(opcode,save_family,address))return false;
        const auto mode=(opcode>>3)&7,reg=opcode&7;
        if(save_family) {
            if(mode==4){address-=4;regs.regs[8+reg]=address;}
            write(address,4,regs.fpu_state?0x41000000u:0u);
        } else {
            const auto frame=read(AccessKind::Read,address,4),version=frame>>24,size=(frame>>16)&0xff;
            if(version==0) {
                regs.fpu_state=0;regs.fpcr=regs.fpsr=regs.fpiar=0;
                for(auto&value:regs.fp)value={0x7fff0000,0xffffffff,0xffffffff};
            } else if(version==0x41&&size==0)regs.fpu_state=1;
            else return false; // valid pending/busy frame resume is not yet connected.
            if(mode==3)regs.regs[8+reg]=address+4;
        }
        return true;
    }
    if((opcode&0xfff8)==0xf248) {
        const auto extra=uae_u16(read(AccessKind::Fetch,regs.instruction_pc+2,2));
        const auto displacement=std::int16_t(read(AccessKind::Fetch,regs.instruction_pc+4,2));
        bool enabled_bsun=false;const bool condition=fpu_condition(extra,enabled_bsun);
        if(deliver_bsun(enabled_bsun))return true;
        regs.fpiar=regs.instruction_pc;regs.fpu_state=1;regs.pc=regs.instruction_pc+6;
        if(!condition) {
            auto&data=regs.regs[opcode&7];const auto count=uae_u16(data)-1;
            data=(data&0xffff0000u)|count;if(count!=0xffff)regs.pc=regs.instruction_pc+4+displacement;
        }
        return true;
    }
    if(opcode==0xf27a||opcode==0xf27b||opcode==0xf27c) {
        const auto extra=uae_u16(read(AccessKind::Fetch,regs.instruction_pc+2,2));
        if(opcode==0xf27a){(void)read(AccessKind::Fetch,regs.instruction_pc+4,2);regs.pc=regs.instruction_pc+6;}
        else if(opcode==0xf27b){(void)read(AccessKind::Fetch,regs.instruction_pc+4,4);regs.pc=regs.instruction_pc+8;}
        else regs.pc=regs.instruction_pc+4;
        bool enabled_bsun=false;const bool condition=fpu_condition(extra,enabled_bsun);
        if(deliver_bsun(enabled_bsun))return true;
        regs.fpiar=regs.instruction_pc;regs.fpu_state=1;
        if(condition)Exception_cpu_oldpc(7,regs.instruction_pc);
        return true;
    }
    if((opcode&0xffc0)==0xf240) {
        const auto mode=(opcode>>3)&7;
        if(mode==1)return false; // FDBcc handled above.
        const auto extra=uae_u16(read(AccessKind::Fetch,regs.instruction_pc+2,2));
        bool enabled_bsun=false;const bool condition=fpu_condition(extra,enabled_bsun);
        if(deliver_bsun(enabled_bsun))return true;
        regs.fpiar=regs.instruction_pc;regs.fpu_state=1;
        if(mode==0) {
            auto&data=regs.regs[opcode&7];data=(data&0xffffff00u)|(condition?0xff:0);regs.pc=regs.instruction_pc+4;
        } else {
            uae_u32 address=0;if(!byte_destination(opcode,address))return false;write(address,1,condition?0xff:0);
        }
        return true;
    }
    if((opcode&0xff80)==0xf280) {
        const bool long_displacement=opcode&0x40;const auto base=regs.instruction_pc+2;
        const auto displacement=long_displacement?std::int32_t(read(AccessKind::Fetch,base,4)):
                                                  std::int32_t(std::int16_t(read(AccessKind::Fetch,base,2)));
        bool enabled_bsun=false;const bool taken=fpu_condition(opcode&63,enabled_bsun);
        if(deliver_bsun(enabled_bsun))return true;
        regs.fpiar=regs.instruction_pc;regs.fpu_state=1;
        regs.pc=taken?base+displacement:base+(long_displacement?4:2);
        return true;
    }
    if((opcode&0xffc0)!=0xf200)return false;
    const auto extra=uae_u16(read(AccessKind::Fetch,regs.pc+2,2));
    const auto opclass=(extra>>13)&7;
    if(opclass==6||opclass==7) {
        const bool to_memory=extra&0x2000;uae_u32 address=0;
        if(!control_address(opcode,to_memory,address))return false;
        const unsigned list_mode=(extra>>11)&3;
        uae_u32 list=(list_mode==0||list_mode==2)?extra&0xff:regs.regs[(extra>>4)&7]&0xff;
        const auto ea_mode=(opcode>>3)&7,ea_reg=opcode&7;int increment=ea_mode==4?-1:1,reg_direction=1;
        if((list_mode==0||list_mode==1)&&(ea_mode==4||to_memory))reg_direction=-1;
        if(list&&to_memory&&reg_direction<0&&increment>0&&ea_mode!=4)reg_direction=-2;
        if(list&&to_memory&&reg_direction>0&&increment<0)reg_direction=2;
        for(unsigned r=0;r<8;++r) {
            const unsigned fpreg=reg_direction<0?7-r:r;
            if(list&0x80) {
                if(increment<0)address-=12;
                if(to_memory) {
                    const auto&words=regs.fp[fpreg];
                    if(reg_direction<-1||reg_direction>1){write(address,4,words[2]);write(address+4,4,words[1]);write(address+8,4,words[0]);}
                    else {write(address,4,words[0]);write(address+4,4,words[1]);write(address+8,4,words[2]);}
                } else {
                    const auto a=read(AccessKind::Read,address,4),b=read(AccessKind::Read,address+4,4),c=read(AccessKind::Read,address+8,4);
                    regs.fp[fpreg]=softfloat_from_extended(a,b,c).words;
                }
                if(increment>0)address+=12;
            }
            list<<=1;
        }
        if(ea_mode==3||ea_mode==4)regs.regs[8+ea_reg]=address;
        regs.fpu_state=1;return true;
    }
    if(opclass==3) {
        const unsigned format=(extra>>10)&7,source=(extra>>7)&7,mode=(opcode>>3)&7;
        static constexpr unsigned sizes[8]={4,4,12,0,2,8,1,0};
        if(!sizes[format])return false;
        FpuStore converted;if(!softfloat_store(format,regs.fp[source],regs.fpcr,converted))return false;
        const auto status=converted.status&0x0000ff00u;
        if(const auto vector=arithmetic_vector(status)) {
            regs.fpiar=regs.instruction_pc;regs.fpu_state=1;
            regs.fpsr=(regs.fpsr&0x00ff0000u)|condition(regs.fp[source])|status|accrued(status);
            Exception(vector);return true;
        }
        if(mode==0) {
            auto&data=regs.regs[opcode&7];
            if(format==0||format==1)data=converted.words[0];
            else if(format==4)data=(data&0xffff0000u)|(converted.words[0]&0xffff);
            else if(format==6)data=(data&0xffffff00u)|(converted.words[0]&0xff);
            else return false;
            regs.pc=regs.instruction_pc+4;
        } else {
            uae_u32 address=0;if(!store_address(opcode,sizes[format],address))return false;
            if(format==2){write(address,4,converted.words[0]);write(address+4,4,converted.words[1]);write(address+8,4,converted.words[2]);}
            else if(format==5){write(address,4,converted.words[0]);write(address+4,4,converted.words[1]);}
            else write(address,sizes[format],converted.words[0]);
        }
        regs.fpiar=regs.instruction_pc;regs.fpu_state=1;
        regs.fpsr=(regs.fpsr&0x00ff0000u)|condition(regs.fp[source])|status|accrued(status);
        return true;
    }
    if(opclass==4||opclass==5) {
        const auto mode=(opcode>>3)&7,reg=opcode&7;
        auto selected=extra&0x1c00;if(!selected)selected=0x0400;
        regs.pc=regs.instruction_pc+4;
        if(mode==0) {
            if(selected!=0x1000&&selected!=0x0800&&selected!=0x0400)return false;
            auto&data=regs.regs[reg];
            if(extra&0x2000) {
                if(extra&0x1000)data=regs.fpcr;
                if(extra&0x0800)data=regs.fpsr;
                if(selected&0x0400)data=regs.fpiar;
            } else {
                if(extra&0x1000)regs.fpcr=data&0xffff;
                if(extra&0x0800)regs.fpsr=data;
                if(selected&0x0400)regs.fpiar=data;
            }
        } else if(mode==1) {
            if(selected!=0x0400)return false;
            if(extra&0x2000)regs.regs[8+reg]=regs.fpiar;
            else regs.fpiar=regs.regs[8+reg];
        } else if(mode==7&&reg==4&&!(extra&0x2000)) {
            auto cursor=regs.instruction_pc+4;
            if(selected&0x1000){regs.fpcr=read(AccessKind::Fetch,cursor,4)&0xffff;cursor+=4;}
            if(selected&0x0800){regs.fpsr=read(AccessKind::Fetch,cursor,4);cursor+=4;}
            if(selected&0x0400){regs.fpiar=read(AccessKind::Fetch,cursor,4);cursor+=4;}
            regs.pc=cursor;regs.fpu_state=1;return true;
        } else {
            const bool to_memory=extra&0x2000;uae_u32 address=0;
            if(!control_address(opcode,to_memory,address))return false;
            const unsigned count=unsigned(bool(selected&0x1000))+unsigned(bool(selected&0x0800))+unsigned(bool(selected&0x0400));
            const auto total=4*count;if(mode==4)address-=total;
            auto cursor=address;
            if(to_memory) {
                if(selected&0x1000){write(cursor,4,regs.fpcr);cursor+=4;}
                if(selected&0x0800){write(cursor,4,regs.fpsr);cursor+=4;}
                if(selected&0x0400){write(cursor,4,regs.fpiar);cursor+=4;}
            } else {
                if(selected&0x1000){regs.fpcr=read(AccessKind::Read,cursor,4)&0xffff;cursor+=4;}
                if(selected&0x0800){regs.fpsr=read(AccessKind::Read,cursor,4);cursor+=4;}
                if(selected&0x0400){regs.fpiar=read(AccessKind::Read,cursor,4);cursor+=4;}
            }
            if(mode==3)regs.regs[8+reg]=cursor;else if(mode==4)regs.regs[8+reg]=address;
        }
        regs.fpu_state=1;return true;
    }
    if(opclass!=0&&opclass!=2)return false;
    if((extra&0xfc00)==0x5c00)return false; // FMOVECR constant ROM.
    const unsigned source=(extra>>10)&7,destination=(extra>>7)&7;
    Source operand;
    if(extra&0x4000) {
        if(!source_operand(opcode,extra,operand))return false;
    } else {
        operand.words=regs.fp[source];regs.pc=regs.instruction_pc+4;
    }
    FpuExecution executed;
    if(!softfloat_execute_040(extra&0x7f,regs.fp[destination],operand.words,regs.fpcr,regs.fpsr,executed))return false;
    const auto status=executed.value.status&0x0000ff00u;
    regs.fpiar=regs.instruction_pc;
    regs.fpu_state=1;
    const auto quotient=executed.write_quotient?executed.quotient:(regs.fpsr&0x00ff0000u);
    regs.fpsr=quotient|condition(executed.value.words)|status|accrued(status);
    if(const auto vector=arithmetic_vector(status)){Exception(vector);return true;}
    if(executed.write_destination)regs.fp[destination]=executed.value.words;
    if(executed.write_secondary)regs.fp[executed.secondary_register]=executed.secondary;
    return true;
}
}
