/* SPDX-License-Identifier: GPL-2.0-or-later
 * Real CPU fault -> actual format7 builder -> guest MOVEC/MOVES cleanup/RTE.
 * WB1 origin/whole-write evidence is supplied by an owned, synchronous bus.
 */
#include "c-api.h"
#include "restart.hpp"
#include <iostream>
#include <string_view>
using namespace modern040;
namespace {
unsigned checks=0,frames=0;bool audit=false,bad_pc=false,bad_address=false,bad_lanes=false,bad_count=false;
void need(bool ok,const char*why){++checks;if(!ok)throw std::runtime_error(why);}
Result execute(unsigned budget){m040_result r{};need(m040_run(budget,&r)==M040_OK,"public execution boundary");return {Exit(r.exit),r.instructions,int(r.vector),r.instruction_pc,r.current_pc,r.address,r.size,r.detail};}
struct Bus {
 std::array<unsigned char,65536> ram{};
 unsigned logical=0x6000,physical=0x5000,denied_writes=0,target_reads=0,target_writes=0,mode=0,calls=0,deny_address=~0u;
 bool faulted=false,reentry_checked=false;State at_fault{};m040_bus_request cleanup{};
 unsigned map(unsigned a)const{
  if(a>=0x018bb4b2&&a<0x018bb4c2)return a-0x018bb4b2+0x100;
  if(a>=0x6000&&a<0x6040)return a-0x1000;
  return a;
 }
 void put(unsigned a,unsigned n,unsigned v){a=map(a);for(unsigned i=n;i;--i){ram.at(a+i-1)=v;v>>=8;}}
 unsigned get(unsigned a,unsigned n)const{a=map(a);unsigned v=0;for(unsigned i=0;i<n;++i)v=(v<<8)|ram.at(a+i);return v;}
 static int access(void*o,const m040_bus_request*q,uint32_t*v,m040_bus_detail*d){
  auto&b=*static_cast<Bus*>(o);
  ++b.calls;
#ifdef WB1_ADMITTED
  if(!b.reentry_checked){need(m040_physical_write_exceptions(0)==M040_BUSY,"physical profile cannot change during an active callback");b.reentry_checked=true;}
#endif
  if(q->address==b.deny_address){d->completion=M040_BUS_FAULT_BEFORE_TRANSFER;return 1;}
  if(q->kind==M040_WRITE&&q->address==b.logical&&!b.faulted){
   b.faulted=true;++b.denied_writes;b.at_fault=regs;
   d->completion=M040_BUS_FAULT_BEFORE_TRANSFER;d->origin=M040_ORIGIN_EXTERNAL_BUS;
   d->whole_scalar_no_commit=1;d->has_cpu_physical_address=1;d->cpu_physical_address=b.physical;
   if(b.mode==1)d->origin=M040_ORIGIN_UNSPECIFIED;
   if(b.mode==2)d->origin=M040_ORIGIN_ATC_TRANSLATION;
   if(b.mode==3)d->has_cpu_physical_address=0;
   if(b.mode==4)d->whole_scalar_no_commit=0;
   if(b.mode==5){d->completion=M040_BUS_UNKNOWN_FAULT;d->whole_scalar_no_commit=0;}
   if(b.mode==6)return 0;
   return 1;
  }
  if((q->address>=0x5000&&q->address<0x5040)||(q->address>=0x6000&&q->address<0x6040)){
   if(q->kind==M040_WRITE){++b.target_writes;b.cleanup=*q;}else ++b.target_reads;
  }
  if(q->kind==M040_WRITE)b.put(q->address,q->size,*v);else *v=b.get(q->address,q->size);
  d->completion=M040_BUS_COMPLETED;return 1;
 }
};
void emit(Bus&b,unsigned&pc,unsigned value){b.put(pc,2,value);pc+=2;}
void attach(Bus&b,unsigned sr){
 const m040_options options{M040_ABI_VERSION,1,0,0};
 need(m040_reset(&options)==M040_OK&&m040_fault_recovery(1)==M040_OK&&m040_set_detailed_bus(Bus::access,&b)==M040_OK,"attach actual C ABI detailed bus/restart bodies");
#ifdef WB1_ADMITTED
 need(m040_physical_write_exceptions(1)==M040_OK,"explicit physical WB1 profile opt-in");
#endif
 regs.usp=0x7000;regs.regs[15]=regs.isp=0x9000;regs.msp=0x8000;regs.sr=sr;MakeFromSR();
 regs.regs[15]=(sr&0x2000)?regs.isp:regs.usp;regs.regs[8]=b.logical;regs.dfc=7;regs.sfc=2;
 for(unsigned i=0;i<8;++i)regs.regs[i]=0x12340000+i*0x101;
 b.put(8,4,0x200);
}
void handler(Bus&b,unsigned size){
 unsigned pc=0x200;const auto e=[&](unsigned v){emit(b,pc,v);};
 for(unsigned i=0;i<3;++i){e(0x23c0+i);e(0);e(0x4000+4*i);} // save D0-D2
 e(0x23c8);e(0);e(0x400c);e(0x4e7a);e(0x2001); // save A0; DFC -> D2
 e(0x302f);e(18);e(0x2200);e(0x0281);e(0);e(7);e(0x4e7b);e(0x1001);
 e(0x206f);e(40);e(0x202f);e(44); // WB1A -> A0; WB1D -> D0
 e(0x2208);e(0x0281);e(0);e(3);e(0xe789);e(0xe3b8); // rotate memory lanes
 if(size==1){e(0xe088);e(0xe088);e(0xe088);}else if(size==2){e(0xe088);e(0xe088);}
 e(size==1?0x0e10:size==2?0x0e50:0x0e90);e(0x0800); // MOVES D0,(A0)
 e(0x426f);e(18);e(0x4e7b);e(0x2001); // invalidate WB1, restore DFC
 for(unsigned i=0;i<3;++i){e(0x2039+(i<<9));e(0);e(0x4000+4*i);}
 e(0x2079);e(0);e(0x400c);e(0x4e73);
}
void one(unsigned kind,unsigned size,unsigned low,unsigned ccr,unsigned value,bool supervisor,unsigned dfc){
 Bus b;b.logical+=low;b.physical+=low;attach(b,(supervisor?0x2500:0x500)|ccr);regs.regs[0]=value;
 for(unsigned i=0;i<16;++i)b.ram[0x5000+i]=0xa5;
 if(kind==2)regs.dfc=dfc;
 b.put(b.logical,size,value);b.put(0x100,2,kind==0?(size==1?0x1080:size==2?0x3080:0x2080):kind==1?(size==1?0x4610:size==2?0x4650:0x4690):(size==1?0x0e10:size==2?0x0e50:0x0e90));
 if(kind==2)b.put(0x102,2,0x0800);
 const unsigned next=kind==2?0x104:0x102;handler(b,size);b.put(next,2,0x2048);
 const unsigned mask=size==1?255:size==2?65535:~0u,expected=(kind==1?~value:value)&mask;
 const unsigned fc=kind==2?dfc:supervisor?5:1;const auto r=execute(1);
 if(audit){need(r.exit==Exit::MemoryFault&&host.bus_fault&&host.bus_fault->completed_producer&&host.exception_count==0,"before integration records certified completed write without fabricating a frame");return;}
 need(r.exit==Exit::Budget&&r.vector==2&&regs.pc==0x200,"actual certified write enters vector2 handler");const auto sp=regs.regs[15];
 need(b.get(sp+2,4)==(bad_pc?0x100u:next)&&b.get(sp+6,2)==0x7008,"actual frame saves completed PC and format7");
 const unsigned bits=size==1?0x20:size==2?0x40:0;
 need(b.get(sp+12,2)==(bits|fc)&&b.get(sp+18,2)==(0x80|bits|fc)&&b.get(sp+14,2)==0&&b.get(sp+16,2)==0,"external write selects WB1 only with exact SSW/FC/size and ATC clear");
 need(b.get(sp+20,4)==b.logical&&b.get(sp+40,4)==(bad_address?b.logical:b.physical),"logical FA and physical WB1 address remain distinct");
 unsigned lane_data=0;
 for(unsigned i=0;i<size;++i){unsigned lane=(low+i)&3;lane_data|=((expected>>(8*(size-1-i)))&255)<<(24-8*lane);}
 need(b.get(sp+44,4)==(bad_lanes?lane_data^1:lane_data),"actual WB1 data follows manual memory-lane alignment");
 unsigned used=0;for(unsigned i=0;i<size;++i)used|=255u<<(24-8*((low+i)&3));
 b.put(sp+44,4,lane_data|(0xa5a5a5a5u&~used)); // Unused hardware lanes need not be zero.
#ifdef WB1_ADMITTED
 need(!host.physical_writeback,"scoped writeback is absent after frame construction");
#endif
 const unsigned expected_ccr=kind==2?ccr:(ccr&16)|(expected==0?4:0)|((expected>>(size*8-1))?8:0);
 need(b.get(sp,2)==((supervisor?0x2500:0x500)|expected_ccr),"completed producer flags are preserved in the actual frame");
 for(unsigned steps=0;regs.pc!=next&&steps<80;++steps)need(execute(1).exit==Exit::Budget,"guest WB1 cleanup uses genuine instructions");
 need(regs.pc==next&&regs.regs[15]==(supervisor?0x9000u:0x7000u),"actual cleanup/RTE returns to next PC and original active bank");
 const auto state=architectural_state();
 need(state.sr==((supervisor?0x2500:0x500)|expected_ccr)&&state.dfc==(kind==2?dfc:7)&&state.sfc==2&&state.usp==0x7000&&state.isp==0x9000&&state.msp==0x8000,"cleanup/RTE preserves CCR, mode, all banks and original function-code registers");
 for(unsigned i=0;i<15;++i)need(regs.regs[i]==b.at_fault.regs[i],"cleanup preserves each completed non-stack GPR");
 need(b.get(b.physical,size)==expected&&b.denied_writes==1&&b.target_writes==(bad_count?2u:1u)&&b.target_reads==(kind==1?1u:0u),"one failed write and one software cleanup write, no duplicate or target reread");
 need(b.cleanup.address==b.physical&&b.cleanup.size==size&&b.cleanup.function_code==fc&&b.cleanup.has_alternate_function_code&&b.cleanup.alternate_function_code==fc&&!b.cleanup.transfer_type,"guest cleanup issues the exact physical width/address/FC transfer");
 for(unsigned i=0;i<16;++i)need(b.ram[0x5000+i]==(i>=low&&i<low+size?((expected>>(8*(size-1-(i-low))))&255):0xa5u),"cleanup preserves every adjacent owned RAM byte");
 const bool irq_control=kind==0&&size==4&&low==0&&ccr==0&&value==1&&supervisor;
 if(irq_control){m040_result irq{};int pending=0,accepted=0;need(m040_restart_pending(&pending)==M040_OK&&pending&&m040_interrupt(64,6,&accepted,&irq)==M040_OK&&!accepted&&regs.pc==next,"physical profile retains existing ordinary RTE IRQ holdoff");}
 need(execute(1).exit==Exit::Budget&&regs.pc==next+2&&b.target_writes==1,"next guest instruction executes without replaying completed store");++frames;
 if(irq_control){b.put(64*4,4,0x300);b.put(0x300,2,0x4e73);m040_result irq{};int accepted=0;need(m040_interrupt(64,6,&accepted,&irq)==M040_OK&&accepted&&regs.pc==0x300&&execute(1).exit==Exit::Budget&&regs.pc==next+2&&b.target_writes==1,"ordinary IRQ enters after existing holdoff and returns without a duplicate write");}
}
void peer(){
 Bus b;b.logical=b.physical=0xfffffa42;attach(b,0x2000);regs.pc=regs.instruction_pc=0x018bb4b2;
 b.put(0x100,2,0x31fc);b.put(0x102,2,3);b.put(0x104,2,0xfa42);b.put(0x106,2,0x33fc);
 auto r=execute(1);
 if(audit){need(r.exit==Exit::MemoryFault&&host.bus_fault&&host.bus_fault->completed_producer&&r.address==0xfffffa42&&r.size==2&&host.exception_count==0,"before exact peer instruction records its failed supervisor word write");return;}
 const auto sp=regs.regs[15];
 need(r.exit==Exit::Budget&&r.vector==2&&regs.pc==0x200&&b.get(sp+2,4)==0x018bb4b8&&b.get(sp+12,2)==0x45&&b.get(sp+18,2)==0xc5&&b.get(sp+20,4)==0xfffffa42&&b.get(sp+40,4)==0xfffffa42&&b.get(sp+44,4)==3,"exact owner opcode/PC/address produces a full32bit physical WB1 frame");
 unsigned pc=0x200;for(unsigned w:{0x4fefu,60u,0x4ef9u,0u,0x300u})emit(b,pc,w);b.put(0x300,2,0x4e71);
 need(execute(2).exit==Exit::Budget&&regs.pc==0x300&&regs.regs[15]==0x9000&&b.denied_writes==1&&b.target_writes==0,"owned discard-frame hardware-probe handler recovers without peripheral retry");
}
#ifdef WB1_ADMITTED
void rejection_and_scope(){
 for(unsigned mode=1;mode<=6;++mode){
  Bus b;b.mode=mode;attach(b,0x2000);b.put(0x100,2,0x2080);
  need(execute(1).exit==Exit::MemoryFault&&host.exception_count==0&&!host.physical_writeback&&b.denied_writes==1&&b.target_writes==0,"unknown/ATC/partial/missing evidence cannot fabricate a physical writeback frame");
  const auto calls=b.calls;need(execute(1).exit==Exit::Unsupported&&b.calls==calls,"rejected evidence retains reset-only terminal latch");
 }
 for(unsigned sr:{0x6000u,0xa000u,0x3000u}){
  Bus b;attach(b,sr);b.put(0x100,2,0x2080);
  need(execute(1).exit==Exit::MemoryFault&&host.exception_count==0&&!host.physical_writeback,"unqualified trace/master write state remains terminal");
 }
 for(unsigned opcode:{0x0188u,0x01c8u,0xead0u,0xecd0u,0xeed0u,0xefd0u,0x4e90u,0x6100u,0x4890u,0x48d0u}){
  Bus b;attach(b,0x2000);b.put(0x100,2,opcode);b.put(0x102,2,opcode==0x4890||opcode==0x48d0?3:opcode==0x6100?0x5efe:opcode==0x0188||opcode==0x01c8?0:0x0008);
  if(opcode==0x4e90||opcode==0x6100){b.logical=0x8ffc;regs.regs[8]=0x6000;}
  else b.put(b.logical,4,0x12345678);
  need(execute(1).exit==Exit::MemoryFault&&host.exception_count==0&&!host.physical_writeback&&host.bus_fault&&!host.bus_fault->completed_producer,"unfinished control/MOVEP/MOVEM/bitfield producers cannot borrow completed-scalar WB1");
 }
 for(unsigned address:{0x8ffcu,8u}){
  Bus b;b.deny_address=address;attach(b,0x2000);b.put(0x100,2,0x2080);
  need(execute(1).exit==Exit::DoubleFault&&host.halted&&!host.physical_writeback,"stack/vector failure halts and destroys scoped writeback metadata");
  const auto calls=b.calls;need(execute(1).exit==Exit::DoubleFault&&b.calls==calls,"double fault does not retry frame or target transfers");
 }
 Bus b;attach(b,0x2000);need(m040_physical_write_exceptions(2)==M040_BAD_ARGUMENT,"physical profile validates boolean control");
 need(m040_physical_write_exceptions(0)==M040_OK,"physical profile can be left explicitly disabled");b.put(0x100,2,0x2080);
 need(execute(1).exit==Exit::MemoryFault&&host.exception_count==0&&!host.physical_writeback,"detailed callback alone keeps .9.2 terminal contract");
 const m040_options options{M040_ABI_VERSION,1,0,0};need(m040_reset(&options)==M040_OK&&!host.physical_write_exceptions&&!host.physical_writeback,"reset clears opt-in and scoped frame state");
}
#endif
}
int main(int argc,char**argv){try{
 for(int i=1;i<argc;++i){std::string_view a=argv[i];if(a=="--before-audit")audit=true;else if(a=="--negative-pc")bad_pc=true;else if(a=="--negative-address")bad_address=true;else if(a=="--negative-lanes")bad_lanes=true;else if(a=="--negative-count")bad_count=true;else throw std::runtime_error("bad mode");}
 for(unsigned kind=0;kind<3;++kind)for(unsigned size:{1u,2u,4u})for(unsigned low=0;low<4;++low)for(unsigned ccr=0;ccr<32;++ccr)for(unsigned value:{0u,1u,0x80u,0x8000u,0x12345678u,0xffffffffu})for(bool supervisor:{false,true}){
  if(kind==2&&!supervisor)continue;for(unsigned dfc:{1u,5u}){if(kind!=2&&dfc!=5)continue;one(kind,size,low,ccr,value,supervisor,dfc);}
 }
 peer();
#ifdef WB1_ADMITTED
 if(!audit)rejection_and_scope();
#endif
 std::cout<<"PASS: "<<checks<<" physical WB1 assertions; actual-frames="<<frames<<(audit?" before-only":"")<<'\n';
}catch(const std::exception&e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}}
