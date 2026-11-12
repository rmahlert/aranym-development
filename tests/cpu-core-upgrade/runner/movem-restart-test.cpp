/* SPDX-License-Identifier: GPL-2.0-or-later
 * Independent CM-resume stream oracle: six genuine MOVEM bodies, literal
 * extension layouts and saved operand EA. All operand/index inputs point
 * elsewhere, so repeating EA calculation cannot silently pass.
 * Guest RTE/frame integration is exercised separately by MOVEMF.PRG.
 */
#include "restart.hpp"
#include <iostream>
#include <stdexcept>
#include <string_view>
using namespace modern040;
namespace {
unsigned checks=0;bool wrong_pc=false,wrong_count=false,wrong_address=false;
void expect(bool yes,const char*message){++checks;if(!yes)throw std::runtime_error(message);}
struct Bus {
 std::array<uae_u8,65536> ram{};
 struct Event{BusRequest request;uae_u32 value;};std::vector<Event> events;
 void put(unsigned a,unsigned size,uae_u32 value){for(unsigned n=size;n;--n){ram[a+n-1]=uae_u8(value);value>>=8;}}
 uae_u32 get(unsigned a,unsigned size)const{uae_u32 value=0;for(unsigned n=0;n<size;++n)value=(value<<8)|ram[a+n];return value;}
 static BusCompletion access(void*opaque,const BusRequest&r,uae_u32&v){auto&b=*static_cast<Bus*>(opaque);if(r.address>=b.ram.size()||r.size>b.ram.size()-r.address)return BusCompletion::FaultBeforeTransfer;if(r.kind==AccessKind::Write)b.put(r.address,r.size,v);else v=b.get(r.address,r.size);b.events.push_back({r,v});return BusCompletion::Completed;}
};
struct Layout{uae_u16 header;std::vector<std::pair<unsigned,uae_u32>> extra;};
void run_cases(){
 std::vector<Layout> layouts={{0x1a04,{}}};
 for(unsigned bd:{0x10u,0x20u,0x30u})for(unsigned iis:{0u,1u,2u,3u,5u,6u,7u}){
  Layout l{uae_u16(0x1900|bd|iis),{}};
  if(bd==0x20)l.extra.push_back({2,16});if(bd==0x30)l.extra.push_back({4,0x12345678});
  if((iis&3)==2)l.extra.push_back({2,0xfff8});if((iis&3)==3)l.extra.push_back({4,0x87654321});layouts.push_back(l);
 }
 // Suppression bits must not change the number of extension fetches.
 layouts.push_back({0x01f3,{{4,0x12345678},{4,0x87654321}}});
 for(unsigned opcode:{0x48b0u,0x48f0u,0x4cb0u,0x4cf0u,0x4cbbu,0x4cfbu})for(const auto&l:layouts)for(unsigned ccr=0;ccr<32;++ccr){
  Bus b;reset();install_restart_handlers();host.deliver_exceptions=host.fault_recovery=true;host.precise_bus=Bus::access;host.bus_opaque=&b;
  regs.sr=uae_u16(0x2000|ccr);MakeFromSR();regs.pc=0x100;regs.regs[15]=regs.isp=0x9000;
  for(unsigned i=0;i<15;++i)regs.regs[i]=0x11110000+i;regs.regs[0]=0x12345678;regs.regs[1]=0x700;regs.regs[8]=0x800;
  const auto before=architectural_state();b.put(0x100,2,opcode);b.put(0x102,2,1);b.put(0x104,2,l.header);
  unsigned cursor=0x106;std::vector<std::pair<unsigned,unsigned>> fetches={{0x100,2},{0x102,2},{0x104,2}};
  for(const auto&[size,value]:l.extra){b.put(cursor,size,value);fetches.push_back({cursor,size});cursor+=size;}
  b.put(0x500,4,0x89abcdef);mmu040_movem=1;mmu040_movem_ea=0x500;
  const auto result=run(1);const bool load=opcode&0x400;const unsigned size=opcode&0x40?4:2;
  expect(result.exit==Exit::Budget&&!result.vector&&regs.pc==cursor+(wrong_pc?2:0),"resumed MOVEM consumes extension stream and reaches next opcode");
  unsigned n=0,operands=0;
  for(const auto&e:b.events){if(e.request.kind==AccessKind::Fetch){expect(n<fetches.size()&&e.request.address==fetches[n].first&&e.request.size==fetches[n].second,"exact instruction extension fetch address/width");++n;}
   else {++operands;expect(e.request.address==(wrong_address?0x800u:0x500u)&&e.request.size==size&&e.request.kind==(load?AccessKind::Read:AccessKind::Write),"saved EA is the sole operand address, never a recalculated pointer");}}
  expect(n==fetches.size()+(wrong_count?1:0)&&operands==1,"exact extension and operand transfer counts");
  const auto after=architectural_state();
  expect(after.sr==before.sr&&!mmu040_movem,"CM completion preserves CCR and clears continuation");
  for(unsigned i=0;i<16;++i)expect(after.regs[i]==(load&&i==0?(size==2?0xffff89abu:0x89abcdefu):before.regs[i]),"all GPRs follow MOVEM semantics");
  expect(b.get(0x500,4)==(load?0x89abcdefu:size==2?0x5678cdefu:0x12345678u),"exact word/long store bytes and unchanged load RAM");
 }
}
}
int main(int argc,char**argv){try{if(argc>2)throw std::runtime_error("bad arguments");if(argc==2){const std::string_view mode=argv[1];wrong_pc=mode=="--negative-pc";wrong_count=mode=="--negative-extension-count";wrong_address=mode=="--negative-data-address";if(!wrong_pc&&!wrong_count&&!wrong_address)throw std::runtime_error("bad mode");}install_handlers();run_cases();std::cout<<"PASS: "<<checks<<" CM MOVEM extension/savedEA/CCR/GPR/RAM assertions\n";}catch(const std::exception&e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}}
