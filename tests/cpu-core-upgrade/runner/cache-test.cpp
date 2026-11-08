/* SPDX-License-Identifier: GPL-2.0-or-later
 * Empty-cache interpreter contract, not a hardware cache/timing model.
 */
#include "core.hpp"
#include <iostream>
#include <stdexcept>
#include <string_view>
using namespace modern040;
namespace {
unsigned checks=0;bool corrupt=false;
void expect(bool pass,const char *message){++checks;if(!pass)throw std::runtime_error(message);}
void scope_and_privilege() {
    for(bool push:{false,true})for(unsigned scope=1;scope<=3;++scope)for(unsigned cache=0;cache<4;++cache)
    for(unsigned areg=0;areg<8;++areg)for(unsigned ccr=0;ccr<32;++ccr) {
        reset();regs.sr=0x2700|ccr;MakeFromSR();for(unsigned i=0;i<16;++i)regs.regs[i]=0xf1000100+i*13;
        const auto opcode=uae_u16(0xf400|(push?0x20:0)|(scope<<3)|(cache<<6)|areg);
        poke(0x100,2,opcode);const auto before=host.ram;const auto initial=architectural_state();
        expect(legal[opcode]&&handlers[opcode],"cache instruction must use actual admitted generated decoder");
        expect(run(1).exit==Exit::Budget&&regs.pc==0x102,"real generated cache maintenance completes with exact PC");
        expect(host.cache_operations.size()==1,"cache source wrapper calls empty-cache model exactly once");
        const auto &event=host.cache_operations[0];
        expect(event.cache==int(corrupt?(cache^1):cache)&&event.scope==int(scope)&&event.address==initial.regs[8+areg]&&event.push==push&&event.invalidate,"real source wrapper retains cache/scope/address/push/invalidate parameters");
        expect(host.ram==before&&host.accesses.size()==1&&host.accesses[0].kind==AccessKind::Fetch,"empty cache maintenance does not read/write target RAM/MMIO even for unmapped addresses");
        expect(host.cache_controls==(cache&2?2u:1u),"instruction selector invokes real generated instruction-flush seam");
        const auto after=architectural_state();expect(after.sr==initial.sr,"cache maintenance preserves all32CCR encodings");
        for(unsigned i=0;i<16;++i)expect(after.regs[i]==initial.regs[i],"cache maintenance preserves every D/A register");
    }
    for(uae_u16 opcode:{0xf448,0xf490,0xf4d8,0xf468,0xf4b0,0xf4f8}) {
        reset();regs.sr=0x071f;MakeFromSR();poke(0x100,2,opcode);const auto r=run(1);
        expect(r.exit==Exit::GuestException&&r.vector==8&&regs.pc==0x100&&host.cache_operations.empty(),"real CINV/CPUSH user privilege failure precedes cache callback");
    }
}
void self_modifying_guest() {
    reset();regs.regs[15]=regs.isp=0x9000;regs.cacr=0x80008000;
    poke(0x100,2,0x7000);poke(0x102,2,0x4e72);poke(0x104,2,0x2700);
    expect(run(1).exit==Exit::Budget&&regs.regs[0]==0,"initial instruction fetch executes original target");
    // Actual guest MOVE.W #$7009,$100; CINVA bc; JMP $100.
    const uae_u16 code[]={0x33fc,0x7009,0x0000,0x0100,0xf4d8,0x4ef9,0x0000,0x0100};
    for(unsigned i=0;i<std::size(code);++i)poke(0x200+2*i,2,code[i]);m68k_setpc(0x200);
    const auto r=run(20);
    expect(r.exit==Exit::Stopped&&r.instructions==5&&regs.regs[0]==9&&host.cache_operations.size()==1,"actual guest write/cache-maintenance/jump observes new instruction in direct-fetch profile");
}
}
int main(int argc,char **argv){try{if(argc>2||(argc==2&&std::string_view(argv[1])!="--negative-selector"))throw std::runtime_error("bad mode");corrupt=argc==2;install_handlers();scope_and_privilege();self_modifying_guest();std::cout<<"PASS: "<<checks<<" real generated empty-cache maintenance assertions\n";return 0;}catch(const std::exception &e){std::cerr<<"FAIL: "<<e.what()<<'\n';return corrupt&&std::string_view(e.what())=="real source wrapper retains cache/scope/address/push/invalidate parameters"?1:2;}}
