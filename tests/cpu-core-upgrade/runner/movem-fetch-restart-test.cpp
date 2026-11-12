/* SPDX-License-Identifier: GPL-2.0-or-later
 * Fault each instruction-extension transfer while CM owns a saved operand
 * EA. Real format7/RTE must retry fetches before the sole operand transfer.
 * A one-shot synthetic BEFORE_TRANSFER fetch denial is not a paged-MMU claim.
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
 std::array<uae_u8,65536> ram{};uae_u32 denied=0;bool faulted=false;
 struct Event{BusRequest request;BusCompletion completion;};std::vector<Event> events;
 void put(unsigned a,unsigned size,uae_u32 value){for(unsigned n=size;n;--n){ram[a+n-1]=uae_u8(value);value>>=8;}}
 uae_u32 get(unsigned a,unsigned size)const{uae_u32 value=0;for(unsigned n=0;n<size;++n)value=(value<<8)|ram[a+n];return value;}
 static BusCompletion access(void*opaque,const BusRequest&r,uae_u32&v){auto&b=*static_cast<Bus*>(opaque);auto status=BusCompletion::Completed;
  if(!b.faulted&&r.kind==AccessKind::Fetch&&r.address==b.denied){b.faulted=true;status=BusCompletion::FaultBeforeTransfer;}
  else if(r.address>=b.ram.size()||r.size>b.ram.size()-r.address)status=BusCompletion::FaultBeforeTransfer;
  else if(r.kind==AccessKind::Write)b.put(r.address,r.size,v);else v=b.get(r.address,r.size);
  b.events.push_back({r,status});return status;}
 unsigned count(AccessKind kind,uae_u32 address,BusCompletion status)const{unsigned n=0;for(const auto&e:events)if(e.request.kind==kind&&e.request.address==address&&e.completion==status)++n;return n;}
};
struct Layout{uae_u16 header;std::vector<std::pair<unsigned,uae_u32>> extra;};
void run_cases(){
 std::vector<Layout> layouts={{0x1a04,{}}};
 for(unsigned bd:{0x10u,0x20u,0x30u})for(unsigned iis:{0u,1u,2u,3u,5u,6u,7u}){
  Layout l{uae_u16(0x1900|bd|iis),{}};
  if(bd==0x20)l.extra.push_back({2,16});if(bd==0x30)l.extra.push_back({4,0x12345678});
  if((iis&3)==2)l.extra.push_back({2,0xfff8});if((iis&3)==3)l.extra.push_back({4,0x87654321});layouts.push_back(l);
 }
 layouts.push_back({0x01f3,{{4,0x12345678},{4,0x87654321}}});
 for(unsigned opcode:{0x48b0u,0x48f0u,0x4cb0u,0x4cf0u,0x4cbbu,0x4cfbu})for(const auto&l:layouts)for(unsigned failed=0;failed<=l.extra.size();++failed)for(unsigned ccr=0;ccr<32;++ccr){
  Bus b;reset();install_restart_handlers();host.deliver_exceptions=host.fault_recovery=true;host.precise_bus=Bus::access;host.bus_opaque=&b;
  regs.sr=uae_u16(0x2000|ccr);MakeFromSR();regs.pc=0x100;regs.regs[15]=regs.isp=0x9000;
  for(unsigned i=0;i<15;++i)regs.regs[i]=0x11110000+i;regs.regs[0]=0x12345678;regs.regs[1]=0x700;regs.regs[8]=0x800;
  const auto before=architectural_state();b.put(8,4,0x200);b.put(0x200,2,0x4e73);b.put(0x100,2,opcode);b.put(0x102,2,1);b.put(0x104,2,l.header);
  unsigned cursor=0x106;std::vector<std::pair<unsigned,unsigned>> segments={{0x104,2}};
  for(const auto&[size,value]:l.extra){b.put(cursor,size,value);segments.push_back({cursor,size});cursor+=size;}
  b.denied=segments[failed].first;b.put(0x500,4,0x89abcdef);mmu040_movem=1;mmu040_movem_ea=0x500;
  const auto first=run(1);const auto sp=regs.regs[15];const unsigned fetch_size=segments[failed].second;
  expect(first.exit==Exit::Budget&&first.vector==2&&regs.pc==0x200,"CM extension fetch fault reaches real vector2 handler");
  expect(b.get(sp+2,4)==(wrong_pc?0x104u:0x100u)&&b.get(sp+6,2)==0x7008&&b.get(sp+8,4)==0x500,"fetch fault stacks original PC/format7/saved operand EA");
  expect(b.get(sp,2)==(0x2000|ccr)&&b.get(sp+12,2)==(0x1106|(fetch_size==2?0x40:0))&&b.get(sp+14,2)==0&&b.get(sp+20,4)==b.denied,"fetch fault has exact program FC/width/CM metadata and no WB3");
  expect(b.count(AccessKind::Read,0x500,BusCompletion::Completed)==0&&b.count(AccessKind::Write,0x500,BusCompletion::Completed)==0,"no operand transfer before extension fetch completion");
  expect(run(1).exit==Exit::Budget&&regs.pc==0x100&&mmu040_movem==1,"actual guest RTE restores pending saved-EA continuation");
  const auto after_rte=b.events.size();expect(run(1).exit==Exit::Budget&&regs.pc==cursor,"second guest attempt consumes complete stream before completing MOVEM");
  unsigned fetches=0,operands=0;
  for(unsigned index=after_rte;index<b.events.size();++index){const auto&e=b.events[index];if(e.request.kind==AccessKind::Fetch){
    const auto expected=fetches==0?std::pair<unsigned,unsigned>{0x100,2}:fetches==1?std::pair<unsigned,unsigned>{0x102,2}:segments.at(fetches-2);
    expect(e.completion==BusCompletion::Completed&&e.request.address==expected.first&&e.request.size==expected.second,"restarted instruction fetch order/width is exact");++fetches;
   }else {++operands;expect(fetches==segments.size()+2&&e.request.address==(wrong_address?0x800u:0x500u),"saved-EA operand occurs only after all code fetches, without pointer recalculation");}}
  expect(fetches==segments.size()+2+(wrong_count?1:0)&&operands==1,"exact successful extension and operand counts after RTE");
  for(unsigned index=0;index<segments.size();++index){const auto address=segments[index].first;
   expect(b.count(AccessKind::Fetch,address,BusCompletion::Completed)==(index<failed?2u:1u)&&b.count(AccessKind::Fetch,address,BusCompletion::FaultBeforeTransfer)==(index==failed?1u:0u),"only pre-fault extension segments repeat, and only after RTE");}
  const bool load=opcode&0x400;const unsigned size=opcode&0x40?4:2;const auto after=architectural_state();
  expect(after.sr==before.sr&&!mmu040_movem,"fetch recovery preserves all CCR and clears CM after completed instruction");
  for(unsigned i=0;i<16;++i)expect(after.regs[i]==(load&&i==0?(size==2?0xffff89abu:0x89abcdefu):before.regs[i]),"all GPRs survive extension fetch recovery");
  expect(b.get(0x500,4)==(load?0x89abcdefu:size==2?0x5678cdefu:0x12345678u),"exact final RAM bytes after one completed operand");
 }
}
}
int main(int argc,char**argv){try{if(argc>2)throw std::runtime_error("bad arguments");if(argc==2){const std::string_view mode=argv[1];wrong_pc=mode=="--negative-pc";wrong_count=mode=="--negative-extension-count";wrong_address=mode=="--negative-data-address";if(!wrong_pc&&!wrong_count&&!wrong_address)throw std::runtime_error("bad mode");}install_handlers();run_cases();std::cout<<"PASS: "<<checks<<" CM extension-fetch/frame/RTE/ordering/GPR/CCR/RAM assertions\n";}catch(const std::exception&e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}}
