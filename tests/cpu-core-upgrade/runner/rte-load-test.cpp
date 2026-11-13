/* SPDX-License-Identifier: GPL-2.0-or-later
 * MC68040UM 8.2.1: internal RTE state-load errors halt; reset alone restarts.
 * Owned synchronous RAM, not a paged-MMU or arbitrary-device atomicity test.
 */
#include "restart.hpp"
#include "c-api.h"
#include <iostream>
#include <stdexcept>
#include <string_view>
using namespace modern040;
namespace {
unsigned checks=0;
bool negative_writes=false,negative_state=false,negative_irq=false;
void need(bool ok,const char* why){++checks;if(!ok)throw std::runtime_error(why);}
struct Bus {
 std::array<uae_u8,65536> ram{};
 std::vector<BusRequest> requests;
 unsigned denied=0xffffffff,failed=0,writes=0,requests_at_fault=0;
 bool fail_next_write=false;
 BusCompletion completion=BusCompletion::FaultBeforeTransfer;
 State at_fault{};
 void put(unsigned a,unsigned n,unsigned v){for(unsigned i=n;i;--i){ram.at(a+i-1)=uae_u8(v);v>>=8;}}
 unsigned get(unsigned a,unsigned n)const{unsigned v=0;for(unsigned i=0;i<n;++i)v=(v<<8)|ram.at(a+i);return v;}
 static BusCompletion access(void* opaque,const BusRequest& q,uae_u32& v){
  auto& b=*static_cast<Bus*>(opaque);b.requests.push_back(q);
  if((!b.failed&&q.address==b.denied)||(b.fail_next_write&&q.kind==AccessKind::Write)){
   if(q.kind==AccessKind::Write)b.fail_next_write=false;
   ++b.failed;b.at_fault=regs;b.requests_at_fault=b.requests.size();return b.completion;
  }
  if(q.kind==AccessKind::Write){++b.writes;b.put(q.address,q.size,v);}else v=b.get(q.address,q.size);
  return BusCompletion::Completed;
 }
 static int api(void* opaque,const m040_bus_request* q,uint32_t* v){
  return int(access(opaque,{AccessKind(q->kind),q->address,q->size,q->function_code,
   q->has_alternate_function_code?std::optional<unsigned>(q->alternate_function_code):std::nullopt,q->transfer_type},*v));
 }
};
void setup(Bus& b,bool master=false,unsigned ccr=0,unsigned mask=0){
 reset();host.deliver_exceptions=host.fault_recovery=true;host.precise_bus=Bus::access;host.bus_opaque=&b;
 regs.usp=0x7000;regs.isp=0x9000;regs.msp=0x8000;
 regs.s=true;regs.m=master;regs.regs[15]=master?regs.msp:regs.isp;regs.sr=uae_u16(0x2000|(master?0x1000:0)|(mask<<8)|ccr);MakeFromSR();
 for(unsigned i=0;i<15;++i)regs.regs[i]=0x12340000+0x1111*i;
 b.put(8,4,0x500);b.put(12,4,0x520);b.put(36,4,0x540);
 for(unsigned level=1;level<=7;++level)b.put((24+level)*4,4,0x600);
 b.put(0x200,2,0x4e73);b.put(0x600,2,0x4e73);b.put(0x1000,2,0x4e71);regs.pc=0x200;
}
unsigned frame(Bus& b,bool master,bool throwaway,unsigned saved_sr,unsigned ssw){
 unsigned sp=master?0x8fc4:0x7fc4;
 if(!throwaway)sp=master?0x7fc4:0x8fc4;
 if(throwaway){
  // Format1 consumes the current bank then switches S/M to the other bank.
  const unsigned first=master?0x7ff8:0x8ff8;
  regs.regs[15]=first;
  if(master){regs.msp=first;regs.isp=sp;}else{regs.isp=first;regs.msp=sp;}
  b.put(first,2,master?0x2000:0x3000);b.put(first+2,4,0x1000);b.put(first+6,2,0x1000);
 }else{regs.regs[15]=sp;if(master)regs.msp=sp;else regs.isp=sp;}
 b.put(sp,2,saved_sr);b.put(sp+2,4,0x1000);b.put(sp+6,2,0x7008);
 b.put(sp+8,4,0x180);b.put(sp+12,2,ssw);return sp;
}
void unchanged_halt(Bus& b){
 const auto state=regs;const auto n=b.requests.size();
 for(auto budget:{0u,1u,17u}){const auto r=run(budget);need(r.exit==Exit::DoubleFault&&r.instructions==0,"halt must prevent instruction resume");}
 for(unsigned level=1;level<=7;++level){
  const bool accepted=inject_interrupt(24+int(level),level);
  need(negative_irq?accepted:!accepted,"halt rejects IRQ/NMI until external reset");
  need(regs==state&&b.requests.size()==n,"halted IRQ must not alter registers or memory");
 }
 bool halted=false;try{Exception(4);}catch(const Stop& event){halted=event.result.exit==Exit::DoubleFault;}
 need(halted&&regs==state&&b.requests.size()==n,"explicit exception entry cannot alter a halted CPU");
}
void loads(){
 for(auto completion:{BusCompletion::FaultBeforeTransfer,BusCompletion::UnknownFault})
 for(unsigned which=0;which<3;++which)for(bool master:{false,true})for(bool throwaway:{false,true})
 for(unsigned saved:{0x0000u,0x1000u,0x2000u,0x3000u,0x4000u,0x8000u,0x6000u,0xa000u})for(unsigned ccr=0;ccr<32;++ccr){
  Bus b;setup(b,master,ccr);const auto sp=frame(b,master,throwaway,saved|ccr,which==1?0x2005:which==2?0x1105:0x0105);
  b.denied=sp+(which?8:12);b.completion=completion;
  const auto result=run(1);
  need(b.failed==1&&b.requests.at(b.requests_at_fault-1).address==b.denied&&b.requests.at(b.requests_at_fault-1).size==(which?4u:2u),"exact internal SSW/EA transfer fails once");
  need(result.exit==Exit::DoubleFault&&host.halted&&result.vector==2&&result.address==b.denied&&result.size==(which?4u:2u)&&result.instruction_pc==0x200,"RTE internal state-load fault halts at exact transfer");
  need(b.writes==(negative_writes?1u:0u)&&host.exception_count==0&&b.requests.size()==b.requests_at_fault,"no recursive frame writes or subsequent bus access");
  auto expected=b.at_fault;if(negative_state)expected.pc+=2;
  need(regs==expected,"halt preserves register state at failed internal load");
  unchanged_halt(b);
 }
}
void controls(){
 for(bool master:{false,true})for(unsigned offset:{0u,2u,6u}){
  Bus b;setup(b,master);const auto sp=frame(b,master,false,0x2700,0x105);b.denied=sp+offset;
  const auto r=run(1);need(r.exit==Exit::Budget&&r.vector==2&&!host.halted&&host.exception_count==1&&b.writes==18,"initial header-read boundary retains ordinary precise fault delivery");
 }
 for(bool master:{false,true})for(bool throwaway:{false,true})for(bool data:{false,true}){
  Bus b;setup(b,master);frame(b,master,throwaway,0x2700,0x105);
  auto r=run(1);need(r.exit==Exit::Budget&&!host.halted&&regs.pc==0x1000,"successful genuine RTE control");
  b.put(0x1000,2,0x4a10);regs.regs[8]=0x1800;b.denied=data?0x1800:0x1000;
  r=run(1);need(r.exit==Exit::Budget&&r.vector==2&&!host.halted&&b.failed==1,"RTE load phase cannot leak into successor fetch/data fault");
 }
 for(unsigned ssw:{0x8005u,0x4005u,0x3005u}){
  Bus b;setup(b);frame(b,false,false,0x2700,ssw);const auto r=run(1);
  need(r.exit==Exit::Unsupported&&host.capability_latched&&!host.halted&&b.writes==0,"FPU/combined-continuation capability stop is distinct from double fault");
 }
 {
  Bus b;setup(b);const auto sp=frame(b,false,false,0x2700,0x2005);b.put(sp+2,4,0x1001);
  const auto r=run(1);need(r.exit==Exit::Budget&&host.last_frame&&host.last_frame->vector==3&&!host.halted,"odd RTE PC keeps synchronous address-error priority over pending CT");
 }
}
void halted_irqs(){
 for(bool master:{false,true})for(unsigned mask:{0u,3u,7u})for(unsigned ccr=0;ccr<32;++ccr){
  Bus b;setup(b,master,ccr,mask);regs.pc=0x100;regs.regs[8]=0x1800;b.put(0x100,2,0x4a10);
  b.denied=0x1800;b.fail_next_write=true;
  const auto r=run(1);
  if(r.exit!=Exit::DoubleFault||!host.halted||b.failed!=2)std::cerr<<"setup exit="<<exit_name(r.exit)<<" halted="<<host.halted<<" failed="<<b.failed<<" detail="<<r.detail<<'\n';
  need(r.exit==Exit::DoubleFault&&host.halted&&b.failed==2,"access-error frame construction double fault setup");
  unchanged_halt(b);
  Bus fresh;setup(fresh,master,ccr,mask);
  need(!host.halted&&mmu040_movem==0&&!host.rte_trace_pending,"external reset clears halt and private restart state");
  need(inject_interrupt(31,7),"external reset allows NMI again");
  const auto returned=run(1);need(returned.exit==Exit::Budget&&regs.pc==0x200&&!host.halted,"IRQ genuine RTE works after external reset");
 }
}
void public_api(){
 Bus b;const m040_options options{M040_ABI_VERSION,1,0,0};
 need(m040_reset(&options)==M040_OK&&m040_fault_recovery(1)==M040_OK&&m040_set_precise_bus(Bus::api,&b)==M040_OK,"public C ABI setup");
 m040_state state{};need(m040_get_state(&state)==M040_OK,"C ABI get state");
 state.pc=state.instruction_pc=0x200;state.sr=0x2700;state.regs[15]=state.isp=0x8fc4;state.usp=0x7000;state.msp=0x8000;
 need(m040_set_state(&state)==M040_OK,"C ABI set initial context");
 b.put(0x200,2,0x4e73);b.put(0x8fc4,2,0x2700);b.put(0x8fc6,4,0x1000);b.put(0x8fca,2,0x7008);b.denied=0x8fd0;
 m040_result result{};need(m040_run(1,&result)==M040_OK&&result.exit==M040_DOUBLE_FAULT,"C ABI exposes internal-load halt");
 m040_state before{},after{};need(m040_get_state(&before)==M040_OK,"C ABI halted get state");
 const auto count=b.requests.size();
 for(unsigned level=1;level<=7;++level){int accepted=99;need(m040_interrupt(24+level,level,&accepted,&result)==M040_OK&&accepted==0,"C ABI halted IRQ/NMI rejected");}
 need(m040_get_state(&after)==M040_OK&&before.pc==after.pc&&before.sr==after.sr&&before.regs[15]==after.regs[15]&&b.requests.size()==count,"C ABI halted context and bus stable");
 need(m040_run(1,&result)==M040_OK&&result.exit==M040_DOUBLE_FAULT&&result.instructions==0,"C ABI halt prevents resume");
 need(m040_exception(4,&result)==M040_OK&&result.exit==M040_DOUBLE_FAULT&&b.requests.size()==count,"C ABI explicit exception preserves halt without bus traffic");
 need(m040_set_state(&before)==M040_OK&&m040_run(1,&result)==M040_OK&&result.exit==M040_DOUBLE_FAULT,"state import cannot unlock halt");
 need(m040_reset(&options)==M040_OK&&m040_set_precise_bus(Bus::api,&b)==M040_OK,"C ABI external reset");
 state={};need(m040_get_state(&state)==M040_OK,"C ABI reset context");state.regs[15]=state.isp=0x9000;state.sr=0x2700;
 need(m040_set_state(&state)==M040_OK,"C ABI reset stack");b.put(31*4,4,0x600);int accepted=0;
 need(m040_interrupt(31,7,&accepted,&result)==M040_OK&&accepted==1,"C ABI external reset permits NMI");
}
}
int main(int argc,char** argv){try{
 bool only_controls=false,only_halt=false;
 for(int i=1;i<argc;++i){std::string_view arg=argv[i];if(arg=="--only-controls")only_controls=true;else if(arg=="--only-halt")only_halt=true;
  else if(arg=="--negative-writes")negative_writes=true;else if(arg=="--negative-state")negative_state=true;else if(arg=="--negative-irq")negative_irq=true;else throw std::runtime_error("unknown option");}
 install_handlers();install_restart_handlers();
 if(!only_controls&&!only_halt)loads();if(!only_halt)controls();if(!only_controls)halted_irqs();if(!only_controls&&!only_halt)public_api();
 std::cout<<"PASS: "<<checks<<" RTE internal-load/reset-only halt assertions\n";
}catch(const std::exception& e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}}
