struct NativeFpuOperand {
    uae_u64 mantissa;
    uae_u16 sign_exponent;

    unsigned exponent() const { return sign_exponent & 0x7fff; }
    bool negative() const { return (sign_exponent & 0x8000) != 0; }
    bool supported() const {
        return (!exponent() && !mantissa) ||
            (exponent() >= 0x3000 && exponent() <= 0x4fff && (mantissa >> 63));
    }
    static NativeFpuOperand load(const void *address) {
        NativeFpuOperand value{};
        const auto *bytes=static_cast<const uae_u8*>(address);
        memcpy(&value.mantissa,bytes,sizeof value.mantissa);
        memcpy(&value.sign_exponent,bytes+8,sizeof value.sign_exponent);
        return value;
    }
    void save(void *address) const {
        auto *bytes=static_cast<uae_u8*>(address);
        memcpy(bytes,&mantissa,sizeof mantissa);
        memcpy(bytes+8,&sign_exponent,sizeof sign_exponent);
    }
    uae_u32 condition() const {
        return (negative()?0x08000000u:0u) | (!mantissa?0x04000000u:0u);
    }
};
static_assert(offsetof(NativeFpuOperand,sign_exponent)==8,"x87 exponent layout");

enum class NativeFpuSource { Register, SingleDn, LongDn, SingleRam, DoubleRam, Immediate };
enum class NativeFpuOperation { Move, Negate, Absolute, Test, Compare, Add, Subtract, SingleMultiply, SquareRoot, Multiply, Divide, StoreLong, StoreFloat };

struct NativeFpuInstruction {
    uae_u32 pc;
    uae_u32 displacement;
    uae_u8 source;
    uae_u8 destination;
    uae_u8 ea_mode;
    bool *active;
};

template<typename Bits>
static ALWAYS_INLINE bool native_binary_operand(Bits bits,NativeFpuOperand &out) {
    constexpr bool single=sizeof(Bits)==4;
    constexpr unsigned fraction_bits=single?23:52, max_exponent=single?255:2047;
    constexpr unsigned bias_delta=16383-max_exponent/2;
    constexpr Bits implicit=Bits(1)<<fraction_bits;
    const unsigned exponent=(bits>>fraction_bits)&max_exponent;
    const auto fraction=bits&(implicit-1);
    if(exponent==max_exponent || (!exponent && fraction))return false;
    out.sign_exponent=(uae_u16(bits>>(sizeof(Bits)*8-16))&0x8000) | (exponent?exponent+bias_delta:0);
    out.mantissa=exponent?uae_u64(fraction|implicit)<<(63-fraction_bits):0;
    return true;
}

static ALWAYS_INLINE NativeFpuOperand native_long_operand(uae_u32 bits) {
    if (!bits) return {};
    const bool negative=(bits>>31)!=0;
    const uae_u32 magnitude=negative?0u-bits:bits;
    const unsigned power=31-__builtin_clz(magnitude);
    return {uae_u64(magnitude)<<(63-power),uae_u16((negative?0x8000:0) | (16383+power))};
}

static inline uae_u32 native_fpu_index_offset(uae_u32 extension) {
    const uae_u32 index=regs.regs[(extension>>12)&15];
    const uae_u32 value=(extension&0x800)?index:uae_u32(uae_s32(uae_s16(index)));
    return (value<<((extension>>9)&3))+uae_s8(extension);
}

static ALWAYS_INLINE bool native_fpu_ram(const NativeFpuInstruction &ins,unsigned size,bool store,uae_u32 &address) {
    const auto mode=ins.ea_mode;
    const uae_u32 base=regs.regs[8+ins.source];
    const uae_u32 displacement=mode==6?native_fpu_index_offset(ins.displacement):ins.displacement;
    address=mode==7?displacement:mode==4?base-size:base+displacement;
    if ((address&1) || address<0x800) return false;
    if (aranym_modern_translation_active(regs.tc,regs.itt0,regs.itt1,regs.dtt0,regs.dtt1)) return false;
    const uint64_t end=uint64_t(address)+size;
    if (!(end<=RAMSize || (address>=FastRAMBase && end<=uint64_t(FastRAMBase)+FastRAMSize))) return false;
    if (store) {
        static const bool frame_tracking=getenv("ARANYM_MODERN040_FRAME_IRQ")!=nullptr;
        if (frame_tracking) return false;
    }
    return test_ram_boundary(address,size,regs.s,store);
}

