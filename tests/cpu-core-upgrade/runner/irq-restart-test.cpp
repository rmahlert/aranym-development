/* SPDX-License-Identifier: GPL-2.0-or-later
 * MC68040UM sections8.2.6/8.3: finish trace exception processing before IRQ,
 * with no intervening instruction. Read/CM RTE still defers until restart.
 * Raw pending frames complement the actual guest fault/WB3/MFP cohorts.
 */
#include "restart.hpp"
#include <iostream>
#include <stdexcept>
#include <string_view>
using namespace modern040;
namespace {
unsigned checks=0;bool bad_pc=false,bad_order=false,bad_banks=false;
void expect(bool yes,const char*why){++checks;if(!yes)throw std::runtime_error(why);}
unsigned get(unsigned a,unsigned size){unsigned v=0;for(unsigned i=0;i<size;++i)v=(v<<8)|host.ram.at(a+i);return v;}
void cases(unsigned kind){
 for(unsigned mode:{0x8000u,0x9000u,0xa000u,0xb000u})for(unsigned ccr=0;ccr<32;++ccr){
  reset();install_handlers();install_restart_handlers();host.deliver_exceptions=host.fault_recovery=true;
  regs.regs[15]=regs.isp=0x9000;regs.usp=0x7000;regs.msp=0x8000;regs.sr=uae_u16(0x2700|(mode&0x1000));MakeFromSR();
  for(unsigned i=0;i<15;++i)regs.regs[i]=0x11110000+i;
  const unsigned top=mode&0x1000?0x8000:0x9000,sp=top-60,saved=mode|0x500|ccr;
  regs.regs[15]=sp;regs.pc=0x200;
  poke(36,4,0x300);poke(69*4,4,0x400);poke(0x200,2,0x4e73);poke(0x300,2,0x0257);poke(0x302,2,0x3fff);poke(0x304,2,0x4e73);poke(0x400,2,0x4e73);
  poke(sp,2,saved);poke(sp+2,4,kind?0x100:0x102);poke(sp+6,2,0x7008);poke(sp+8,4,kind==2?0x500:0x100);poke(sp+12,2,kind==2?0x1105:kind==1?0x105:0x2005);
  poke(0x100,2,kind==2?0x4cd2:0x2048);poke(0x102,2,1);poke(0x500,4,0x89abcdef);regs.regs[10]=0x600;
  expect(run(1).exit==Exit::Budget,"real format7 RTE executes");
  if(kind){
   expect(regs.pc==0x100&&host.exception_count==0,"read/CM RTE returns to unfinished instruction");
   expect(!inject_interrupt(69,6)&&host.exception_count==0,"read/CM restart defers IRQ before instruction resumes");
   expect(run(1).exit==Exit::Budget&&regs.pc==0x300&&host.exception_count==1,"read/CM instruction completes then Trace9");
  }else expect(regs.pc==0x300&&host.exception_count==1,"CT RTE completes pending Trace9 before any new opcode");
  expect(get(top-12,2)==saved&&get(top-10,4)==(kind==2?0x104u:0x102u)&&get(top-6,2)==0x2024&&get(top-4,4)==0x100,"exact trace frame remains pending for its handler");
  expect(inject_interrupt(69,6),"completed Trace9 permits pending IRQ without executing its first handler instruction");
  const unsigned irqsp=mode&0x1000?0x8ff8:0x8fec;
  expect(host.exception_count==(bad_order?3u:2u)&&regs.pc==0x400,"one IRQ follows one trace event");
  expect(regs.regs[15]==irqsp&&get(irqsp+2,4)==(bad_pc?0x302u:0x300u),"IRQ saves exact unexecuted first trace-handler PC");
  expect(get(irqsp+6,2)==((mode&0x1000?0x1000:0)|69*4),"IRQ uses correct ISP/master throwaway format");
  expect(get(top-12,2)==saved,"IRQ must not execute or mutate trace handler first");
  expect(run(1).exit==Exit::Budget&&regs.pc==0x300&&regs.regs[15]==top-12,"real IRQ RTE restores the original pending trace frame");
  expect(run(2).exit==Exit::Budget&&regs.pc==(kind==2?0x104u:0x102u),"trace handler clears T and real RTE returns once");
  const auto state=architectural_state();expect(state.sr==(saved&0x3fff)&&state.usp==(bad_banks?0x7004u:0x7000u)&&state.isp==0x9000&&state.msp==0x8000,"IRQ/trace return restores SR and all three stack banks");
  for(unsigned i=0;i<15;++i)expect(state.regs[i]==(kind==2&&i==0?0x89abcdefu:i==10?0x600u:0x11110000+i),"all GPR values survive trace-before-IRQ processing");
 }
}
}
int main(int argc,char**argv){try{unsigned kind=0;for(int i=1;i<argc;++i){const std::string_view arg=argv[i];if(arg=="--only-read")kind=1;else if(arg=="--only-cm")kind=2;else if(arg=="--negative-pc")bad_pc=true;else if(arg=="--negative-order")bad_order=true;else if(arg=="--negative-banks")bad_banks=true;else throw std::runtime_error("bad argument");}cases(kind);std::cout<<"PASS: "<<checks<<" post-trace IRQ/frame/order/restart/bank assertions\n";}catch(const std::exception&e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}}
