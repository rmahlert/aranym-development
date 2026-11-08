/* SPDX-License-Identifier: GPL-2.0-or-later
 * MCP MC68040UM PDF365-367 (§A.5), format4 diagram visually checked.
 * This validates an explicit absent-FPU probe subset, not FPU arithmetic.
 */
#include "core.hpp"
#include <iostream>
#include <stdexcept>
#include <string_view>
using namespace modern040;
namespace {
unsigned checks=0;
bool wrong_format_expectation=false, inject_operand_read=false;
void expect(bool value,const char *message) { ++checks; if(!value) throw std::runtime_error(message); }
uae_u32 peek(uae_u32 a,unsigned size) { uae_u32 v=0;for(unsigned i=0;i<size;++i)v=(v<<8)|host.ram.at(a+i);return v; }
void setup() {
    reset(); host.deliver_exceptions=true; host.lc040_probes=true;
    regs.isp=regs.regs[15]=0x9000; regs.usp=0x7000; regs.msp=0x8000;
    regs.sr=0x2715; MakeFromSR();
    poke(0x2c,4,0x200); poke(0x20,4,0x200); poke(0x200,2,0x4e73);
}
void expected_frame4(uae_u32 next,uae_u32 ea) {
    expect(regs.pc==0x200&&regs.regs[15]==0x8ff0,"recognized absent-FPU op enters16-byte format4");
    expect(peek(0x8ff0,2)==0x2715&&peek(0x8ff2,4)==next&&peek(0x8ff6,2)==(wrong_format_expectation?0x002c:0x402c),"LC040 exact SR/nextPC/format/vector");
    expect(peek(0x8ff8,4)==ea&&peek(0x8ffc,4)==0x100,"LC040 exact EA and distinct faulted-instruction PC");
    expect(host.frames.size()==1&&host.frames[0].format==4&&host.frames[0].stacked_pc==next,"frame4 event metadata agrees with guest bytes");
    expect(regs.fpiar==0x100&&!regs.fp_unimp_ins,"real source absent-FPU helper bookkeeping");
}
void fnop_and_bcc() {
    for(bool wide:{false,true}) for(unsigned condition=0;condition<32;++condition) {
        setup();
        poke(0x100,2,(wide?0xf2c0:0xf280)|condition);
        poke(0x102,wide?4:2,wide?0xfffffffc:0xfffc);
        const auto next=wide?0x106u:0x104u;
        expect(run(1).exit==Exit::Budget,"real FBcc/FNOP absent path delivers guest exception");
        expected_frame4(next,0);
        expect(run(1).exit==Exit::Budget&&regs.pc==next&&regs.regs[15]==0x9000,"actual RTE consumes format4 and resumes next instruction");
        expect(architectural_state().sr==0x2715,"LC040 probe RTE restores complete SR/CCR");
    }
}
void save_restore_effective_addresses() {
    struct Case { uae_u16 opcode; unsigned extensions; uae_u16 first,second; uae_u32 ea; };
    const Case cases[]={
        {0xf350,0,0,0,0x400},{0xf358,0,0,0,0x400},{0xf35f,0,0,0,0x9000},
        {0xf368,1,0x10,0,0x410},{0xf370,1,0x1a04,0,0x40a},
        {0xf378,1,0x400,0,0x400},{0xf379,2,0,0x400,0x400},
        {0xf37a,1,0x10,0,0x112},{0xf37b,1,0x1a04,0,0x10c},
        {0xf310,0,0,0,0x400},{0xf320,0,0,0,0x400},{0xf327,0,0,0,0x9000},
        {0xf328,1,0x10,0,0x410},{0xf330,1,0x1a04,0,0x40a},
        {0xf338,1,0x400,0,0x400},{0xf339,2,0,0x400,0x400}};
    for(const auto &test:cases) {
        setup(); regs.regs[8]=0x400; regs.regs[1]=3;
        poke(0x400,4,0xdeadbeef); poke(0x9000,4,0xaabbccdd);
        poke(0x100,2,test.opcode);
        if(test.extensions)poke(0x102,2,test.first);
        if(test.extensions==2)poke(0x104,2,test.second);
        const auto next=0x102u+2*test.extensions;
        expect(run(1).exit==Exit::Budget,"real FSAVE/FRESTORE decode prefix takes absent exception");
        if(inject_operand_read) (void)read(AccessKind::Read,test.ea,4);
        expected_frame4(next,test.ea);
        expect(regs.regs[8]==0x400,"absent FSAVE/FRESTORE must not perform An data-frame update");
        for(const auto &access:host.accesses)
            if(access.kind==AccessKind::Read) expect(access.address==0x2c,"absence path must not read/restore a fabricated FPU operand frame");
        expect(peek(0x400,4)==0xdeadbeef&&peek(0x9000,4)==0xaabbccdd,"absent operation leaves source/destination canaries unchanged");
        expect(run(1).exit==Exit::Budget&&regs.pc==next&&regs.regs[15]==0x9000,"FSAVE/FRESTORE absent recovery via actual RTE4");
    }
}
void privilege_and_fail_closed() {
    for(uae_u16 opcode:{0xf327,0xf35f}) {
        setup(); regs.sr=0x0715; MakeFromSR(); poke(0x100,2,opcode);
        expect(run(1).exit==Exit::Budget&&host.frames.size()==1&&host.frames[0].vector==8&&host.frames[0].format==0,
               "FSAVE/FRESTORE user privilege failure precedes no-FPU path");
        expect(peek(0x8ffa,4)==0x100&&peek(0x8ffe,2)==0x20,"privilege frame stores faulting opcode PC");
    }
    setup(); host.lc040_probes=false; poke(0x100,2,0xf35f);
    expect(run(1).exit==Exit::Unsupported&&regs.pc==0x100&&host.frames.empty(),"profile gate cannot silently skip FRESTORE");
    host.lc040_probes=true;
    expect(run(1).exit==Exit::Unsupported&&run(1).instructions==0,"unsupported profile continuation requires reset");
    setup(); poke(0x100,2,0xf200); poke(0x102,2,0);
    expect(run(1).exit==Exit::Unsupported,"unimplemented arithmetic-FPU decode remains excluded, not approximated");
    setup(); poke(0x100,2,0xffff);
    expect(run(1).exit==Exit::Budget&&host.frames[0].format==0&&peek(0x8ffa,4)==0x100,
           "ordinary undefined Fline retains format0 instead of recognized-FPU format4");
}
void emutos_probe_shape() {
    // Matches the key local EmuTOS processor.S probe shape, not asserted to be
    // the exact source revision used for a particular ROM image.
    setup(); regs.regs[0]=0; regs.regs[8]=0x9000;
    poke(0x9000,4,0x300); // saved caller return address
    const uae_u16 code[]={0x42a7,0x42a7,0x42a7,0xf35f,0xf280,0x0000,0xf327};
    for(unsigned i=0;i<std::size(code);++i)poke(0x100+2*i,2,code[i]);
    poke(0x200,2,0x2e48); poke(0x202,2,0x4e75); // MOVEA.L A0,A7; RTS (fexit shape)
    poke(0x300,2,0x4e72); poke(0x302,2,0x2700);
    const auto result=run(100);
    expect(result.exit==Exit::Stopped&&result.instructions==7,"EmuTOS-shaped null-frame/FRESTORE/fexit path executes actual guest instructions");
    expect(regs.regs[0]==0&&regs.regs[15]==0x9004&&regs.pc==0x300,"absence cookie result and fexit caller stack restored");
    expect(host.frames.size()==1&&host.frames[0].format==4&&peek(0x8ff0,4)==0x106&&peek(0x8fec,4)==0x8ff4,
           "probe fails at recognized FRESTORE, before FNOP/FSAVE");
    for(const auto &access:host.accesses) if(access.kind==AccessKind::Read)
        expect(access.address==0x2c||access.address==0x9000,"no read of pushed NULL FPU frame during absent probe");
}
}
int main(int argc,char **argv) {
    const std::string_view mode=argc==2?argv[1]:"";
    try {
        if(argc>2||(argc==2&&mode!="--negative-format"&&mode!="--negative-read")) throw std::runtime_error("unknown probe-test mode");
        wrong_format_expectation=mode=="--negative-format";inject_operand_read=mode=="--negative-read";
        install_handlers(); fnop_and_bcc(); save_restore_effective_addresses(); privilege_and_fail_closed(); emutos_probe_shape();
        std::cout<<"PASS: "<<checks<<" real LC040 recognized-FPU probe/EA/format4/RTE assertions\n";return 0;
    } catch(const std::exception &error) {
        const std::string_view message=error.what();
        if((mode=="--negative-format"&&message=="LC040 exact SR/nextPC/format/vector")||
           (mode=="--negative-read"&&message.find("must not read/restore")!=std::string_view::npos)) {
            std::cout<<"REJECTED deliberate "<<mode<<" by actual LC040 probe checker\n";return 1;
        }
        std::cerr<<"FAIL: "<<error.what()<<'\n';return 2;
    }
}