static ALWAYS_INLINE NativeFpuOperand native_fpu_compare(NativeFpuOperand a,NativeFpuOperand b) {
    if (!a.mantissa && !b.mantissa) return a;
    if (a.sign_exponent==b.sign_exponent && a.mantissa==b.mantissa) return {};
    bool less=a.negative();
    if (a.negative()==b.negative()) {
        const bool smaller=a.exponent()<b.exponent() ||
            (a.exponent()==b.exponent() && a.mantissa<b.mantissa);
        less=smaller!=a.negative();
    }
    return {uae_u64(1)<<63,uae_u16(less?0xbfff:0x3fff)};
}

static ALWAYS_INLINE NativeFpuOperand native_fpu_single_multiply(NativeFpuOperand a,NativeFpuOperand b,bool &inexact) {
    const unsigned sign=(a.sign_exponent^b.sign_exponent)&0x8000;
    if (!a.mantissa || !b.mantissa) return {0,uae_u16(sign)};
    const uae_u64 product=(a.mantissa>>40)*(b.mantissa>>40);
    const unsigned carry=product>>47;
    unsigned exponent=a.exponent()+b.exponent()-16383+carry;
    const unsigned shift=23+carry;
    const uae_u64 half=uae_u64(1)<<(shift-1);
    const uae_u64 remainder=product&((half<<1)-1);
    uae_u64 rounded=product>>shift;
    inexact=remainder!=0;
    if (remainder>half || (remainder==half && (rounded&1))) {
        if (++rounded==(uae_u64(1)<<24)) { rounded>>=1; ++exponent; }
    }
    return {rounded<<40,uae_u16(sign|exponent)};
}

static ALWAYS_INLINE bool native_fpu_add(NativeFpuOperand a,NativeFpuOperand b,NativeFpuOperand &out,bool &inexact) {
    if (!a.mantissa && !b.mantissa) { out={0,uae_u16(a.sign_exponent&b.sign_exponent&0x8000)}; return true; }
    if (!a.mantissa || !b.mantissa) { out=a.mantissa?a:b; return true; }
    if (a.exponent()<b.exponent() || (a.exponent()==b.exponent() && a.mantissa<b.mantissa)) std::swap(a,b);
    const unsigned distance=a.exponent()-b.exponent();
    if (distance>63) {
        constexpr unsigned significand_bits=64;
        if (distance<significand_bits+2) return false;
        out=a;
        inexact=true;
        return true;
    }
    using Wide=unsigned __int128;
    const Wide large=Wide(a.mantissa)<<distance;
    const Wide sum=a.negative()==b.negative()?large+b.mantissa:large-b.mantissa;
    if (!sum) { out={}; return true; }
    const uae_u64 hi=sum>>64,lo=sum;
    const int shift=hi?64-__builtin_clzll(hi):-__builtin_clzll(lo);
    int exponent=int(b.exponent())+shift;
    uae_u64 rounded;
    if (shift>0) {
        rounded=sum>>shift;
        const uae_u64 half=uae_u64(1)<<(shift-1);
        const uae_u64 remainder=lo&((half-1)|half);
        inexact=remainder!=0;
        if ((remainder>half || (remainder==half && (rounded&1))) && ++rounded==0) {
            rounded=uae_u64(1)<<63;
            ++exponent;
        }
    } else rounded=lo<<-shift;
    out={rounded,uae_u16((a.sign_exponent&0x8000)|exponent)};
    return true;
}

