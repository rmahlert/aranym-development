/* SPDX-License-Identifier: GPL-2.0-or-later
 * Real ARAnyM private generated bodies, mocked external replies only.
 */
#include "core.hpp"
#include <iostream>
#include <stdexcept>
#include <string_view>
using namespace modern040;
namespace {
unsigned checks=0;bool wrong_pc=false;
void expect(bool pass,const char *message){++checks;if(!pass)throw std::runtime_error(message);}
void setup(unsigned sr=0x271f) {
    reset();host.private_operations=true;regs.isp=regs.regs[15]=0x9000;regs.usp=0x7000;regs.msp=0x8000;regs.sr=sr;MakeFromSR();
    for(unsigned i=0;i<15;++i)regs.regs[i]=0x12340000+i;
}
void suspend_resume() {
    for(auto op:{0x7300u,0x7301u,0x7101u,0x71ffu})for(bool supervisor:{false,true})for(unsigned ccr=0;ccr<32;++ccr) {
        setup((supervisor?0x2700:0x0700)|ccr);poke(0x100,2,op);poke(0x102,2,0x4e71);const auto before=architectural_state();
        const auto r=run(10);
        expect(r.exit==Exit::HostService&&r.instructions==1&&regs.pc==0x100&&host.service.has_value(),"real private handler suspends exactly once at host helper before PC advancement");
        const auto service=*host.service;
        expect(service.kind==(op==0x7300?ServiceKind::NatFeatId:op==0x7301?ServiceKind::NatFeatCall:ServiceKind::EmulOp)&&service.opcode==op&&service.stack==before.regs[15]+4&&service.supervisor==supervisor,"private service exposes exact opcode/kind/stack argument pointer and privilege");
        const auto suspended=architectural_state();
        for(unsigned i=0;i<16;++i)expect(suspended.regs[i]==before.regs[i],"suspension preserves every D/A register");
        expect(suspended.pc==before.pc&&suspended.sr==before.sr&&suspended.usp==before.usp&&suspended.isp==before.isp&&suspended.msp==before.msp,"suspension preserves architectural PC/SR/banks; opcode/trace cursor are decoded bookkeeping");
        const auto accesses=host.accesses.size();expect(run(10).exit==Exit::HostService&&run(10).instructions==0&&host.accesses.size()==accesses,"pending host service cannot refetch or invoke twice");
        expect(!inject_interrupt(64,7),"IRQ cannot interrupt partially completed private helper");
        regs.regs[0]=0x89abcdef;const auto done=complete_service();
        expect(done.exit==Exit::Budget&&done.instructions==0&&!host.service&&regs.pc==(wrong_pc?0x104u:0x102u),"genuine private handler continuation advances PC once without counting a second guest instruction");
        const auto after=architectural_state();
        for(unsigned i=1;i<16;++i)expect(after.regs[i]==before.regs[i],"external D0 reply preserves D1-D7/A0-A7");
        expect(after.regs[0]==0x89abcdef&&after.sr==before.sr&&after.usp==before.usp&&after.isp==before.isp&&after.msp==before.msp,"private reply preserves CCR/SR/banked stacks and returned D0");
        expect(host.accesses.size()==accesses,"completion reuses generated helper body without opcode refetch");
        expect(run(1).exit==Exit::Budget&&regs.pc==0x104,"next ordinary instruction executes after private host reply");
    }
}
void return_privilege_and_trace() {
    setup();poke(0x100,2,0x7100);
    expect(run(1).exit==Exit::HostService&&host.service->kind==ServiceKind::EmulOpReturn,"real privileged EMULOP_RETURN reaches explicit host boundary");
    expect(complete_service().exit==Exit::HostReturn&&regs.pc==0x100&&!host.service,"real EMULOP_RETURN preserves PC and exits nested execution");
    setup(0x071f);host.deliver_exceptions=true;poke(0x100,2,0x7100);poke(32,4,0x200);poke(0x200,2,0x4e73);
    expect(run(1).exit==Exit::Budget&&host.frames.size()==1&&host.frames[0].vector==8&&!host.service,"actual old generated EMULOP_RETURN privilege check delivers real exception before service");
    setup();host.private_operations=false;poke(0x100,2,0x7300);
    expect(run(1).exit==Exit::GuestException&&!host.service,"private extension requires explicit platform opt-in");
    setup();poke(0x100,2,0x7302);
    expect(run(1).exit==Exit::GuestException&&!host.service,"resolved old decoder does not claim neighboring illegal private words");
    setup(0xa71f);host.deliver_exceptions=true;poke(0x100,2,0x7300);poke(36,4,0x200);poke(0x200,2,0x4e73);
    expect(run(1).exit==Exit::HostService&&host.frames.empty(),"private suspension defers pending T1 until actual completion");
    expect(complete_service().exit==Exit::Budget&&host.frames.size()==1&&host.frames[0].vector==9&&host.frames[0].stacked_pc==0x102,"private completion traces exactly once after genuine source PC increment");
    expect(run(1).exit==Exit::Budget&&regs.pc==0x102&&host.frames.size()==1,"trace RTE preserves normal deferred tracing contract");
}
}
int main(int argc,char **argv) {
    try{if(argc>2||(argc==2&&std::string_view(argv[1])!="--negative-pc"))throw std::runtime_error("bad mode");wrong_pc=argc==2;install_handlers();suspend_resume();return_privilege_and_trace();std::cout<<"PASS: "<<checks<<" actual ARAnyM private-body service-boundary assertions\n";return 0;}
    catch(const std::exception &e){std::cerr<<"FAIL: "<<e.what()<<"\n";return wrong_pc&&std::string_view(e.what())=="genuine private handler continuation advances PC once without counting a second guest instruction"?1:2;}
}
