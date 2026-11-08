/* SPDX-License-Identifier: GPL-2.0-or-later
 * Tests only the explicit external-engine boundary; no simulated FP arithmetic.
 */
#include "core.hpp"
#include "c-api.h"
#include <iostream>
#include <stdexcept>
#include <string>
using namespace modern040;
namespace {
unsigned checks=0;bool negative=false;
void expect(bool ok,const char *why){++checks;if(!ok)throw std::runtime_error(why);}
void boundary() {
    const m040_options options={M040_ABI_VERSION,1,0,1};
    const unsigned opcodes[]={0xf200,0xf210,0xf227,0xf23c,0xf240,0xf248,0xf27c,0xf280,0xf2c0,0xf310,0xf327,0xf350,0xf35f};
    for(auto opcode:opcodes)for(unsigned ccr=0;ccr<32;++ccr) {
        m040_result result;m040_service call;m040_state state;
        expect(m040_reset(&options)==0&&m040_legacy_fpu_operations(1)==0,"initialize explicit original FPU boundary");
        expect(m040_get_state(&state)==0,"export initial boundary state");
        state.sr=0x2700|ccr;state.regs[0]=0x12345678;
        expect(m040_set_state(&state)==0&&m040_poke(0x100,2,opcode)==0&&m040_poke(0x102,2,0x6000)==0,"prepare genuine FPU opcode and unread extension");
        expect(m040_run(8,&result)==0,"dispatch API succeeds");
        if(result.exit!=M040_HOST_SERVICE)std::cerr<<"opcode="<<std::hex<<opcode<<" exit="<<result.exit<<" detail="<<result.detail<<"\n";
        expect(result.exit==M040_HOST_SERVICE&&result.instructions==1,"one real decoder fetch yields before external execution");
        expect(m040_get_service(&call)==0&&call.kind==M040_LEGACY_FPU&&call.opcode==opcode&&call.pc==0x100,"numeric BE opcode and original PC survive boundary");
        expect(host.accesses.size()==1&&host.accesses[0].kind==AccessKind::Fetch&&host.accesses[0].address==0x100,"boundary performs no extension/operand read");
        expect(m040_legacy_fpu_operations(0)==M040_BUSY,"cannot alter engine while service pending");
        expect(m040_run(1,&result)==0&&result.exit==M040_HOST_SERVICE&&result.instructions==0&&host.accesses.size()==1,"pending boundary cannot execute/refetch implicitly");
        expect(m040_get_state(&state)==0&&state.pc==0x100&&state.instruction_pc==0x100&&state.sr==(0x2700|ccr)&&state.regs[0]==0x12345678,"boundary preserves CCR/registers before real helper");
        // Represents one already-executed external generated body, not an
        // implementation of this instruction. Actual engine checked in guest.
        state.pc=0x180;state.regs[0]=0x87654321;
        expect(m040_set_state(&state)==0&&m040_complete_service(&result)==0,"external engine can return advanced/branched PC");
        expect(result.exit==M040_BUDGET&&result.instructions==0&&result.current_pc==(negative?0x182:0x180)&&result.instruction_pc==0x100,"completion retains original instruction PC and external target exactly once");
        expect(m040_get_state(&state)==0&&state.regs[0]==0x87654321&&state.sr==(0x2700|ccr)&&host.accesses.size()==1,"completion performs no replay/fetch/flag transformation");
        expect(m040_get_service(&call)==M040_BAD_ARGUMENT,"completed external service is cleared");
    }
    m040_result result;m040_state state;
    expect(m040_reset(&options)==0&&m040_poke(0x100,2,0xf200)==0&&m040_run(1,&result)==0&&result.exit==M040_UNSUPPORTED,"reset defaults external engine off");
    expect(m040_reset(&options)==0&&m040_legacy_fpu_operations(2)==M040_BAD_ARGUMENT,"invalid engine selector rejected");
    expect(m040_legacy_fpu_operations(1)==0&&m040_get_state(&state)==0,"prepare traced engine boundary");
    state.sr=0xa700;
    expect(m040_set_state(&state)==0&&m040_poke(0x100,2,0xf200)==0&&m040_run(1,&result)==0&&result.exit==M040_UNSUPPORTED&&host.accesses.size()==1,"unintegrated traced external engine fails before extension reads");
    expect(m040_reset(&options)==0&&m040_legacy_fpu_operations(1)==0&&m040_poke(0x100,2,0x7003)==0&&m040_run(1,&result)==0&&result.exit==M040_BUDGET&&regs.regs[0]==3,"integer opcode still executes actual modern body");
}
}
int main(int argc,char **argv){try{negative=argc==2&&std::string(argv[1])=="--negative-pc";install_handlers();boundary();std::cout<<"PASS: "<<checks<<" explicit original FPU boundary assertions (not FP arithmetic)\n";return 0;}catch(const std::exception &e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}}