static ALWAYS_INLINE bool native_fpu_to_long(NativeFpuOperand a,uae_u32 &bits,bool &inexact) {
    if (!a.mantissa) { bits=0; return true; }
    const int power=int(a.exponent())-16383;
    if (power>=30) return false;
    uae_u32 magnitude=0;
    bool above_half=false,halfway=false;
    if (power<0) {
        inexact=true;
        if (power==-1) {
            halfway=a.mantissa==(uae_u64(1)<<63);
            above_half=!halfway;
        }
    } else {
        const unsigned shift=63-power;
        const uae_u64 half=uae_u64(1)<<(shift-1);
        const uae_u64 remainder=a.mantissa&((half<<1)-1);
        magnitude=a.mantissa>>shift;
        inexact=remainder!=0;
        above_half=remainder>half; halfway=remainder==half;
    }
    const unsigned mode=(fpu.fpcr>>4)&3;
    if ((!mode && (above_half || (halfway && (magnitude&1)))) ||
        (inexact && ((mode==2 && a.negative()) || (mode==3 && !a.negative())))) ++magnitude;
    bits=a.negative()?0u-magnitude:magnitude;
    return true;
}

template<typename Bits>
static ALWAYS_INLINE bool native_fpu_to_binary(NativeFpuOperand a,Bits &bits,bool &inexact) {
    constexpr bool single=sizeof(Bits)==4;
    constexpr unsigned fraction_bits=single?23:52, shift=63-fraction_bits;
    constexpr int max_exponent=single?255:2047, bias_delta=16383-max_exponent/2;
    bits=Bits(a.sign_exponent&0x8000)<<(sizeof(Bits)*8-16);
    if(!a.mantissa)return true;
    const int exponent=int(a.exponent())-bias_delta;
    if(exponent<=0 || exponent>=max_exponent-1)return false;
    Bits significand=a.mantissa>>shift;
    const uae_u64 remainder=a.mantissa&((uae_u64(1)<<shift)-1),half=uae_u64(1)<<(shift-1);
    inexact=remainder!=0;
    if(remainder>half || (remainder==half && (significand&1)))++significand;
    bits|=(Bits(exponent-1)<<fraction_bits)+significand;
    return true;
}

static ALWAYS_INLINE void native_fpu_store_longword(uae_u32 address,uae_u32 bits) {
    if(address&2) {
        phys_put_word(address,uae_u16(bits>>16));
        phys_put_word(address+2,uae_u16(bits));
    } else phys_put_long(address,bits);
}

#include "compemu_fpu_numeric.h"

