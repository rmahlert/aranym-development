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
struct Address { std::uint32_t value=0;bool memory=false,ok=false; };
struct SingleMemoryPolicy {
    unsigned size=0,immediate_words=0;
    bool read=false,write=false;
};
struct MultipleMemoryPolicy {
    unsigned size=0,mode=0,reg=0;
    bool load=false;
};
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
    if(mode<2||size_code==3)return {};
    const unsigned size=1u<<size_code,group=opcode&0xff00;
    const bool unary=group==0x4000||group==0x4200||group==0x4400||group==0x4600||group==0x4a00;
    const bool quick=(opcode&0xf000)==0x5000;
    const bool immediate=group==0x0000||group==0x0200||group==0x0400||group==0x0600||group==0x0a00||group==0x0c00;
    if(!unary&&!quick&&!immediate)return {};
    const bool read_only=group==0x4a00||group==0x0c00;
    // Only TST/CMPI can use PC-relative sources; no immediate pseudo-EA.
    if(mode==7&&(reg>3||(!read_only&&reg>1)))return {};
    return {size,immediate?(size==4?2u:1u):0u,group!=0x4200,!read_only};
}
template<class Memory> class Preflight {
    const Image &image;
    Memory &memory;
    std::array<std::uint32_t,16> registers;
    unsigned cursor=1;
    bool good=true;
    std::uint16_t word() {
        if(cursor>=image.count||!memory.allowed(image.pc+cursor*2,2,false)) {
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
    bool multiple_allowed() {
        if(image.count<1||image.count>image.words.size()||!memory.allowed(image.pc,2,false))return false;
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
    bool allowed() {
        if(image.count<1||image.count>image.words.size()||!memory.allowed(image.pc,2,false))return false;
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
}
