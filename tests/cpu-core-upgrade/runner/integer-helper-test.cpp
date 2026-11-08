/* SPDX-License-Identifier: GPL-2.0-or-later
 * Independent integer arithmetic/bitstream oracles through actual dispatch.
 */
#include "core.hpp"
#include <algorithm>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string_view>
using namespace modern040;
namespace {
unsigned checks=0; bool corrupt=false;
void expect(bool value,const char *message) { ++checks;if(!value)throw std::runtime_error(message); }
void setup(unsigned ccr=0) { reset(); regs.isp=regs.regs[15]=0x9000;regs.sr=0x2700|ccr;MakeFromSR();host.log_accesses=false; }
uae_u32 bits(const std::array<uae_u8,65536> &ram,int start,unsigned width) {
    uae_u32 result=0;for(unsigned i=0;i<width;++i) { const auto bit=unsigned(start+int(i));result=(result<<1)|((ram[bit/8]>>(7-bit%8))&1); }return result;
}
void setbits(std::array<uae_u8,65536> &ram,int start,unsigned width,uae_u32 value) {
    for(unsigned i=0;i<width;++i) {const auto bit=unsigned(start+int(i));const auto mask=uae_u8(1u<<(7-bit%8));ram[bit/8]=(ram[bit/8]&~mask)|((value>>(width-1-i)&1)?mask:0);}
}
void bitfields() {
    for(unsigned op=0;op<8;++op)for(int offset=-17;offset<=39;++offset)for(unsigned width=1;width<=32;++width) {
        setup((offset&1)?31:0);regs.regs[8]=0x400;regs.regs[1]=uae_u32(offset);regs.regs[2]=0xa15e739c;
        for(unsigned i=0x3f0;i<0x410;++i)host.ram[i]=uae_u8(i*37+offset*11+width);
        poke(0x100,2,0xe8d0|(op<<8));poke(0x102,2,0x2840|(width&31)); // D2 result/source; dynamic offset D1.
        const auto before=host.ram;auto expected=before;const auto field=bits(before,0x400*8+offset,width);
        const auto mask=width==32?0xffffffffu:(uae_u32{1}<<width)-1;
        auto value=field,result=regs.regs[2];
        switch(op) {
        case 1:result=field;break;
        case 2:value=field^mask;setbits(expected,0x400*8+offset,width,value);break;
        case 3:result=(field&(uae_u32{1}<<(width-1)))?(field|~mask):field;break;
        case 4:value=0;setbits(expected,0x400*8+offset,width,value);break;
        case 5:{unsigned leading=0;while(leading<width&&!(field&(uae_u32{1}<<(width-1-leading))))++leading;result=uae_u32(offset)+leading;break;}
        case 6:value=mask;setbits(expected,0x400*8+offset,width,value);break;
        case 7:value=regs.regs[2]&mask;setbits(expected,0x400*8+offset,width,value);break;
        }
        const auto r=run(1);const auto flagvalue=op==7?value:field;
        expect(r.exit==Exit::Budget&&regs.pc==0x104,"bitfield dispatch completes exact extension length");
        expect(regs.regs[2]==(corrupt?result^1:result)&&regs.regs[1]==uae_u32(offset)&&regs.regs[8]==0x400,"bitfield signed offset/extract/leading/source registers");
        expect(host.ram==expected,"bit-by-bit memory oracle preserves all outside-field bits");
        expect(regs.n==bool(flagvalue&(uae_u32{1}<<(width-1)))&&regs.z==(flagvalue==0)&&!regs.v&&!regs.c&&regs.x==bool(offset&1),"bitfield independent NZVC and preserved X");
    }
    for(unsigned encoded_width:{0u,1u,31u,32u,33u,63u,0xffffffffu}) {
        setup(31);regs.regs[8]=0x400;regs.regs[1]=5;regs.regs[3]=encoded_width;
        poke(0x400,4,0xa5e19c72);poke(0x404,1,0x5a);poke(0x100,2,0xe9d0);poke(0x102,2,0x2863);
        const auto width=((encoded_width-1)&31)+1,field=bits(host.ram,0x400*8+5,width);
        expect(run(1).exit==Exit::Budget&&regs.regs[2]==field&&regs.regs[3]==encoded_width,"dynamic bitfield width normalization including zero/full32 and source preservation");
    }
    // Real five-byte field access: long followed by byte, then same ordered writes.
    setup();host.log_accesses=true;regs.regs[8]=0x400;poke(0x100,2,0xead0);poke(0x102,2,0x01c0);
    expect(run(1).exit==Exit::Budget,"cross-long bitfield executes");
    std::vector<Access> data;for(auto a:host.accesses)if(a.kind!=AccessKind::Fetch)data.push_back(a);
    expect(data.size()==4&&data[0].kind==AccessKind::Read&&data[0].address==0x400&&data[0].size==4&&data[1].address==0x404&&data[1].size==1&&data[2].kind==AccessKind::Write&&data[2].address==0x400&&data[2].size==4&&data[3].address==0x404&&data[3].size==1,"five-byte bitfield preserves ordered long/byte read-modify-write bus contract");
    setup();host.log_accesses=true;regs.regs[8]=0x400;poke(0x100,2,0xead0);poke(0x102,2,0x01c0);host.denied=Access{AccessKind::Read,0x404,1,0,false};const auto before=host.ram;
    expect(run(1).exit==Exit::MemoryFault&&host.ram==before,"second bitfield read failure cannot perform write phase");
    const auto accesses=host.accesses.size();expect(run(1).instructions==0&&host.accesses.size()==accesses,"partly decoded fault cannot silently retry");
}
void word_division() {
    const uae_u32 dividends[]={0,1,0xffff,0x10000,0x7fffffff,0x80000000,0xffffffff};
    const uae_u16 divisors[]={0,1,2,0x7fff,0x8000,0xffff};
    for(bool sign:{false,true})for(auto a:dividends)for(auto b:divisors)for(unsigned ccr=0;ccr<32;++ccr) {
        setup(ccr);regs.regs[0]=b;regs.regs[1]=a;poke(0x100,2,sign?0x83c0:0x82c0);
        const auto av=sign?std::int64_t(uae_s32(a)):std::int64_t(a),bv=sign?std::int64_t(uae_s16(b)):std::int64_t(b);
        const auto r=run(1);
        if(!b) {expect(r.exit==Exit::GuestException&&r.vector==5&&regs.pc==0x102,"word divide zero exception/nextPC");expect(regs.regs[1]==a&&!regs.c&&regs.n==bool(ccr&8)&&regs.z==bool(ccr&4)&&regs.v==bool(ccr&2),"040 divide zero preserves NZV/destination and clears C");}
        else {const auto q=av/bv,rem=av%bv;const bool overflow=sign?(q<-32768||q>32767):q>65535;
            expect(r.exit==Exit::Budget&&regs.pc==0x102,"word divide completion");
            expect(regs.regs[1]==(overflow?a:(uae_u32(rem)<<16)|uae_u16(q)),"word divide independent quotient/remainder or unchanged overflow destination");
            expect(!regs.c&&regs.v==overflow&&regs.n==(overflow?bool(ccr&8):bool(uae_u16(q)&0x8000))&&regs.z==(overflow?bool(ccr&4):uae_u16(q)==0),"040 word divide success/overflow flags");}
        expect(regs.x==bool(ccr&16)&&regs.regs[0]==b,"word divide preserves X/source");
    }
}
void long_multiply_divide() {
    const uae_u32 values[]={0,1,2,0x7fffffff,0x80000000,0xfffffffe,0xffffffff};
    for(bool sign:{false,true})for(bool wide:{false,true})for(auto a:values)for(auto b:values)for(unsigned ccr:{0u,31u}) {
        setup(ccr);regs.regs[0]=b;regs.regs[1]=a;regs.regs[2]=0xaabbccdd;poke(0x100,2,0x4c00);poke(0x102,2,0x1002|(sign?0x800:0)|(wide?0x400:0));
        const auto product=sign?uae_u64(std::int64_t(uae_s32(a))*std::int64_t(uae_s32(b))):uae_u64(a)*b;
        const auto low=uae_u32(product),high=uae_u32(product>>32);const bool overflow=!wide&&(sign?(product!=uae_u64(std::int64_t(uae_s32(low)))):high!=0);
        expect(run(1).exit==Exit::Budget&&regs.pc==0x104,"long multiply decoded completion");
        expect(regs.regs[1]==low&&regs.regs[2]==(wide?high:0xaabbccdd)&&regs.regs[0]==b,"long multiply independent full-width oracle and destination ordering");
        expect(regs.v==overflow&&!regs.c&&regs.z==(wide?product==0:low==0)&&regs.n==(wide?bool(product>>63):bool(low>>31))&&regs.x==bool(ccr&16),"long multiply width/signed overflow flags");
    }
    const uae_u64 numerators[]={0,1,0xffffffff,0x100000000ULL,0x7fffffffffffffffULL,0x8000000000000000ULL,0xffffffffffffffffULL};
    for(bool sign:{false,true})for(bool wide:{false,true})for(auto raw:numerators)for(auto b:values)for(unsigned ccr:{0u,31u}) {
        setup(ccr);const auto low=uae_u32(raw),high=wide?uae_u32(raw>>32):0x13579bdfu;regs.regs[0]=b;regs.regs[1]=low;regs.regs[2]=high;poke(0x100,2,0x4c40);poke(0x102,2,0x1002|(sign?0x800:0)|(wide?0x400:0));
        const __int128 a=sign?(wide?__int128(uae_s64(raw)):__int128(uae_s32(low))):(wide?__int128(raw):__int128(low));const __int128 divisor=sign?__int128(uae_s32(b)):__int128(b);
        const auto r=run(1);
        if(!b) {expect(r.exit==Exit::GuestException&&r.vector==5&&regs.pc==0x104,"long divide zero actual exception/nextPC");expect(regs.regs[1]==low&&regs.regs[2]==high&&!regs.c&&regs.v==bool(ccr&2)&&regs.n==bool(ccr&8)&&regs.z==bool(ccr&4),"040 long divide zero preserves NZV/registers");}
        else {const auto q=a/divisor,rem=a%divisor;const bool overflow=sign?(q<(-__int128(2147483647)-1)||q>2147483647):q>__int128(0xffffffffu);
            expect(r.exit==Exit::Budget&&regs.pc==0x104,"long divide decoded completion");
            expect(regs.regs[1]==(overflow?low:uae_u32(q))&&regs.regs[2]==(overflow?high:uae_u32(rem)),"long divide independent128-bit quotient/remainder and overflow destination");
            expect(!regs.c&&regs.v==overflow&&regs.n==(overflow?bool(ccr&8):bool(uae_u32(q)>>31))&&regs.z==(overflow?bool(ccr&4):uae_u32(q)==0),"040 long divide success/overflow flags");}
        expect(regs.x==bool(ccr&16)&&regs.regs[0]==b,"long divide preserves X/source");
    }
    setup();regs.regs[0]=3;regs.regs[1]=0xffffffff;poke(0x100,2,0x4c00);poke(0x102,2,0x1401);
    expect(run(1).exit==Exit::Budget&&regs.regs[1]==0xfffffffd,"040 aliased wide multiply destination retains low product");
    setup();regs.regs[0]=2;regs.regs[1]=7;poke(0x100,2,0x4c40);poke(0x102,2,0x1001);
    expect(run(1).exit==Exit::Budget&&regs.regs[1]==3,"aliased divide result retains quotient after remainder write");
}
void checks_and_real_exception() {
    for(uae_s16 bound:{uae_s16(-5),uae_s16(0),uae_s16(10)})for(uae_s16 value:{uae_s16(-7),uae_s16(-1),uae_s16(0),uae_s16(5),uae_s16(10),uae_s16(11)})for(unsigned ccr=0;ccr<32;++ccr) {
        setup(ccr);regs.regs[0]=uae_u16(bound);regs.regs[1]=uae_u16(value);poke(0x100,2,0x4380);const bool fault=value<0||value>bound;const auto r=run(1);
        expect(r.exit==(fault?Exit::GuestException:Exit::Budget)&&(!fault||r.vector==6)&&regs.pc==0x102,"CHK.W bounds/negative vector6 completion");
        const bool carry=fault&&((value<0&&bound>=0)||(bound>=0&&value>=bound)||(value<0&&bound<value));
        expect(regs.n==(value<0)&&regs.c==carry&&regs.z==bool(ccr&4)&&regs.v==bool(ccr&2)&&regs.x==bool(ccr&16),"040 CHK source-dependent flags with preserved XZV");
    }
    for(bool trap:{false,true})for(int lower:{-10,0,10})for(int upper:{-5,0,20})for(int value:{-20,-10,-5,0,5,10,20,30}) {
        setup(31);regs.regs[8]=0x400;regs.regs[0]=uae_u32(value);poke(0x100,2,0x00d0);poke(0x102,2,trap?0x800:0);poke(0x400,1,uae_u8(lower));poke(0x401,1,uae_u8(upper));const bool equal=value==lower||value==upper,carry=!equal&&(lower<=upper?(value<lower||value>upper):(value>upper&&value<lower));const auto r=run(1);
        expect(r.exit==((trap&&carry)?Exit::GuestException:Exit::Budget)&&(!(trap&&carry)||r.vector==6),"CHK2/CMP2 wrapped and ordinary intervals");
        expect(regs.z==equal&&regs.c==carry&&regs.n&&regs.v&&regs.x&&regs.pc==0x104,"040 CHK2/CMP2 flags/extension PC");
    }
    // Actual divide-zero format2 then actual generated RTE to next instruction.
    setup(31);host.deliver_exceptions=true;regs.regs[1]=123;poke(0x100,2,0x82c0);poke(20,4,0x200);poke(0x200,2,0x4e73);
    expect(run(1).exit==Exit::Budget&&host.frames.size()==1&&host.frames[0].format==2&&host.frames[0].stacked_pc==0x102,"real integer divide fault delivers format2/nextPC");
    expect(run(1).exit==Exit::Budget&&regs.pc==0x102&&regs.regs[15]==0x9000&&regs.regs[1]==123,"actual RTE recovers real divide helper exception");
}
}
int main(int argc,char **argv) {
    try {if(argc>2||(argc==2&&std::string_view(argv[1])!="--negative-bitfield"))throw std::runtime_error("bad mode");corrupt=argc==2;install_handlers();bitfields();word_division();long_multiply_divide();checks_and_real_exception();std::cout<<"PASS: "<<checks<<" generated integer-helper assertions\n";return 0;}
    catch(const std::exception &e) {std::cerr<<"FAIL: "<<e.what()<<"\n";return corrupt&&std::string_view(e.what())=="bitfield signed offset/extract/leading/source registers"?1:2;}
}