template<NativeFpuOperation op,NativeFpuSource source>
static uae_u32 REGPARAM2 native_fpu_execute(const NativeFpuInstruction *ins) {
    using Op=NativeFpuOperation;
    using Source=NativeFpuSource;
    constexpr bool ram=source==Source::SingleRam || source==Source::DoubleRam;
    constexpr unsigned size=source==Source::DoubleRam?8:4;
    constexpr bool store=op==Op::StoreFloat || op==Op::StoreLong;
    if (op==Op::StoreLong ? (fpu.fpcr&~0x30u)!=0 : fpu.fpcr!=0) return 0;
    uae_u32 address=0,bits=0;
    if constexpr (ram) {
        if (!native_fpu_ram(*ins,size,store,address)) return 0;
    }
    NativeFpuOperand input{},output{};
    bool inexact=false;
    if constexpr (store) {
        input=NativeFpuOperand::load(fpu_register_address(ins->destination&7));
        if (!input.supported()) return 0;
        if constexpr (source==Source::DoubleRam) {
            uae_u64 wide;
            if(!native_fpu_to_binary(input,wide,inexact))return 0;
            native_fpu_store_longword(address,uae_u32(wide>>32));
            native_fpu_store_longword(address+4,uae_u32(wide));
        } else {
            if constexpr (op==Op::StoreLong) {
                if(!native_fpu_to_long(input,bits,inexact))return 0;
            } else if(!native_fpu_to_binary(input,bits,inexact))return 0;
            if constexpr (source==Source::SingleRam)native_fpu_store_longword(address,bits);
            else regs.regs[ins->source]=bits;
        }
    } else {
        if constexpr (source==Source::Register) {
            input=NativeFpuOperand::load(fpu_register_address(ins->source&7));
            if (!input.supported()) return 0;
        } else if constexpr (source==Source::DoubleRam) {
            uae_u64 wide;
            memcpy(&wide,phys_get_real_address(address),sizeof wide);
            if(!native_binary_operand(SDL_SwapBE64(wide),input))return 0;
        } else if constexpr (source==Source::LongDn) input=native_long_operand(regs.regs[ins->source]);
        else {
            if constexpr (source==Source::SingleRam) {
                memcpy(&bits,phys_get_real_address(address),4);
                bits=SDL_SwapBE32(bits);
            }
            else if constexpr (source==Source::Immediate) bits=ins->displacement;
            else bits=regs.regs[ins->source];
            if (!native_binary_operand(bits,input)) return 0;
        }
        if constexpr (op==Op::Move || op==Op::Test || op==Op::Negate || op==Op::Absolute) {
            output=input;
            if constexpr (op==Op::Negate) output.sign_exponent^=0x8000;
            if constexpr (op==Op::Absolute) output.sign_exponent&=0x7fff;
        } else if constexpr (op==Op::SquareRoot) {
            if (!native_fpu_sqrt(input,output,inexact)) return 0;
        } else {
            const auto destination=NativeFpuOperand::load(fpu_register_address(ins->destination&7));
            if (!destination.supported()) return 0;
            if constexpr (op==Op::Compare) output=native_fpu_compare(destination,input);
            if constexpr (op==Op::SingleMultiply) output=native_fpu_single_multiply(destination,input,inexact);
            if constexpr (op==Op::Multiply) {
                if (!native_fpu_multiply(destination,input,output,inexact)) return 0;
            }
            if constexpr (op==Op::Divide) {
                if (!native_fpu_divide(destination,input,output,inexact)) return 0;
            }
            if constexpr (op==Op::Add || op==Op::Subtract) {
                if constexpr (op==Op::Subtract) input.sign_exponent^=0x8000;
                if (!native_fpu_add(destination,input,output,inexact)) return 0;
            }
        }
        if constexpr (op!=Op::Test && op!=Op::Compare) output.save(fpu_register_address(ins->destination&7));
        output.save(&fpu.result);
        fpu.fpsr.condition_codes=output.condition();
    }
    if constexpr (ram) {
        if (ins->ea_mode==3) regs.regs[8+ins->source]=address+size;
        if (ins->ea_mode==4) regs.regs[8+ins->source]=address;
    }
    fpu.fpsr.exception_status=inexact?0x200:0;
    if (inexact) fpu.fpsr.accrued_exception|=8;
    fpu.instruction_address=ins->pc;
    *ins->active=true;
    return 1;
}

