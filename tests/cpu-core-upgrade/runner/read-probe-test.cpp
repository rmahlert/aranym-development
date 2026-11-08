/* SPDX-License-Identifier: GPL-2.0-or-later
 * Narrow physical TST.B(An) probe: genuine frame delivery, no RTE7 restart.
 */
#include "core.hpp"
#include "c-api.h"
#include <iostream>
#include <stdexcept>
#include <string_view>
using namespace modern040;
namespace {
unsigned checks=0;bool wrong_pc=false;
void expect(bool pass,const char *message){++checks;if(!pass)throw std::runtime_error(message);}
uae_u32 peek(uae_u32 address,unsigned size){uae_u32 v=0;for(unsigned i=0;i<size;++i)v=(v<<8)|host.ram.at(address+i);return v;}
void setup(unsigned sr=0x271f){reset();host.deliver_exceptions=true;host.read_probe_exceptions=true;regs.regs[15]=regs.isp=0x9000;regs.usp=0x7000;regs.msp=0x8000;regs.sr=sr;MakeFromSR();poke(8,4,0x200);poke(0x200,2,0x4e73);}
void frame_and_no_restart() {
    for(unsigned mode:{0x0700u,0x1700u,0x2700u,0x3700u})for(unsigned ccr=0;ccr<32;++ccr)for(unsigned areg=0;areg<8;++areg) {
        setup(mode|ccr);const auto old_sp=regs.regs[15];
        if(areg!=7)regs.regs[8+areg]=0x400;
        const auto address=regs.regs[8+areg],base=(mode&0x1000)?0x8000u:0x9000u,sp=base-60;
        const auto initial=architectural_state();poke(0x100,2,0x4a10|areg);host.denied=Access{AccessKind::Read,address,1,0,false};
        const auto result=run(1);
        expect(result.exit==Exit::Budget&&result.instructions==1&&result.vector==2&&result.address==address&&regs.pc==0x200,"narrow failed read delivers real guest vector within one attempted instruction");
        expect(regs.regs[15]==sp&&host.frames.size()==1&&host.frames[0].format==7&&host.frames[0].stacked_pc==0x102,"read-probe actual frame uses correct supervisor bank and full60bytes");
        expect(peek(sp,2)==(mode|ccr)&&peek(sp+2,4)==(wrong_pc?0x100u:0x102u)&&peek(sp+6,2)==0x7008,"manual format7 saved SR/next-instruction PC/vector");
        expect(peek(sp+12,2)==(0x0120u|((mode&0x2000)?5:1))&&peek(sp+20,4)==address,"physical byte-read SSW size/RW/effectiveFC and fault address");
        expect(peek(sp+14,2)==0&&peek(sp+16,2)==0&&peek(sp+18,2)==0,"synchronous empty-cache read probe has no pending valid writebacks");
        unsigned failed_reads=0;for(const auto &a:host.accesses)if(a.kind==AccessKind::Read&&a.address==address&&!a.completed)++failed_reads;
        expect(failed_reads==1&&!host.access_fault_latched&&!host.delivering_read_probe,"probe is attempted once; successful frame delivery clears no unrelated fault state");
        for(unsigned i=0;i<15;++i)expect(regs.regs[i]==initial.regs[i],"TST failed before any source D/A changes");
        expect(run(1).exit==Exit::Unsupported&&regs.regs[15]==sp,"actual generated RTE7 stays blocked without consuming frame or retrying read");
        (void)old_sp;
    }
}
void discard_handler() {
    setup();regs.regs[0]=0;regs.regs[1]=0x9000;regs.regs[8]=0x10000;regs.regs[9]=0x500;poke(0x9000,4,0x300);
    poke(0x100,2,0x4a10);poke(0x102,2,0x4e71);poke(0x104,2,0x7001);
    // Local EmuTOS vectors.S check_read_byte handler shape: restore vector,
    // restore savedSP inD1, NOP, RTS. Does not attempt RTE/continuation.
    const uae_u16 code[]={0x21c9,0x0008,0x2e41,0x4e71,0x4e75};
    for(unsigned i=0;i<std::size(code);++i)poke(0x200+2*i,2,code[i]);poke(0x300,2,0x4e72);poke(0x302,2,0x2700);
    expect(run(100).exit==Exit::Budget&&regs.pc==0x200,"probe delivery explicitly yields at exception entry even with unused caller budget");
    const auto result=run(100);
    expect(result.exit==Exit::Stopped&&result.instructions==5&&regs.regs[0]==0&&regs.regs[15]==0x9004,"real guest discard-frame handler returns absent result and intact caller stack");
    expect(peek(8,4)==0x500&&host.frames.size()==1,"discard handler restores original bus vector through actual MOVE");
    unsigned probes=0;for(auto a:host.accesses)if(a.kind==AccessKind::Read&&a.address==0x10000)++probes;
    expect(probes==1,"discard recovery never replays failed peripheral/RAM probe read");
}
void fail_closed_and_success() {
    for(uae_u16 opcode:{0x4a50,0x4a18,0x1010,0x1080}) {
        setup();regs.regs[8]=0x400;poke(0x100,2,opcode);host.denied=Access{opcode==0x1080?AccessKind::Write:AccessKind::Read,0x400,4,0,false};
        expect(run(1).exit==Exit::MemoryFault&&host.frames.empty(),"other opcodes/widths/partial-register/write paths retain terminal fault gate");
    }
    for(unsigned trace:{0x8000u,0x4000u}) {setup(0x2700|trace);regs.regs[8]=0x400;poke(0x100,2,0x4a10);host.denied=Access{AccessKind::Read,0x400,1,0,false};expect(run(1).exit==Exit::MemoryFault&&host.frames.empty(),"traced read faults cannot skip pending continuation semantics");}
    setup();host.read_probe_exceptions=false;regs.regs[8]=0x10000;poke(0x100,2,0x4a10);
    expect(run(1).exit==Exit::MemoryFault&&host.frames.empty(),"probe profile is explicit opt-in");
    setup();regs.regs[8]=0x10000;poke(0x100,2,0x4a10);host.denied=Access{AccessKind::Write,0x8fc4,60,0,false};
    expect(run(1).exit==Exit::DoubleFault&&run(1).exit==Exit::DoubleFault,"stack failure during real bus-fault entry halts without retry");
    setup();regs.regs[8]=0x10000;poke(0x100,2,0x4a10);poke(8,4,0x201);
    expect(run(1).exit==Exit::DoubleFault,"odd bus-fault vector halts instead of chaining another fake recovery");
    for(unsigned value:{0u,1u,0x80u,0xffu}){setup();regs.regs[8]=0x400;poke(0x100,2,0x4a10);poke(0x400,1,value);expect(run(1).exit==Exit::Budget&&host.frames.empty()&&regs.pc==0x102&&regs.n==bool(value&0x80)&&regs.z==(value==0)&&regs.x&&!regs.v&&!regs.c,"successful probe remains genuine TST flags/PC without exception");}
    const m040_options options={M040_ABI_VERSION,1,1,0};m040_state state;m040_result result;
    expect(m040_reset(&options)==M040_OK&&m040_read_probe_exceptions(1)==M040_OK&&m040_get_state(&state)==M040_OK,"C profile explicitly enables narrow probe exception");state.regs[15]=state.isp=0x9000;state.regs[8]=0x10000;
    expect(m040_set_state(&state)==M040_OK&&m040_poke(0x100,2,0x4a10)==M040_OK&&m040_poke(8,4,0x200)==M040_OK,"C caller prepares probe/vector");
    expect(m040_run(1,&result)==M040_OK&&result.exit==M040_BUDGET&&result.vector==2&&result.address==0x10000&&result.current_pc==0x200,"C result carries real delivered-probe boundary metadata");
}
}
int main(int argc,char **argv){try{if(argc>2||(argc==2&&std::string_view(argv[1])!="--negative-pc"))throw std::runtime_error("bad mode");wrong_pc=argc==2;install_handlers();frame_and_no_restart();discard_handler();fail_closed_and_success();std::cout<<"PASS: "<<checks<<" bounded real format7 read-probe/discard-handler assertions\n";return 0;}catch(const std::exception &e){std::cerr<<"FAIL: "<<e.what()<<'\n';return wrong_pc&&std::string_view(e.what())=="manual format7 saved SR/next-instruction PC/vector"?1:2;}}
