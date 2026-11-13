/* SPDX-License-Identifier: GPL-2.0-or-later
 * MC68040UM PDF223 explicitly T0-traces MOVES. A completed failed write
 * must retain that trace through WB3/RTE; ordinary MOVE must not acquire it.
 * Alternate data/instruction FC1/2/5/6 use the existing physical-bus contract.
 */
#include "restart.hpp"
#include <iostream>
#include <stdexcept>
#include <string_view>
using namespace modern040;
namespace {
unsigned checks=0;bool bad_pc=false,bad_count=false,bad_fc=false;
void expect(bool yes,const char*why){++checks;if(!yes)throw std::runtime_error(why);}
struct Bus {
 std::array<uae_u8,65536>ram{};bool armed=true,read_fault=false;
 struct Event{BusRequest r;uae_u32 data;BusCompletion status;};std::vector<Event>events;
 void put(unsigned a,unsigned size,uae_u32 v){for(unsigned i=size;i;--i){ram.at(a+i-1)=uae_u8(v);v>>=8;}}
 unsigned get(unsigned a,unsigned size)const{unsigned v=0;for(unsigned i=0;i<size;++i)v=(v<<8)|ram.at(a+i);return v;}
 static BusCompletion access(void*opaque,const BusRequest&r,uae_u32&v){auto&b=*static_cast<Bus*>(opaque);auto status=BusCompletion::Completed;
  if(b.armed&&r.address==0x500&&r.kind==(b.read_fault?AccessKind::Read:AccessKind::Write)){b.armed=false;status=BusCompletion::FaultBeforeTransfer;}
  else if(r.kind==AccessKind::Write)b.put(r.address,r.size,v);else v=b.get(r.address,r.size);
  b.events.push_back({r,v,status});return status;}
 unsigned count(AccessKind kind,BusCompletion status)const{unsigned n=0;for(const auto&e:events)if(e.r.address==0x500&&e.r.kind==kind&&e.status==status)++n;return n;}
};
struct EA {unsigned mode,reg,base,bytes,first,second;int update;bool indirect;};
const EA forms[]={{2,0,0x500,0,0,0,0,false},{3,0,0x500,0,0,0,1,false},
 {4,0,0x500,0,0,0,-1,false},{5,0,0x4fc,2,4,0,0,false},
 {6,0,0x400,2,0x1000,0,0,false},{6,0,0x700,2,0x0151,0,0,true},
 {7,0,0x500,2,0x500,0,0,false},{7,1,0x500,4,0,0x500,0,false}};
void run_cases(unsigned group){
 for(const auto&ea:forms)for(unsigned size:{1u,2u,4u})for(unsigned fc:{1u,2u,5u,6u})for(unsigned ccr=0;ccr<32;++ccr){
  const bool t1=group==1,nonflow=group==2,reading=group==3,nofault=group==4;const unsigned t=t1?0x8000:0x4000,sr=0x2700|t|ccr,next=(nonflow?0x102:0x104)+ea.bytes,effective=fc==2?1:fc==6?5:fc,width=size==1?0x20:size==2?0x40:0,mask=size==1?255:size==2?65535:0xffffffffu;
  Bus b;b.read_fault=reading;b.armed=!nofault;reset();install_handlers();install_restart_handlers();host.deliver_exceptions=host.fault_recovery=true;host.precise_bus=Bus::access;host.bus_opaque=&b;
  regs.regs[15]=regs.isp=0x9000;regs.usp=0x7000;regs.msp=0x8000;regs.sr=uae_u16(sr);MakeFromSR();regs.sfc=regs.dfc=fc;
  for(unsigned i=0;i<15;++i)regs.regs[i]=0x11110000+i;regs.regs[0]=0xfedcba98;regs.regs[1]=0x100;regs.regs[8]=ea.base+(ea.update<0?size:0);const auto before=architectural_state();
  b.put(8,4,0x200);b.put(36,4,0x300);b.put(0x500,4,0x89abcdef);b.put(0x700,4,0x500);
  b.put(0x100,2,nonflow?(size==1?0x1000:size==2?0x3000:0x2000)+(ea.mode<<6)+(ea.reg<<9):0x0e00+(size==1?0:size==2?0x40:0x80)+(ea.mode<<3)+ea.reg);
  if(!nonflow)b.put(0x102,2,reading?0:0x0800);const unsigned extension=nonflow?0x102:0x104;if(ea.bytes)b.put(extension,2,ea.first);if(ea.bytes==4)b.put(extension+2,2,ea.second);b.put(next,2,0x2249);
  unsigned pc=0x200;for(unsigned word:{0x23c8u,0u,0x6000u,0x206fu,24u,size==1?0x10afu:size==2?0x30afu:0x20afu,size==1?31u:size==2?30u:28u,0x2079u,0u,0x6000u,0x426fu,14u,0x4e73u}){b.put(pc,2,word);pc+=2;}
  if(reading)b.put(0x200,2,0x4e73);b.put(0x300,2,0x0257);b.put(0x302,2,0x3fff);b.put(0x304,2,0x4e73);
  expect(run(1).exit==Exit::Budget&&regs.pc==(nofault?0x300u:0x200u),"MOVES/control reaches expected real exception handler");
  const unsigned flags=nonflow?(ccr&16)|8:ccr,sp=0x8fc4;
  if(!nofault){
   const unsigned ct=reading||nonflow?0:0x2000;
   if(b.get(sp+12,2)!=(ct|width|(reading?0x100:0)|(nonflow?5:effective)))std::cerr<<"MOVES before observed SSW="<<std::hex<<b.get(sp+12,2)<<" expected="<<(ct|width|(reading?0x100:0)|(nonflow?5:effective))<<" opcode="<<b.get(0x100,2)<<" framePC="<<b.get(sp+2,4)<<'\n';
   expect(b.get(sp+12,2)==(ct|width|(reading?0x100:0)|(nonflow?5:effective)),"MOVES T0 completed write preserves pending CT; read/nonflow controls do not");
   expect(b.get(sp,2)==(0x2700|t|(reading?ccr:flags))&&b.get(sp+2,4)==(reading?0x100u:next)&&b.get(sp+6,2)==0x7008&&b.get(sp+8,4)==(ct?0x100u:0u),"exact completed/unfinished fault frame SR PC EA");
   expect(host.exception_count==1,"no trace occurs before failed access recovery");
   expect(b.get(sp+14,2)==(reading?0:0x80|width|(nonflow?5:effective))&&(reading||(b.get(sp+24,4)==0x500&&b.get(sp+28,4)==(before.regs[0]&mask))),"MOVES/control WB3 preserves exact width address and data");
   for(const auto&e:b.events)if(e.status==BusCompletion::FaultBeforeTransfer){expect(e.r.function_code==(bad_fc?7u:nonflow?5u:effective)&&e.r.alternate_function_code==(nonflow?std::optional<unsigned>{}:std::optional<unsigned>{fc})&&e.r.transfer_type==0,"fault retains exact normalized and selected alternate FC");}
   expect(!ismoves_nommu,"MOVES transient alternate-FC mode clears on fault unwind");
   expect(run(reading?1:6).exit==Exit::Budget&&regs.pc==(reading?0x100u:nonflow?next:0x300u),"actual guest WB3/RTE or read RTE selects correct continuation");
   if(reading)expect(run(1).exit==Exit::Budget&&regs.pc==0x300,"retried MOVES read completes before T0 trace");
  }
  if(nonflow){expect(host.exception_count==1&&run(1).exit==Exit::Budget&&regs.pc==next+2&&host.exception_count==1,"ordinary T0 MOVE/writeback does not trace following nonflow MOVEA");}
  else{
   expect(host.exception_count==(nofault?1u:2u)&&b.get(0x8ff4,2)==(0x2700|t|ccr)&&b.get(0x8ff6,4)==next+(bad_pc?2:0)&&b.get(0x8ffa,2)==0x2024&&b.get(0x8ffc,4)==0x100,"MOVES trace frame identifies original instruction and exact next PC");
   expect(run(2).exit==Exit::Budget&&regs.pc==next,"trace handler clears T and returns once");
  }
  const auto after=architectural_state();for(unsigned i=0;i<15;++i)expect(after.regs[i]==(reading&&i==0?(before.regs[0]&~mask)|(0x89abcdefu>>(8*(4-size))):i==8?before.regs[i]+ea.update*size:before.regs[i]),"MOVES recovery preserves all unaffected GPRs");
  expect(after.regs[15]==0x9000&&after.usp==0x7000&&after.isp==0x9000&&after.msp==0x8000,"MOVES recovery restores all stack banks");
  expect(b.count(reading?AccessKind::Read:AccessKind::Write,BusCompletion::Completed)==(bad_count?2u:1u)&&b.count(reading?AccessKind::Read:AccessKind::Write,BusCompletion::FaultBeforeTransfer)==(nofault?0u:1u),"exact operand transfer counts after MOVES/control recovery");
  unsigned pointers=0;for(const auto&e:b.events)if(e.r.address==0x700&&e.r.kind==AccessKind::Read)++pointers;
  expect(pointers==(ea.indirect?(reading?2u:1u):0u),"indirect RAM pointer reads repeat only for explicit read-fault RTE retry");
  expect(b.get(0x500,size)==(reading?0x89abcdefu>>(8*(4-size)):before.regs[0]&mask),"MOVES/control completes exact physical RAM value");
 }
}
}
int main(int argc,char**argv){try{unsigned group=0;for(int i=1;i<argc;++i){const std::string_view x=argv[i];if(x=="--control-t1")group=1;else if(x=="--control-nonflow")group=2;else if(x=="--control-read")group=3;else if(x=="--control-nofault")group=4;else if(x=="--negative-pc")bad_pc=true;else if(x=="--negative-count")bad_count=true;else if(x=="--negative-fc")bad_fc=true;else throw std::runtime_error("bad argument");}run_cases(group);std::cout<<"PASS: "<<checks<<" MOVES T0/CT/FC/WB3/RTE/GPR/transfer assertions\n";}catch(const std::exception&e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}}