using NativeFpuHandler=uae_u32 (REGPARAM2 *)(const NativeFpuInstruction *);
template<NativeFpuSource source>
static NativeFpuHandler native_fpu_handler(unsigned extra) {
    using Op=NativeFpuOperation;
    if ((extra>>13)==3) {
        if ((extra&127)!=0) return nullptr;
        switch ((extra>>10)&7) {
        case 0: return jit_disable.fmove?nullptr:native_fpu_execute<Op::StoreLong,source>;
        case 1:case 5: return jit_disable.fmove?nullptr:native_fpu_execute<Op::StoreFloat,source>;
        default: return nullptr;
        }
    }
    switch (extra&127) {
    case 0x00: return jit_disable.fmove?nullptr:native_fpu_execute<Op::Move,source>;
    case 0x04: return jit_disable.fsqrt?nullptr:native_fpu_execute<Op::SquareRoot,source>;
    case 0x18: return jit_disable.fabs?nullptr:native_fpu_execute<Op::Absolute,source>;
    case 0x1a: return jit_disable.fneg?nullptr:native_fpu_execute<Op::Negate,source>;
    case 0x3a: return jit_disable.ftst?nullptr:native_fpu_execute<Op::Test,source>;
    case 0x38: return jit_disable.fcmp?nullptr:native_fpu_execute<Op::Compare,source>;
    case 0x22: return jit_disable.fadd?nullptr:native_fpu_execute<Op::Add,source>;
    case 0x23: return jit_disable.fmul?nullptr:native_fpu_execute<Op::Multiply,source>;
    case 0x20: return jit_disable.fdiv?nullptr:native_fpu_execute<Op::Divide,source>;
    case 0x28: return jit_disable.fsub?nullptr:native_fpu_execute<Op::Subtract,source>;
    case 0x27: return jit_disable.fsglmul?nullptr:native_fpu_execute<Op::SingleMultiply,source>;
    default: return nullptr;
    }
}

extern unsigned aranym_fpu_branch_mask(unsigned predicate);
template<bool signaling>
static uae_u32 REGPARAM2 native_fpu_set_condition(const NativeFpuInstruction *ins) {
    const unsigned flags=(fpu.fpsr.condition_codes>>24)&15;
    if (signaling && (flags&1)) {
        if (fpu.fpcr&0x8000) return 0;
        fpu.fpsr.exception_status|=0x8000;
        fpu.fpsr.accrued_exception|=0x80;
    }
    const uae_u32 byte=(ins->displacement&(1u<<flags))?255:0;
    auto &data=regs.regs[ins->source];
    data=(data&0xffffff00u)|byte;
    fpu.instruction_address=ins->pc;
    *ins->active=true;
    return 1;
}

extern bool *aranym_modern_fpu_activity_address();
static NativeFpuHandler native_fpu_decode(uae_u32 opcode,uae_u16 extra,unsigned ram_words,
        uae_u16 *code,uae_u32 pc,NativeFpuInstruction &ins,unsigned &words) {
    using Source=NativeFpuSource;
    const unsigned kind=extra>>13,format=(extra>>10)&7;
    ins={pc,0,uae_u8(opcode&7),uae_u8((extra>>7)&7),uae_u8((opcode>>3)&7),aranym_modern_fpu_activity_address()};
    words=2;
    if ((opcode&0xfff8)==0xf240) {
        if (extra>=32 || jit_disable.fscc) return nullptr;
        ins.displacement=aranym_fpu_branch_mask(extra&15);
        return (extra&16)?native_fpu_set_condition<true>:native_fpu_set_condition<false>;
    }
    if (!kind) {
        ins.source=format;
        return native_fpu_handler<Source::Register>(extra);
    }
    if (kind!=2 && kind!=3) return nullptr;
    if (ram_words && (format==1 || format==5 || (kind==3 && !format))) {
        words=ram_words;
        if (words==3) ins.displacement=uae_s16(do_get_mem_word(code+2));
        if (words==4) { memcpy(&ins.displacement,code+2,4); ins.displacement=SDL_SwapBE32(ins.displacement); }
        return format==5?native_fpu_handler<Source::DoubleRam>(extra):native_fpu_handler<Source::SingleRam>(extra);
    }
    if (!ins.ea_mode && !format) return native_fpu_handler<Source::LongDn>(extra);
    if (!ins.ea_mode && format==1) return native_fpu_handler<Source::SingleDn>(extra);
    if ((opcode&63)==0x3c && kind==2 && format==1) {
        words=4;
        memcpy(&ins.displacement,code+2,4); ins.displacement=SDL_SwapBE32(ins.displacement);
        return native_fpu_handler<Source::Immediate>(extra);
    }
    return nullptr;
}
