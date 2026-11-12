/* SPDX-License-Identifier: GPL-2.0-or-later
 * MC68040UM PDF238: CT resumes pending trace through format7/RTE to format2.
 * Uncompleted reads retry before tracing; completed writes require WB3 first.
 * Expected bytes/PC/order derive independently from the manual and program.
 */
#include "restart.hpp"
#include <iostream>
#include <stdexcept>
#include <string_view>
using namespace modern040;
namespace {
unsigned checks=0;bool bad_pc=false,bad_order=false,bad_wb=false,bad_cycles=false;
Result observed{Exit::Budget};
void expect(bool yes,const char*why){++checks;if(!yes)throw std::runtime_error(why);}
Result step(unsigned n){return observed=run(n);}
struct Bus {
 std::array<uae_u8,65536> ram{};bool armed=true;AccessKind denied_kind=AccessKind::Read;uae_u32 denied=0x500;
 struct Event{BusRequest request;uae_u32 value;BusCompletion completion;};std::vector<Event>events;
 void put(unsigned a,unsigned size,uae_u32 v){for(unsigned i=size;i;--i){ram[a+i-1]=uae_u8(v);v>>=8;}}
 uae_u32 get(unsigned a,unsigned size)const{uae_u32 v=0;for(unsigned i=0;i<size;++i)v=(v<<8)|ram[a+i];return v;}
 static BusCompletion access(void*opaque,const BusRequest&r,uae_u32&v){auto&b=*static_cast<Bus*>(opaque);auto status=BusCompletion::Completed;
  if(b.armed&&r.kind==b.denied_kind&&r.address==b.denied){b.armed=false;status=BusCompletion::FaultBeforeTransfer;}
  else if(r.address>=b.ram.size()||r.size>b.ram.size()-r.address)status=BusCompletion::FaultBeforeTransfer;
  else if(r.kind==AccessKind::Write)b.put(r.address,r.size,v);else v=b.get(r.address,r.size);
  b.events.push_back({r,v,status});return status;}
 unsigned count(AccessKind kind,unsigned address,BusCompletion status)const{unsigned n=0;for(const auto&e:events)if(e.request.kind==kind&&e.request.address==address&&e.completion==status)++n;return n;}
};
void code(Bus&b,unsigned a,std::initializer_list<unsigned>words){for(auto v:words){b.put(a,2,v);a+=2;}}
unsigned kernel_top(unsigned sr){return sr&0x1000?0x8000:0x9000;}
void setup(Bus&b,unsigned sr){reset();install_restart_handlers();host.fault_recovery=host.deliver_exceptions=true;host.precise_bus=Bus::access;host.bus_opaque=&b;
 regs.regs[15]=regs.isp=0x9000;regs.usp=0x7000;regs.msp=0x8000;regs.sr=uae_u16(sr);MakeFromSR();
 for(unsigned i=0;i<15;++i)regs.regs[i]=0x11110000+i;regs.regs[0]=0x89abcdef;regs.regs[1]=0x12345678;regs.regs[8]=0x500;
 b.put(8,4,0x200);b.put(36,4,0x300);code(b,0x200,{0x4e73});code(b,0x300,{0x0257,0x3fff,0x4e73});code(b,0x102,{0x4e71,0x6002,0x4e71,0x4e71});
 b.put(0x500,4,0x11223344);for(unsigned i=0;i<12;++i)b.put(kernel_top(sr)+i,1,0xa5^i);
}
void guard(const Bus&b,unsigned sr){for(unsigned i=0;i<12;++i)expect(b.get(kernel_top(sr)+i,1)==(0xa5^i),"RTE CT must not write above the original stack top");}
unsigned move_flags(unsigned ccr){return(ccr&16)|8;}
void check_frame(const Bus&b,unsigned sp,unsigned sr,unsigned pc,unsigned format){expect(b.get(sp,2)==sr,"exact saved SR");expect(b.get(sp+2,4)==pc+(bad_pc?2:0),"exact fault/trace saved PC");expect(b.get(sp+6,2)==format,"exact fault/trace format and vector");}
void wb_handler(Bus&b,unsigned size){
 // Preserve A0 with ordinary MOVEs; no test helper uses MOVEM.
 code(b,0x200,{0x23c8,0,0x6000,0x206f,24,size==1?0x10afu:size==2?0x30afu:0x20afu,size==1?31u:size==2?30u:28u,0x2079,0,0x6000,0x426f,14,0x4e73});
}
void check_gpr(const State&before,unsigned changed,unsigned value){for(unsigned i=0;i<15;++i)expect(regs.regs[i]==(i==changed?value:before.regs[i]),"all GPR values survive trace/fault handling");}
void check_banks(const State&before){const auto after=architectural_state();expect(after.usp==before.usp&&after.isp==before.isp&&after.msp==before.msp,"all banked stack pointers are restored after trace return");}
void traced_reads(){
 for(unsigned size:{1u,2u,4u})for(unsigned mode:{0x8000u,0x9000u,0xa000u,0xb000u})for(unsigned ccr=0;ccr<32;++ccr){
  Bus b;setup(b,mode|ccr);b.put(0x500,4,0x89abcdef);b.put(0x100,2,size==1?0x1210:size==2?0x3210:0x2210);const auto before=architectural_state();
  expect(step(1).exit==Exit::Budget&&regs.pc==0x200,"traced read enters vector2 before any trace");const auto fs=kernel_top(mode)-60;
  check_frame(b,fs,mode|ccr,0x100,0x7008);expect(b.get(fs+12,2)==(0x100|(size==1?0x20:size==2?0x40:0)|((mode&0x2000)?5:1))&&b.get(fs+14,2)==0,"uncompleted traced read has no CT or WB3");
  expect(host.exception_count==(bad_order?2u:1u),"no trace before interrupted read completes");
  expect(step(1).exit==Exit::Budget&&regs.pc==0x100&&host.exception_count==1,"RTE returns to faulting read without tracing RTE");
  expect(step(1).exit==Exit::Budget&&regs.pc==0x300&&host.exception_count==2,"retried read completes then delivers exactly one Trace9");
  const auto ts=kernel_top(mode)-12;check_frame(b,ts,mode|move_flags(ccr),0x102,0x2024);expect(regs.regs[15]==ts,"read trace selects correct active A7");expect(b.get(ts+8,4)==0x100,"trace identifies original interrupted instruction");
  const auto value=size==1?0x12345689u:size==2?0x123489abu:0x89abcdefu;check_gpr(before,1,value);
  expect(b.count(AccessKind::Read,0x500,BusCompletion::Completed)==(bad_cycles?2u:1u)&&b.count(AccessKind::Read,0x500,BusCompletion::FaultBeforeTransfer)==1,"exact read transfer counts after trace recovery");
  guard(b,mode);expect(step(2).exit==Exit::Budget&&regs.pc==0x102&&host.exception_count==2,"trace handler clears T and returns without retracing");expect(regs.regs[15]==before.regs[15],"trace/read restores active stack");check_banks(before);
 }
}
void traced_writes(){
 for(unsigned size:{1u,2u,4u})for(unsigned mode:{0x8000u,0x9000u,0xa000u,0xb000u})for(unsigned ccr=0;ccr<32;++ccr){
  Bus b;setup(b,mode|ccr);b.denied_kind=AccessKind::Write;b.put(0x100,2,size==1?0x1080:size==2?0x3080:0x2080);wb_handler(b,size);const auto before=architectural_state();
  expect(step(1).exit==Exit::Budget&&regs.pc==0x200,"traced completed write enters vector2 before pending trace");const auto fs=kernel_top(mode)-60,flags=move_flags(ccr),fc=(mode&0x2000)?5u:1u;
  check_frame(b,fs,mode|flags,0x102,0x7008);expect(b.get(fs+8,4)==0x100&&b.get(fs+12,2)==(0x2000|(size==1?0x20:size==2?0x40:0)|fc),"completed traced write stacks CT and traced instruction EA");
  const auto data=0x89abcdefu&(size==1?255u:size==2?65535u:0xffffffffu);
  expect(b.get(fs+14,2)==(0x80|(size==1?0x20:size==2?0x40:0)|fc)&&b.get(fs+24,4)==0x500&&b.get(fs+28,4)==(data^(bad_wb?1u:0u)),"pending WB3 retains exact width address and value");
  expect(host.exception_count==1&&b.get(0x500,4)==0x11223344,"pending trace does not bypass software writeback");
  expect(step(6).exit==Exit::Budget&&regs.pc==0x300&&host.exception_count==2,"WB3 software completion and RTE deliver pending trace before following opcode");
  const auto ts=kernel_top(mode)-12;check_frame(b,ts,mode|flags,0x102,0x2024);expect(regs.regs[15]==ts,"write trace selects correct active A7");expect(b.get(ts+8,4)==0x100,"CT conversion preserves traced instruction address");check_gpr(before,16,0);
  expect(b.get(0x500,size)==data&&b.count(AccessKind::Write,0x500,BusCompletion::Completed)==1&&b.count(AccessKind::Write,0x500,BusCompletion::FaultBeforeTransfer)==1,"one software WB3 store and no implicit retry");
  guard(b,mode);expect(step(2).exit==Exit::Budget&&regs.pc==0x102&&host.exception_count==2&&regs.regs[15]==before.regs[15],"trace/write returns correct PC and stack without duplicate trace");check_banks(before);
 }
}
void traced_fetches(){
 for(bool extension:{false,true})for(unsigned mode:{0x8000u,0x9000u,0xa000u,0xb000u})for(unsigned ccr=0;ccr<32;++ccr){
  Bus b;setup(b,mode|ccr);b.denied_kind=AccessKind::Fetch;b.denied=extension?0x102:0x100;
  if(extension){code(b,0x100,{0x0081,0,1,0x4e71});}else b.put(0x100,2,0x4e71);
  const auto before=architectural_state();expect(step(1).exit==Exit::Budget&&regs.pc==0x200,"traced fetch enters vector2 before trace");const auto fs=kernel_top(mode)-60;
  check_frame(b,fs,mode|ccr,0x100,0x7008);expect(b.get(fs+12,2)==(0x100|(extension?0:0x40)|((mode&0x2000)?6:2))&&b.get(fs+14,2)==0,"uncompleted program fetch has exact width/FC and no CT/WB3");
  expect(step(1).exit==Exit::Budget&&regs.pc==0x100&&host.exception_count==1,"fetch-fault RTE returns to opcode without tracing itself");
  expect(step(1).exit==Exit::Budget&&regs.pc==0x300&&host.exception_count==2,"fetched instruction completes then traces exactly once");
  const unsigned next=extension?0x106:0x102,flags=extension?(ccr&16):ccr;
  check_frame(b,kernel_top(mode)-12,mode|flags,next,0x2024);expect(regs.regs[15]==kernel_top(mode)-12,"fetch trace selects correct active A7");expect(b.get(kernel_top(mode)-4,4)==0x100,"fetch retry trace identifies original opcode");
  check_gpr(before,extension?1:16,0x12345679);expect(b.count(AccessKind::Fetch,b.denied,BusCompletion::Completed)==1&&b.count(AccessKind::Fetch,b.denied,BusCompletion::FaultBeforeTransfer)==1,"one failed and one guest-directed successful program fetch");
  guard(b,mode);expect(step(2).exit==Exit::Budget&&regs.pc==next&&host.exception_count==2,"fetch trace handler returns without duplicate event");check_banks(before);
 }
}
void pending_ct(){
 for(unsigned mode:{0x8000u,0x9000u,0xa000u,0xb000u})for(unsigned ccr=0;ccr<32;++ccr){
  Bus b;setup(b,0x2700|(mode&0x1000));b.armed=false;const unsigned top=kernel_top(mode),sp=top-60;regs.regs[15]=sp;regs.pc=0x200;
  b.put(sp,2,mode|ccr);b.put(sp+2,4,0x102);b.put(sp+6,2,0x7008);b.put(sp+8,4,0x100);b.put(sp+12,2,0x2045);b.put(sp+14,2,0);b.put(0x102,2,0x7e63);const auto d7=regs.regs[7];
  expect(step(1).exit==Exit::Budget&&regs.pc==0x300&&host.exception_count==1,"pending CT RTE delivers trace before next opcode");
  check_frame(b,top-12,mode|ccr,0x102,0x2024);expect(regs.regs[15]==top-12,"CT selects correct active trace stack");expect(b.get(top-4,4)==0x100&&regs.regs[7]==d7,"CT must not trace RTE or execute following MOVEQ");guard(b,mode);
  expect(step(2).exit==Exit::Budget&&regs.pc==0x102&&host.exception_count==1,"CT trace handler returns exactly once");const auto banks=architectural_state();expect(banks.usp==0x7000&&banks.isp==0x9000&&banks.msp==0x8000,"CT restores all three stack banks");
  expect(step(1).exit==Exit::Budget&&regs.regs[7]==99&&host.exception_count==1,"next opcode executes only after trace handler returns");
 }
 {
  Bus b;setup(b,0x2700);b.armed=false;regs.pc=0x200;regs.regs[15]=0x8fc4;b.put(12,4,0x400);b.put(0x8fc4,2,0xa700);b.put(0x8fc6,4,0x103);b.put(0x8fca,2,0x7008);b.put(0x8fcc,4,0x100);b.put(0x8fd0,2,0x2045);
  expect(step(1).exit==Exit::Budget&&regs.pc==0x400&&host.last_frame&&host.last_frame->vector==3&&host.exception_count==1,"RTE odd-PC address error precedes pending trace");
 }
 for(unsigned unsupported:{0x8000u,0x4000u,0xa000u,0x6000u}){
  Bus b;setup(b,0x2700);b.armed=false;regs.pc=0x200;regs.regs[15]=0x8fc4;b.put(0x8fc4,2,0x2700);b.put(0x8fc6,4,0x102);b.put(0x8fca,2,0x7008);b.put(0x8fd0,2,unsupported);
  expect(step(1).exit==Exit::Unsupported&&host.exception_count==0,"FPU CP/CU continuation remains explicitly gated");
 }
}
void t0_faults(){
 for(bool write:{false,true})for(unsigned mode:{0x4000u,0x5000u,0x6000u,0x7000u})for(unsigned ccr=0;ccr<32;++ccr){
  Bus b;setup(b,mode|ccr);b.put(0x100,2,write?0x2080:0x2210);b.put(0x102,2,0x2048); // MOVEA.L A0,A0 preserves flags and does not force a pipeline refill.
  if(write){b.denied_kind=AccessKind::Write;wb_handler(b,4);}else b.put(0x500,4,0x89abcdef);const auto before=architectural_state();
  expect(step(1).exit==Exit::Budget&&regs.pc==0x200,"T0 nonflow fault enters vector2 without trace");const unsigned sp=kernel_top(mode)-60;
  expect((b.get(sp+12,2)&0x2000)==0,"T0 data instruction does not set CT");
  expect(step(write?6:1).exit==Exit::Budget&&regs.pc==(write?0x102u:0x100u),"T0 returns through actual RTE");if(!write)expect(step(1).exit==Exit::Budget&&regs.pc==0x102,"T0 read retries without tracing nonflow instruction");
  expect(host.exception_count==1,"T0 data completion has no premature trace");expect(step(1).exit==Exit::Budget&&regs.pc==0x104&&host.exception_count==1,"T0 following MOVEA does not trace");
  expect(step(1).exit==Exit::Budget&&regs.pc==0x300&&host.exception_count==2,"T0 traces following taken branch");check_frame(b,kernel_top(mode)-12,mode|move_flags(ccr),0x108,0x2024);expect(regs.regs[15]==kernel_top(mode)-12,"T0 data/branch trace uses correct active A7");expect(b.get(kernel_top(mode)-4,4)==0x104,"T0 trace identifies actual branch rather than interrupted data instruction");check_gpr(before,write?16:1,0x89abcdef);guard(b,mode);expect(step(2).exit==Exit::Budget&&regs.pc==0x108,"T0 branch trace returns correctly");check_banks(before);
 }
 for(unsigned mode:{0x4000u,0x5000u,0x6000u,0x7000u})for(unsigned ccr=0;ccr<32;++ccr){
  Bus b;setup(b,mode|ccr);code(b,0x100,{0x4ef0,0x0151});b.put(0x500,4,0x400);const auto before=architectural_state();expect(step(1).exit==Exit::Budget&&regs.pc==0x200,"T0 indexed JMP pointer fault reaches access handler");
  expect(step(1).exit==Exit::Budget&&regs.pc==0x100,"T0 pointer fault RTE returns to JMP");expect(step(1).exit==Exit::Budget&&regs.pc==0x300&&host.exception_count==2,"T0 traces JMP only after pointer retry completes");check_frame(b,kernel_top(mode)-12,mode|ccr,0x400,0x2024);expect(b.get(kernel_top(mode)-4,4)==0x100,"T0 pointer-retry trace preserves branch PC");
  expect(b.count(AccessKind::Read,0x500,BusCompletion::Completed)==1&&b.count(AccessKind::Read,0x500,BusCompletion::FaultBeforeTransfer)==1,"T0 pointer is read once per guest attempt");expect(regs.regs[15]==kernel_top(mode)-12,"T0 JMP trace uses correct active A7");check_gpr(before,16,0);guard(b,mode);expect(step(2).exit==Exit::Budget&&regs.pc==0x400,"T0 JMP trace returns correctly");check_banks(before);
 }
 // MC68040UM section8.2.6 explicitly includes NOP among T0-traced instructions.
 for(unsigned mode:{0x4000u,0x5000u,0x6000u,0x7000u})for(unsigned ccr=0;ccr<32;++ccr){
  Bus b;setup(b,mode|ccr);b.armed=false;b.put(0x100,2,0x4e71);const auto before=architectural_state();
  expect(step(1).exit==Exit::Budget&&regs.pc==0x300&&host.exception_count==1,"T0 NOP traces as specified for the 68040");check_frame(b,kernel_top(mode)-12,mode|ccr,0x102,0x2024);expect(b.get(kernel_top(mode)-4,4)==0x100,"T0 NOP trace identifies the NOP itself");check_gpr(before,16,0);guard(b,mode);expect(step(2).exit==Exit::Budget&&regs.pc==0x102&&host.exception_count==1,"T0 NOP trace returns without duplication");check_banks(before);
 }
}
}
int main(int argc,char**argv){try{std::string_view group="all";for(int i=1;i<argc;++i){const std::string_view m=argv[i];if(m=="--only-read")group="read";else if(m=="--only-write")group="write";else if(m=="--only-ct")group="ct";else if(m=="--only-t0")group="t0";else if(m=="--only-fetch")group="fetch";else if(m=="--negative-pc")bad_pc=true;else if(m=="--negative-order")bad_order=true;else if(m=="--negative-wb")bad_wb=true;else if(m=="--negative-cycles")bad_cycles=true;else throw std::runtime_error("bad argument");}install_handlers();if(group=="all"||group=="read")traced_reads();if(group=="all"||group=="write")traced_writes();if(group=="all"||group=="fetch")traced_fetches();if(group=="all"||group=="ct")pending_ct();if(group=="all"||group=="t0")t0_faults();std::cout<<"PASS: "<<checks<<" integer trace/fault/CT/RTE/frame/bank/transfer assertions\n";}catch(const std::exception&e){std::cerr<<"FAIL: "<<e.what()<<" observed="<<exit_name(observed.exit)<<" vector="<<observed.vector<<" pc="<<std::hex<<regs.pc<<" address="<<observed.address<<" detail="<<observed.detail<<'\n';return 1;}}
