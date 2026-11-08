/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "core.hpp"
#include "c-api.h"
#include <iostream>
#include <stdexcept>
using namespace modern040;
namespace {
unsigned checks=0;
void expect(bool pass,const char *message){++checks;if(!pass)throw std::runtime_error(message);}
bool reset_device(void *opaque){++*static_cast<unsigned *>(opaque);return true;}
bool reset_failure(void *){return false;}
void reset_contract() {
    unsigned calls=0;reset();host.reset_hardware=reset_device;host.reset_opaque=&calls;regs.sr=0x071f;MakeFromSR();poke(0x100,2,0x4e70);
    expect(run(1).exit==Exit::GuestException&&calls==0&&regs.pc==0x100,"genuine RESET privilege precedes platform callback");
    reset();regs.regs[15]=regs.isp=0x9000;regs.regs[0]=0x12345678;regs.sr=0x271f;MakeFromSR();poke(0x100,2,0x4e70);
    expect(run(1).exit==Exit::Reset&&regs.pc==0x100,"RESET has no successful no-op without platform hardware contract");
    host.reset_hardware=reset_device;host.reset_opaque=&calls;
    expect(run(1).exit==Exit::Budget&&calls==1&&regs.pc==0x102,"real generated RESET advances PC after exactly one platform reset");
    expect(regs.regs[0]==0x12345678&&regs.regs[15]==0x9000&&architectural_state().sr==0x271f,"Atari hardware RESET preserves CPU registers/SR/banks");
    reset();host.reset_hardware=reset_failure;poke(0x100,2,0x4e70);
    expect(run(1).exit==Exit::Unsupported&&run(1).instructions==0,"failed hardware reset cannot silently continue or retry");
}
void external_exception() {
    const m040_options options={M040_ABI_VERSION,1,1,0};m040_state state;m040_result result;m040_service call;
    expect(m040_reset(&options)==M040_OK&&m040_private_operations(1)==M040_OK&&m040_get_state(&state)==M040_OK,"initialize actual C host-service exception bridge");
    state.regs[15]=state.isp=0x9000;state.usp=0x7000;state.msp=0x8000;state.sr=0x071f;
    state.regs[15]=state.usp;
    expect(m040_set_state(&state)==M040_OK&&m040_poke(0x100,2,0x7301)==M040_OK&&m040_poke(32,4,0x200)==M040_OK&&m040_poke(0x200,2,0x4e73)==M040_OK,"prepare actual user NatFeat word/vector/RTE");
    expect(m040_run(1,&result)==M040_OK&&result.exit==M040_HOST_SERVICE&&m040_get_service(&call)==M040_OK,"actual NatFeat source suspends before host permission decision");
    expect(m040_exception(8,&result)==M040_OK&&result.exit==M040_BUDGET&&m040_get_service(&call)==M040_BAD_ARGUMENT,"host privilege exception cancels pending source completion");
    expect(m040_get_state(&state)==M040_OK&&state.pc==0x200&&state.isp==0x8ff8&&read(AccessKind::Read,0x8ffa,4)==0x100,"external privilege frame saves faulting private PC before source increment");
    expect(m040_run(1,&result)==M040_OK&&m040_get_state(&state)==M040_OK&&state.pc==0x100&&state.regs[15]==0x7000&&state.sr==0x071f,"actual RTE recovers user service-fault PC/SR/A7");
    expect(m040_exception(2,&result)==M040_BAD_ARGUMENT&&m040_exception(3,&result)==M040_BAD_ARGUMENT,"generic exception API rejects access/address faults without precise metadata");
}
}
int main(){try{install_handlers();reset_contract();external_exception();std::cout<<"PASS: "<<checks<<" platform RESET/external-exception contract assertions\n";return 0;}catch(const std::exception &e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 2;}}
