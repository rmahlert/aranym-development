/* SPDX-License-Identifier: GPL-2.0-or-later
 * Pure 68040 EA preflight for the native physical-RAM path. No register writes
 * or operand/device transfers. Full-format indirection peeks only checked RAM.
 */
#pragma once
#include <array>
#include <cstdint>
#include <type_traits>
namespace modern040::native_ea {
struct Image {
    std::uint32_t pc=0,count=0;
    std::array<std::uint16_t,12> words{};
};
static_assert(sizeof(Image)==32&&std::is_trivially_copyable_v<Image>);
struct CodeSpan {std::uint64_t end=0;unsigned words=0;};
// No wrapping code images and no speculative fetch beyond their backing range.
constexpr CodeSpan code_span(std::uint32_t pc,std::uint32_t base,std::uint32_t size) {
    const auto end=std::uint64_t(base)+size;
    if(!size||end>0x100000000ULL||pc<base||std::uint64_t(pc)+2>end)return {};
    const auto words=(end-pc)/2;
    return {end,words<12?unsigned(words):12u};
}

struct Address { std::uint32_t value=0;bool memory=false,ok=false; };
struct SingleMemoryPolicy {
    unsigned size=0,immediate_words=0;
    bool read=false,write=false;
};
struct MultipleMemoryPolicy {
    unsigned size=0,mode=0,reg=0;
    bool load=false;
};
constexpr bool memory_bitfield_opcode(std::uint16_t opcode) {
    const unsigned mode=(opcode>>3)&7,reg=opcode&7;
    return (opcode&0xf8c0)==0xe8c0&&mode>=2&&mode!=3&&mode!=4&&
        !(mode==7&&reg>3);
}
// MOVEM preflight only; no native admission is implied by this policy.
constexpr MultipleMemoryPolicy multiple_memory_policy(std::uint16_t opcode) {
    if((opcode&0xfb80)!=0x4880)return {};
    const unsigned mode=(opcode>>3)&7,reg=opcode&7;
    const bool load=bool(opcode&0x400);
    if(mode<2||(load&&mode==4)||(!load&&mode==3))return {};
    if(mode==7&&reg>(load?3u:1u))return {};
    return {opcode&0x40?4u:2u,mode,reg,load};
}
// Numeric instruction encodings, independently exercised by literal tests.
// This is a preflight policy, not admission to the native compiler table.
constexpr SingleMemoryPolicy single_memory_policy(std::uint16_t opcode) {
    const unsigned mode=(opcode>>3)&7,reg=opcode&7,size_code=(opcode>>6)&3;
    if(mode<2)return {};
    const unsigned group=opcode&0xff00;
    const bool unary=group==0x4000||group==0x4200||group==0x4400||group==0x4600||group==0x4a00;
    const bool quick=(opcode&0xf000)==0x5000&&size_code!=3;
    const bool immediate=group==0x0000||group==0x0200||group==0x0400||group==0x0600||group==0x0a00||group==0x0c00;
    if(unary||quick||immediate) {
        if(size_code==3)return {};
        const unsigned size=1u<<size_code;
        const bool read_only=group==0x4a00||group==0x0c00;
        // Only TST/CMPI can use PC-relative sources; no immediate pseudo-EA.
        if(mode==7&&(reg>3||(!read_only&&reg>1)))return {};
        return {size,immediate?(size==4?2u:1u):0u,group!=0x4200,!read_only};
    }

    // Ordinary two-operand integer ALU forms.  The register-source direction
    // updates memory; the EA-source direction, ADDA/SUBA/CMPA and MUL read it.
    // Register-only ADDX/SUBX/ABCD/SBCD/EXG encodings have mode<2 above.
    const unsigned top=opcode>>12,opmode=(opcode>>6)&7;
    if(top==8||top==9||top==0xb||top==0xc||top==0xd) {
        unsigned size=0;bool write=false;
        if(opmode<=2)size=1u<<opmode;
        else if(opmode>=4&&opmode<=6){size=1u<<(opmode-4);write=true;}
        else if((top==9||top==0xb||top==0xd)&&(opmode==3||opmode==7))size=opmode==3?2u:4u;
        else if(top==0xc&&(opmode==3||opmode==7))size=2; // MULU/MULS
        if(!size)return {};
        if(mode==7&&(reg>3||(write&&reg>1)))return {};
        return {size,0,true,write};
    }

    // Dynamic/static bit operations use a byte operand in memory.  Static
    // forms have one immediate extension word before the EA extension.
    const bool dynamic_bit=(opcode&0xf100)==0x0100;
    const bool static_bit=(opcode&0xff00)==0x0800;
    if(dynamic_bit||static_bit) {
        const unsigned operation=(opcode>>6)&3;
        const bool write=operation!=0; // BTST is read-only.
        if(mode==7&&(reg>3||(write&&reg>1)))return {};
        return {1,static_bit?1u:0u,true,write};
    }

    // Memory shifts/rotates are word-sized read-modify-write operations.
    if((opcode&0xf8c0)==0xe0c0) {
        if(mode==7&&reg>1)return {};
        return {2,0,true,true};
    }

    // Scc writes one byte and does not read the old destination value.
    if((opcode&0xf0c0)==0x50c0) {
        if(mode==7&&reg>1)return {};
        return {1,0,false,true};
    }
    return {};
}
template<class Memory> class Preflight {
    const Image &image;
    Memory &memory;
    std::array<std::uint32_t,16> registers;
    unsigned cursor=1;
    bool good=true;
    template<class M> static auto permitted_code(M &m,std::uint32_t address,unsigned size,int)
        -> decltype(m.code_allowed(address,size)) {return m.code_allowed(address,size);}
    template<class M> static bool permitted_code(M &m,std::uint32_t address,unsigned size,long)
        {return m.allowed(address,size,false);}
    bool code_allowed(std::uint32_t address,unsigned size) {return permitted_code(memory,address,size,0);}
    std::uint16_t word() {
        if(cursor>=image.count||!code_allowed(image.pc+cursor*2,2)) {
            good=false;return 0;
        }
        return image.words[cursor++];
    }
    std::uint32_t longword() {
        const auto hi=std::uint32_t(word());return (hi<<16)|word();
    }
    std::uint32_t indexed(std::uint32_t base) {
        const auto extension=word();
        auto index=registers[(extension>>12)&15];
        if(!(extension&0x800))index=std::uint32_t(std::int32_t(std::int16_t(index)));
        index<<=(extension>>9)&3;
        if(!(extension&0x100))return base+index+std::uint32_t(std::int32_t(std::int8_t(extension)));
        // Reserved forms deopt; do not invent a native interpretation.
        const unsigned bd=(extension>>4)&3,iis=extension&7;
        // M68000PRM Table2-2: with IS=1 every I/IS=1xx is reserved.
        if(!bd||(extension&8)||iis==4||((extension&0x40)&&iis>=4)){good=false;return 0;}
        if(extension&0x80)base=0;
        if(extension&0x40)index=0;
        if(bd==2)base+=std::uint32_t(std::int32_t(std::int16_t(word())));
        else if(bd==3)base+=longword();
        std::uint32_t outer=0;
        if((iis&3)==2)outer=std::uint32_t(std::int32_t(std::int16_t(word())));
        else if((iis&3)==3)outer=longword();
        if(!good)return 0;
        if(!iis)return base+index;
        const auto pointer_address=base+(iis<4?index:0);
        std::uint32_t pointer=0;
        if(!memory.allowed(pointer_address,4,false)||!memory.peek32(pointer_address,pointer)) {
            good=false;return 0;
        }
        return pointer+outer+(iis>4?index:0);
    }
    Address ea(unsigned mode,unsigned reg,unsigned size,bool destination=false) {
        const auto increment=size==1&&reg==7?2:size;
        switch(mode) {
        case 0:case 1:return {0,false,true};
        case 2:return {registers[8+reg],true,true};
        case 3:{const auto address=registers[8+reg];registers[8+reg]+=increment;return {address,true,true};}
        case 4:registers[8+reg]-=increment;return {registers[8+reg],true,true};
        case 5:{const auto displacement=std::int16_t(word());return {registers[8+reg]+std::uint32_t(std::int32_t(displacement)),true,good};}
        case 6:{const auto address=indexed(registers[8+reg]);return {address,true,good};}
        case 7:
            if(reg==0){const auto value=std::uint32_t(std::int32_t(std::int16_t(word())));return {value,true,good};}
            if(reg==1){const auto value=longword();return {value,true,good};}
            if(destination)return {};
            if(reg==2){const auto base=image.pc+cursor*2;const auto displacement=std::int16_t(word());return {base+std::uint32_t(std::int32_t(displacement)),true,good};}
            if(reg==3){const auto address=indexed(image.pc+cursor*2);return {address,true,good};}
            if(reg==4){if(size==4)longword();else word();return {0,false,good};}
            return {};
        default:return {};
        }
    }
public:
    Preflight(const Image &i,const std::array<std::uint32_t,16>&r,Memory &m):image(i),memory(m),registers(r){}
    bool control_address(std::uint32_t &target) {
        if(image.count<1||image.count>image.words.size()||!code_allowed(image.pc,2))return false;
        const auto opcode=image.words[0];
        const bool jsr=(opcode&0xffc0)==0x4e80;
        const bool jmp=(opcode&0xffc0)==0x4ec0;
        const bool pea=(opcode&0xffc0)==0x4840;
        if(!jsr&&!jmp&&!pea)return false;
        const unsigned mode=(opcode>>3)&7,reg=opcode&7;
        // Address-producing control EAs never update an address register.
        if(mode<2||mode==3||mode==4||(mode==7&&reg>3))return false;
        const auto address=ea(mode,reg,4);
        if(!good||!address.ok||!address.memory)return false;
        target=address.value;return true;
    }
    bool multiple_allowed() {
        if(image.count<1||image.count>image.words.size()||!code_allowed(image.pc,2))return false;
        const auto opcode=image.words[0];
        const auto multiple=multiple_memory_policy(opcode);
        if(multiple.size) {
            auto mask=std::uint32_t(word());unsigned count=0;
            // Empty lists retain the genuine interpreter's EA/bus behavior
            // until separately qualified; never add a speculative pointer
            // read merely to optimize a zero-transfer instruction.
            if(!good||!mask)return false;
            for(;mask;mask&=mask-1)++count;
            const unsigned span=count*multiple.size;
            Address operand;
            if(multiple.mode==4) {
                // Predecrement stores cover the complete lower span, not
                // just the first element. Mask order does not change bounds.
                operand={registers[8+multiple.reg]-span,true,true};
            } else {
                // Postincrement loads start at the original address; no
                // caller-visible register update occurs in this planner.
                operand=ea(multiple.mode==3?2:multiple.mode,multiple.reg,multiple.size,!multiple.load);
            }
            return good&&operand.ok&&operand.memory&&
                memory.allowed(operand.value,span,!multiple.load);
        }
        return false;
    }
    bool bitfield_allowed() {
        if(image.count<2||image.count>image.words.size()||
           !code_allowed(image.pc,4))return false;
        const auto opcode=image.words[0];
        if(!memory_bitfield_opcode(opcode))return false;
        const auto extension=image.words[1];
        cursor=2; // bitfield specifier precedes the EA extension words
        const unsigned mode=(opcode>>3)&7,reg=opcode&7,operation=(opcode>>8)&7;
        const bool write=operation==2||operation==4||operation==6||operation==7;
        if(write&&mode==7&&reg>1)return false;
        const auto operand=ea(mode,reg,1,write);
        if(!good||!operand.ok||!operand.memory)return false;
        const auto raw_offset=(extension&0x800)?registers[(extension>>6)&7]:
            std::uint32_t((extension>>6)&31);
        const auto offset=std::int32_t(raw_offset);
        const unsigned width=(extension&0x20)?(registers[extension&7]&31):
            unsigned(extension&31);
        const unsigned actual_width=width?width:32;
        // Arithmetic right shift gives floor(offset/8), which is the byte
        // containing the first bit for both positive and negative offsets.
        const auto byte_delta=offset>=0?offset/8:-std::int32_t((std::uint32_t(-std::int64_t(offset))+7)/8);
        const unsigned first_bit=unsigned(offset-8*byte_delta);
        const unsigned span=(first_bit+actual_width+7)/8;
        const auto address=std::uint32_t(operand.value+std::uint32_t(byte_delta));
        if(std::uint64_t(address)+span>0x100000000ULL)return false;
        return memory.allowed(address,span,false)&&(!write||memory.allowed(address,span,true));
    }
    bool allowed() {
        if(image.count<1||image.count>image.words.size()||!code_allowed(image.pc,2))return false;
        const auto opcode=image.words[0];const unsigned top=opcode>>12;
        if(top>=1&&top<=3) {
            const unsigned size=top==1?1:top==2?4:2;
            const unsigned sm=(opcode>>3)&7,dm=(opcode>>6)&7;
            if(size==1&&(sm==1||dm==1))return false;
            const auto source=ea(sm,opcode&7,size);
            const auto destination=ea(dm,(opcode>>9)&7,size,true);
            return good&&source.ok&&destination.ok&&
                (!source.memory||memory.allowed(source.value,size,false))&&
                (!destination.memory||memory.allowed(destination.value,size,true));
        }
        if((opcode&0xf1c0)==0x41c0) {
            const unsigned mode=(opcode>>3)&7,reg=opcode&7;
            if(mode<2||mode==3||mode==4||(mode==7&&reg>3))return false;
            const auto source=ea(mode,reg,4);
            // LEA computes an address; the final target need not be mapped.
            return good&&source.ok;
        }
        if(memory_bitfield_opcode(opcode))return bitfield_allowed();
        const auto policy=single_memory_policy(opcode);
        if(policy.size) {
            // Immediate data appears before EA extension words. Consume it
            // without reading an operand or changing the caller's registers.
            for(unsigned i=0;i<policy.immediate_words;++i)word();
            const auto operand=ea((opcode>>3)&7,opcode&7,policy.size,policy.write);
            return good&&operand.ok&&operand.memory&&
                (!policy.read||memory.allowed(operand.value,policy.size,false))&&
                (!policy.write||memory.allowed(operand.value,policy.size,true));
        }
        return false;
    }
};
template<class Memory> bool allowed(const Image&i,const std::array<std::uint32_t,16>&r,Memory&m) {
    return Preflight<Memory>(i,r,m).allowed();
}
template<class Memory> bool movem_allowed(const Image&i,const std::array<std::uint32_t,16>&r,Memory&m) {
    return Preflight<Memory>(i,r,m).multiple_allowed();
}
template<class Memory> bool control_address(const Image&i,const std::array<std::uint32_t,16>&r,Memory&m,std::uint32_t&target) {
    return Preflight<Memory>(i,r,m).control_address(target);
}
}
