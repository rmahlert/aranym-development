/* SPDX-License-Identifier: GPL-2.0-or-later
 * Additive per-request contract, not architectural frame integration.
 */
#include "c-api.h"
#include "restart.hpp"
#include "producer-boundary-cases.hpp"
#include <iostream>
#include <stdexcept>
#include <string_view>
using namespace modern040;
namespace {
unsigned checks=0,rows=0;bool bad_producer=false,bad_fresh=false,bad_data=false,bad_terminal=false,dump=false;
void need(bool ok,const char*why){++checks;if(!ok)throw std::runtime_error(why);}
const m040_options options{M040_ABI_VERSION,1,0,0};
struct Bus {
 std::array<unsigned char,65536> ram{};
 unsigned calls=0,writes=0,fail=1,mode=0,read_address=~0u,malformed=0;
 unsigned read_kind=M040_READ;
 bool flip=false,noise=false,reentry=false,marker=false;
 unsigned address=0,size=0,fc=0,value=0,pc=0;State at_fault{};
 void put(unsigned a,unsigned n,unsigned v){for(unsigned i=n;i;--i){ram.at(a+i-1)=v;v>>=8;}}
 unsigned get(unsigned a,unsigned n)const{unsigned v=0;for(unsigned i=0;i<n;++i)v=(v<<8)|ram.at(a+i);return v;}
 static int access(void*o,const m040_bus_request*q,uint32_t*v,m040_bus_detail*d){
  auto&b=*static_cast<Bus*>(o);++b.calls;
  need(q->abi_version==M040_ABI_VERSION&&d->abi_version==M040_BUS_DETAIL_VERSION&&d->completion==0&&d->origin==0&&d->whole_scalar_no_commit==0&&d->has_cpu_physical_address==0&&d->cpu_physical_address==(bad_fresh?1u:0u),"each callback starts with fresh versioned request-local evidence");
  if(!b.reentry){
   m040_state s{};m040_result r{};
   need(m040_get_state(&s)==M040_BUSY&&m040_set_state(&s)==M040_BUSY&&m040_run(1,&r)==M040_BUSY&&m040_reset(&options)==M040_BUSY&&m040_set_detailed_bus(nullptr,nullptr)==M040_BUSY&&m040_set_precise_bus(nullptr,nullptr)==M040_BUSY&&m040_set_bus(nullptr,nullptr)==M040_BUSY,"all active callback reentry is rejected");b.reentry=true;
  }
  const bool denied=(q->kind==M040_WRITE&&++b.writes==b.fail)||(q->kind==b.read_kind&&q->address==b.read_address);
  if(!denied){
   if(q->kind==M040_WRITE)b.put(q->address,q->size,*v);else *v=b.get(q->address,q->size);
   d->completion=M040_BUS_COMPLETED;
   if(b.noise){d->origin=M040_ORIGIN_EXTERNAL_BUS;d->whole_scalar_no_commit=1;d->has_cpu_physical_address=1;d->cpu_physical_address=0xffffffff;}
   return 1;
  }
  b.marker=!mmu_restart;b.address=q->address;b.size=q->size;b.fc=q->function_code;b.value=*v;b.pc=regs.pc;b.at_fault=regs;
  if(b.flip)mmu_restart=!mmu_restart;
  if(q->kind==M040_WRITE)*v^=0xffffffff; // Must not replace the CPU's store data.
  d->completion=M040_BUS_FAULT_BEFORE_TRANSFER;d->origin=M040_ORIGIN_EXTERNAL_BUS;
  d->whole_scalar_no_commit=q->kind==M040_WRITE;d->has_cpu_physical_address=1;d->cpu_physical_address=0xf0000000u|q->address;
  if(b.mode==1){d->origin=M040_ORIGIN_ATC_TRANSLATION;d->has_cpu_physical_address=0;}
  if(b.mode==2)d->origin=M040_ORIGIN_UNSPECIFIED;
  if(b.mode==3){d->has_cpu_physical_address=0;d->cpu_physical_address=0xdeadbeef;}
  if(b.mode==4)d->whole_scalar_no_commit=0;
  if(b.mode==5){d->completion=M040_BUS_UNKNOWN_FAULT;d->whole_scalar_no_commit=0;}
  if(b.mode==6){d->abi_version=99;d->origin=99;return 0;}
  if(b.mode==7){d->origin=0;d->whole_scalar_no_commit=0;d->has_cpu_physical_address=0;d->cpu_physical_address=0;}
  if(b.mode==8)d->cpu_physical_address=0;
  switch(b.malformed){
   case 1:d->abi_version=99;break;case 2:d->completion=9;break;case 3:d->origin=2;break;
   case 4:d->whole_scalar_no_commit=2;break;case 5:d->has_cpu_physical_address=2;break;
   case 6:return -7;case 7:return 2;case 8:d->completion=0;d->whole_scalar_no_commit=1;break;
  }
  return 1;
 }
 static int legacy(void*o,const m040_bus_request*q,uint32_t*v){
  auto&b=*static_cast<Bus*>(o);++b.calls;
  if(q->kind==M040_WRITE&&++b.writes==b.fail)return M040_BUS_FAULT_BEFORE_TRANSFER;
  if(q->kind==M040_WRITE)b.put(q->address,q->size,*v);else *v=b.get(q->address,q->size);
  return M040_BUS_COMPLETED;
 }
};
void attach(Bus&b,const producer_cases::Case&f,bool super,unsigned dfc,unsigned trace,unsigned ccr){
 need(m040_reset(&options)==M040_OK&&m040_fault_recovery(1)==M040_OK,"reset for real producer boundary");m040_state s{};
 need(m040_get_state(&s)==M040_OK,"read canonical C state");s.sr=(super?0x2000:0)|0x500|trace|ccr;s.isp=0x9000;s.usp=0x7000;s.msp=0x8000;s.regs[15]=super?s.isp:s.usp;s.regs[0]=0xdeadbeef;s.regs[1]=0x12345678;s.regs[8]=f.a0;s.dfc=dfc;
 need(m040_set_state(&s)==M040_OK&&m040_set_detailed_bus(Bus::access,&b)==M040_OK,"attach opt-in detailed callback");
 need(m040_poke(0x100,2,0)==M040_BAD_ARGUMENT,"flat poke cannot bypass detailed bus ownership");
 for(unsigned i=0;i<f.count;++i)b.put(0x100+2*i,2,f.words[i]);for(unsigned i=0;i<64;++i)b.ram[0x5000+i]=(i*19+0x12)&255;
}
void matrix(){
 for(const auto&f:producer_cases::cases)for(bool super:{false,true}){
  if(f.privileged&&!super)continue;
  for(unsigned dfc:{1u,5u}){if(!f.privileged&&dfc!=5)continue;
   for(unsigned trace:{0u,0x4000u,0x8000u})for(unsigned ccr=0;ccr<32;++ccr)for(unsigned n=1;n<=f.writes;++n)
   for(unsigned mode=0;mode<(dump?1u:9u);++mode)for(unsigned change=0;change<(dump?1u:2u);++change){
    Bus b;b.fail=n;b.mode=mode;b.flip=change;b.noise=true;attach(b,f,super,dfc,trace,ccr);m040_result r{};
    const int status=m040_run(1,&r);
    if(bad_fresh)need(status!=M040_HOST_ERROR,"fresh versioned reply negative detected at C boundary");
    need(status==M040_OK&&r.exit==M040_MEMORY_FAULT&&r.vector==2&&r.address==b.address&&r.size==b.size,"detailed failure stays terminal pending architectural integration");
    need(b.writes==n&&host.exception_count==0&&host.access_fault_latched&&host.bus_fault,"no recovery frame or additional write is generated");
    const auto&fault=*host.bus_fault;
    need(fault.detailed_profile&&fault.opcode==f.words[0]&&fault.in_instruction&&fault.request.address==b.address&&fault.request.size==b.size&&fault.request.function_code==b.fc,"fault snapshot binds evidence to the exact failed request");
    need(b.marker==f.completed&&fault.completed_producer==(bad_producer?!f.completed:f.completed),"completion comes from the real producer before callback");
    need(fault.data==(bad_data?b.value^1:b.value),"store data comes from CPU input, not callback output");
    if(mode==6)need(!fault.evidence&&fault.completion==BusCompletion::UnknownFault,"missing reply cannot inherit previous success metadata");
    else{
     need(bool(fault.evidence),"valid reply evidence is copied into the failed-request snapshot");const auto&e=*fault.evidence;
     const auto origin=mode==1?FaultOrigin::AtcTranslation:(mode==2||mode==7)?FaultOrigin::Unspecified:FaultOrigin::ExternalBus;
     need(e.origin==origin&&e.whole_scalar_no_commit==!(mode==4||mode==5||mode==7),"origin, whole-request evidence and completion are independent");
     need((mode==1||mode==3||mode==7)?!e.cpu_physical_address:e.cpu_physical_address==std::optional<uae_u32>(mode==8?0:0xf0000000u|b.address),"CPU physical address preserves presence, zero and all32bits");
     need(fault.completion==(mode==5?BusCompletion::UnknownFault:BusCompletion::FaultBeforeTransfer),"completion status is not inferred from origin");
    }
    need(regs==b.at_fault,"terminal detailed failure does not roll back or fabricate CPU state");
    const auto count=b.calls;const auto snapshot=regs;
    for(unsigned budget:{0u,1u,17u}){m040_result stopped{};need(m040_run(budget,&stopped)==M040_OK&&stopped.exit==(bad_terminal?M040_BUDGET:M040_UNSUPPORTED)&&stopped.instructions==0&&b.calls==count&&regs==snapshot,"terminal latch prevents implicit retry or extra target access");}
    if(dump)std::cout<<f.name<<' '<<super<<' '<<dfc<<' '<<trace<<' '<<ccr<<' '<<n<<' '<<b.marker<<' '<<std::hex<<b.address<<' '<<b.size<<' '<<b.fc<<' '<<b.value<<' '<<b.pc<<std::dec<<'\n';
    ++rows;
   }
  }
 }
}
void malformed(){
 for(unsigned mode=1;mode<=8;++mode){Bus b;b.malformed=mode;attach(b,producer_cases::cases[2],true,5,0,0);m040_result r{};
  need(m040_run(1,&r)==M040_HOST_ERROR&&host.capability_latched&&!host.bus_fault&&host.exception_count==0,"malformed or contradictory reply is a host error, never a guest frame");
  const auto count=b.calls;need(m040_run(1,&r)==M040_OK&&r.exit==M040_UNSUPPORTED&&r.instructions==0&&b.calls==count,"host-error latch prevents execution after invalid evidence");
 }
}
void read_and_fetch(){
 for(unsigned kind:{unsigned(M040_READ),unsigned(M040_FETCH)})for(unsigned mode=0;mode<9;++mode){
  Bus b;b.fail=99;b.read_kind=kind;b.read_address=kind==M040_READ?0x5000:0x100;b.mode=mode;b.noise=true;
  attach(b,producer_cases::cases[0],true,5,0,0);b.put(0x100,2,0x1210);m040_result r{};
  need(m040_run(1,&r)==M040_OK&&r.exit==M040_MEMORY_FAULT&&r.vector==2&&r.address==b.read_address,"detailed read/fetch faults stay terminal");
  need(host.bus_fault&&host.bus_fault->detailed_profile&&!host.bus_fault->completed_producer&&
       host.bus_fault->request.kind==AccessKind(kind)&&host.bus_fault->request.address==b.read_address&&
       host.bus_fault->request.size==(kind==M040_READ?1u:2u)&&
       host.bus_fault->request.function_code==(kind==M040_READ?5u:6u),"read/fetch evidence binds exact request and cannot claim a completed writer");
  const auto& f=*host.bus_fault;
  need(f.in_instruction==(kind==M040_READ)&&f.data==0&&b.writes==0&&host.exception_count==0,"fetch versus operand boundary has no fabricated store or frame");
  need(mode==6?!f.evidence:(bool(f.evidence)&&!f.evidence->whole_scalar_no_commit),"read/fetch evidence cannot inherit prior completed-operation metadata");
  const unsigned calls=b.calls;need(m040_run(1,&r)==M040_OK&&r.exit==M040_UNSUPPORTED&&r.instructions==0&&b.calls==calls,"read/fetch latch prevents implicit retry");
 }
 // A core-side denial never calls the adapter: it has no supplied origin.
 Bus b;b.fail=99;attach(b,producer_cases::cases[2],true,5,0,0);
 host.denied=Access{AccessKind::Write,0x5000,4,0,false};m040_result r{};
 need(m040_run(1,&r)==M040_OK&&r.exit==M040_MEMORY_FAULT&&host.bus_fault&&host.bus_fault->detailed_profile&&
      !host.bus_fault->evidence&&host.bus_fault->completion==BusCompletion::FaultBeforeTransfer&&b.writes==0&&host.exception_count==0,
      "local denied access cannot fabricate adapter origin or fall back to legacy recovery");
}
void switching_and_reset(){
 Bus b;b.fail=99;attach(b,producer_cases::cases[0],true,5,0,0);b.put(0x100,2,0x4e71);
 m040_result r{};need(m040_run(1,&r)==M040_OK&&r.exit==M040_BUDGET,"successful detailed callback executes normally");
 b.put(0x102,2,0x2080);b.put(8,4,0x200);b.writes=0;b.fail=1;
 need(m040_set_precise_bus(Bus::legacy,&b)==M040_OK&&!host.detailed_bus,"precise callback selection detaches detailed profile");
 need(m040_run(1,&r)==M040_OK&&r.exit==M040_BUDGET&&r.vector==2&&host.exception_count==1&&host.bus_fault&&!host.bus_fault->detailed_profile&&!host.bus_fault->evidence,"legacy precise recovery retains its qualified frame behavior");
 need(b.get(0x8fc4+14,2)==0x85,"legacy writeback remains WB3 for the legacy contract");
 need(m040_reset(&options)==M040_OK&&!host.detailed_bus&&!host.bus_fault,"reset clears detailed attachment and evidence");
 need(m040_poke(0x100,2,0x4e71)==M040_OK&&m040_run(1,&r)==M040_OK&&r.exit==M040_BUDGET,"flat RAM resumes after reset without stale bus state");
 Bus second;attach(second,producer_cases::cases[2],true,5,0,0);
 need(m040_run(1,&r)==M040_OK&&r.exit==M040_MEMORY_FAULT,"prepare detailed terminal latch");const auto count=second.calls;m040_state s{};
 need(m040_get_state(&s)==M040_OK&&m040_set_state(&s)==M040_OK&&m040_set_detailed_bus(nullptr,nullptr)==M040_OK,"state import and callback detach retain fault latch");
 need(m040_run(1,&r)==M040_OK&&r.exit==M040_UNSUPPORTED&&r.instructions==0&&second.calls==count,"configuration change cannot bypass required reset");
}
void rte_halt_priority(){
 Bus b;b.fail=99;b.read_address=0x8fd0;attach(b,producer_cases::cases[0],true,5,0,0);
 m040_state s{};need(m040_get_state(&s)==M040_OK,"RTE detailed control state");s.pc=s.instruction_pc=0x200;s.regs[15]=s.isp=0x8fc4;need(m040_set_state(&s)==M040_OK,"RTE synthetic frame input");
 b.put(0x200,2,0x4e73);b.put(0x8fc4,2,0x2700);b.put(0x8fc6,4,0x102);b.put(0x8fca,2,0x7008);
 m040_result r{};need(m040_run(1,&r)==M040_OK&&r.exit==M040_DOUBLE_FAULT&&host.halted&&host.exception_count==0&&host.bus_fault&&host.bus_fault->rte_internal_load&&!host.bus_fault->completed_producer,"internal RTE fault retains architectural halt priority");
 int accepted=1;const auto count=b.calls;need(m040_interrupt(31,7,&accepted,&r)==M040_OK&&!accepted&&b.calls==count,"detailed RTE halt still rejects NMI without bus traffic");
}
}
int main(int argc,char**argv){try{
 for(int i=1;i<argc;++i){std::string_view a=argv[i];if(a=="--negative-producer")bad_producer=true;else if(a=="--negative-fresh")bad_fresh=true;else if(a=="--negative-data")bad_data=true;else if(a=="--negative-terminal")bad_terminal=true;else if(a=="--dump-producers")dump=true;else throw std::runtime_error("bad argument");}
 static_assert(M040_ABI_VERSION==1&&sizeof(m040_bus_detail)==24);
 matrix();if(!dump){malformed();read_and_fetch();switching_and_reset();rte_halt_priority();}
 std::cout<<"PASS: "<<checks<<" detailed per-request contract assertions; rows="<<rows<<" (typed failures terminal, no frame integration)\n";
}catch(const std::exception&e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}}
