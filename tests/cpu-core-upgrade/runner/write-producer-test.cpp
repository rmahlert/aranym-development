/* SPDX-License-Identifier: GPL-2.0-or-later
 * Dynamic producer audit, NOT a full hardware stack-write qualification.
 * Establish why write SSW alone cannot clear this adapter's restart holdoff.
 * All operand/pointer/stack storage is owned RAM; retries follow actual guest
 * software WB3 completion and RTE, never a hidden host replay.
 */
#include "restart.hpp"
#include <iostream>
#include <stdexcept>
#include <string_view>
using namespace modern040;
namespace {
unsigned checks=0;bool bad_pc=false,bad_count=false,bad_pointer=false;
void expect(bool yes,const char*why){++checks;if(!yes)throw std::runtime_error(why);}
struct Bus {
 std::array<uae_u8,65536>ram{};bool armed=true;
 struct Event{BusRequest request;uae_u32 value;BusCompletion status;};std::vector<Event>events;
 void put(unsigned a,unsigned size,uae_u32 v){for(unsigned i=size;i;--i){ram.at(a+i-1)=uae_u8(v);v>>=8;}}
 unsigned get(unsigned a,unsigned size)const{unsigned v=0;for(unsigned i=0;i<size;++i)v=(v<<8)|ram.at(a+i);return v;}
 static BusCompletion access(void*o,const BusRequest&r,uae_u32&v){auto&b=*static_cast<Bus*>(o);auto status=BusCompletion::Completed;
  if(b.armed&&r.kind==AccessKind::Write&&r.address==0x6ffc){b.armed=false;status=BusCompletion::FaultBeforeTransfer;}
  else if(r.kind==AccessKind::Write)b.put(r.address,r.size,v);else v=b.get(r.address,r.size);
  b.events.push_back({r,v,status});return status;}
 unsigned count(AccessKind kind,unsigned a,BusCompletion status)const{unsigned n=0;for(const auto&e:events)if(e.request.kind==kind&&e.request.address==a&&e.status==status)++n;return n;}
};
struct Form {unsigned opcode,extra1,extra2,bytes,target,a0,d1;bool indirect;};
const Form forms[]={
 {0x4e90,0,0,2,0x400,0x400,0,false},{0x4ea8,4,0,4,0x400,0x3fc,0,false},
 {0x4eb0,0x0151,0,4,0x400,0x500,0,true},{0x4eb8,0x400,0,4,0x400,0,0,false},
 {0x4eb9,0,0x400,6,0x400,0,0,false},{0x4eba,0x2fe,0,4,0x400,0,0,false},
 {0x4ebb,0x1000,0,4,0x400,0,0x2fe,false},{0x6100,0x2fe,0,4,0x400,0,0,false},
 {0x613e,0,0,2,0x140,0,0,false},{0x61ff,0,0x2fe,6,0x400,0,0,false}};
void run_cases(){for(const auto&f:forms)for(unsigned mode:{0u,0x4000u,0x8000u})for(unsigned ccr=0;ccr<32;++ccr){
 Bus b;reset();install_handlers();install_restart_handlers();host.deliver_exceptions=host.fault_recovery=true;host.precise_bus=Bus::access;host.bus_opaque=&b;
 regs.regs[15]=regs.isp=0x9000;regs.usp=0x7000;regs.msp=0x8000;regs.sr=uae_u16(mode|0x500|ccr);MakeFromSR();
 for(unsigned i=0;i<15;++i)regs.regs[i]=0x11110000+i;regs.regs[8]=f.a0;regs.regs[1]=f.d1;const auto before=architectural_state();
 b.put(8,4,0x200);b.put(36,4,0x300);b.put(69*4,4,0x700);b.put(0x500,4,0x400);b.put(0x100,2,f.opcode);b.put(0x102,2,f.extra1);b.put(0x104,2,f.extra2);
 // Genuine six-instruction software WB3 completion; preserve A0, clear V, RTE.
 unsigned a=0x200;for(unsigned word:{0x23c8u,0u,0x6000u,0x206fu,24u,0x20afu,28u,0x2079u,0u,0x6000u,0x426fu,14u,0x4e73u}){b.put(a,2,word);a+=2;}
 const auto fault=run(1);const unsigned sp=0x9000-60;
 expect(fault.exit==Exit::Budget&&fault.vector==2&&regs.pc==0x200,"uncompleted stack write reaches vector2");
 expect(b.get(sp,2)==(mode|0x500|ccr)&&b.get(sp+2,4)==(bad_pc?0x100+f.bytes:0x100u)&&b.get(sp+6,2)==0x7008,"uncompleted producer stacks original PC and flags, not completed return PC");
 expect(b.get(sp+12,2)==1&&b.get(sp+14,2)==0x81&&b.get(sp+24,4)==0x6ffc&&b.get(sp+28,4)==0x100+f.bytes,"write SSW alone also describes an unfinished JSR/BSR stack write");
 expect(regs.usp==0x7000&&regs.regs[8]==before.regs[8],"failed control producer has not committed SP or target PC");
 expect(b.count(AccessKind::Write,0x6ffc,BusCompletion::Completed)==0&&b.count(AccessKind::Write,0x6ffc,BusCompletion::FaultBeforeTransfer)==1,"no implicit successful stack write before guest handler");
 expect(run(6).exit==Exit::Budget&&regs.pc==0x100&&regs.regs[15]==0x7000,"guest WB3/RTE returns to the unfinished control instruction");
 expect(!inject_interrupt(69,6),"unfinished write producer still needs restart IRQ holdoff");
 expect(b.count(AccessKind::Write,0x6ffc,BusCompletion::Completed)==1,"only explicit software WB3 completed before instruction restart");
 expect(run(1).exit==Exit::Budget&&regs.pc==(mode?0x300u:f.target),"guest-directed control retry completes and traces when selected");
 expect(b.count(AccessKind::Write,0x6ffc,BusCompletion::Completed)==(bad_count?3u:2u)&&b.get(0x6ffc,4)==0x100+f.bytes,"dynamic audit distinguishes software WB3 from the later instruction store");
 expect(b.count(AccessKind::Read,0x500,BusCompletion::Completed)==(f.indirect?(bad_pointer?1u:2u):0u),"indirect RAM pointer repeats only after actual guest RTE");
 const auto after=architectural_state();expect(after.usp==0x6ffc,"retried control instruction commits its user stack exactly once");
 for(unsigned i=0;i<15;++i)expect(after.regs[i]==before.regs[i],"all GPR values survive software writeback and explicit retry");
 if(mode)expect(b.get(0x8ff6,4)==f.target&&b.get(0x8ffc,4)==0x100,"T1/T0 control retry identifies original instruction and destination");
}}
}
int main(int argc,char**argv){try{for(int i=1;i<argc;++i){const std::string_view arg=argv[i];if(arg=="--negative-pc")bad_pc=true;else if(arg=="--negative-count")bad_count=true;else if(arg=="--negative-pointer")bad_pointer=true;else throw std::runtime_error("bad argument");}run_cases();std::cout<<"PASS: "<<checks<<" unfinished control-write producer audit assertions (RAM only, not full hardware qualification)\n";}catch(const std::exception&e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}}
