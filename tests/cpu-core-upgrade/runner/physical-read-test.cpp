/* SPDX-License-Identifier: GPL-2.0-or-later
 * Certified physical read/fetch faults, actual format7, guest repair and RTE.
 * The owned bus supplies original-request no-transfer evidence explicitly.
 */
#include "c-api.h"
#include "restart.hpp"
#include <iostream>
#include <string_view>
using namespace modern040;
namespace {
unsigned checks=0,frames=0;bool before=false,bad_pc=false,bad_fc=false,bad_count=false,bad_fixup=false;
void need(bool ok,const char*why){++checks;if(!ok)throw std::runtime_error(why);}
Result execute(unsigned n){m040_result r{};need(m040_run(n,&r)==M040_OK,"public execution boundary");return {Exit(r.exit),r.instructions,int(r.vector),r.instruction_pc,r.current_pc,r.address,r.size,r.detail};}
struct Bus {
 std::array<unsigned char,65536> ram{};
 unsigned denied=0x6000,kind=M040_READ,mode=0,faults=0,successes=0,first_reads=0,calls=0,deny_nested=~0u;
 bool repaired=false,reentry=false,flip_marker=false;m040_bus_request failed{};
 unsigned map(unsigned a)const{
  if(a>=0x0122a1e4&&a<0x0122a1f4)return a-0x0122a1e4+0x100;
  return a>=0x6000&&a<0x6040?a-0x1000:a;
 }
 void put(unsigned a,unsigned n,unsigned v){a=map(a);for(unsigned i=n;i;--i){ram.at(a+i-1)=v;v>>=8;}}
 unsigned get(unsigned a,unsigned n)const{a=map(a);unsigned v=0;for(unsigned i=0;i<n;++i)v=(v<<8)|ram.at(a+i);return v;}
 static int access(void*o,const m040_bus_request*q,uint32_t*v,m040_bus_detail*d){
  auto&b=*static_cast<Bus*>(o);++b.calls;
#ifdef PHYSICAL_READ_ADMITTED
  if(!b.reentry){need(m040_physical_read_exceptions(0)==M040_BUSY,"read profile cannot change inside an active callback");b.reentry=true;}
#endif
  if(q->address==b.deny_nested){d->completion=M040_BUS_FAULT_BEFORE_TRANSFER;return 1;}
  if(q->kind==b.kind&&q->address==b.denied&&!b.repaired){
   ++b.faults;b.failed=*q;d->completion=M040_BUS_FAULT_BEFORE_TRANSFER;
   if(b.flip_marker)mmu_restart=false; // Evidence must come from before callback.
   d->origin=M040_ORIGIN_EXTERNAL_BUS;d->whole_scalar_no_commit=1;d->has_cpu_physical_address=1;d->cpu_physical_address=q->address;
   if(b.mode==1)d->origin=M040_ORIGIN_UNSPECIFIED;
   if(b.mode==2)d->origin=M040_ORIGIN_ATC_TRANSLATION;
   if(b.mode==3)d->whole_scalar_no_commit=0;
   if(b.mode==4)d->has_cpu_physical_address=0;
   if(b.mode==5){d->completion=M040_BUS_UNKNOWN_FAULT;d->whole_scalar_no_commit=0;}
   if(b.mode==6)return 0;
   return 1;
  }
  if(q->kind==b.kind&&q->address==b.denied)++b.successes;
  if(q->kind==M040_READ&&q->address==0x6000)++b.first_reads;
  if(q->kind==M040_WRITE){b.put(q->address,q->size,*v);if(q->address==0x4000)b.repaired=true;}
  else *v=b.get(q->address,q->size);
  d->completion=M040_BUS_COMPLETED;return 1;
 }
};
void attach(Bus&b,unsigned sr){
 const m040_options options{M040_ABI_VERSION,1,0,0};
 need(m040_reset(&options)==M040_OK&&m040_fault_recovery(1)==M040_OK&&m040_set_detailed_bus(Bus::access,&b)==M040_OK,"attach actual detailed bus/restart core");
#ifdef PHYSICAL_READ_ADMITTED
 need(m040_physical_read_exceptions(1)==M040_OK,"separate certified-read opt-in");
#endif
 regs.regs[15]=regs.isp=0x9000;regs.usp=0x7000;regs.msp=0x8000;regs.sr=sr;MakeFromSR();regs.regs[8]=b.denied;regs.regs[1]=0x11223344;
 b.put(8,4,0x200);unsigned pc=0x200;
 for(unsigned word:{0x13fcu,1u,0u,0x4000u,0x4e73u}){b.put(pc,2,word);pc+=2;}
}
void frame(Bus&b,const Result&r,unsigned sr,unsigned restart,unsigned size,unsigned fc,bool cm=false,unsigned ea=0){
 need(r.exit==Exit::Budget&&r.vector==2&&regs.pc==0x200,"certified read/fetch reaches actual vector2 handler");
 const auto sp=regs.regs[15];const auto expected=(size==1?0x20:size==2?0x40:0)|0x100|fc|(cm?0x1000:0);
 need(b.get(sp,2)==sr&&b.get(sp+2,4)==(bad_pc?restart+2:restart)&&b.get(sp+6,2)==0x7008,"read/fetch frame preserves original SR, restart PC and format7");
 need(b.get(sp+12,2)==(bad_fc?expected^1:expected)&&b.get(sp+20,4)==b.denied,"read/fetch frame has exact size, FC, direction and logical FA");
 need(b.get(sp+8,4)==ea&&b.get(sp+14,2)==0&&b.get(sp+16,2)==0&&b.get(sp+18,2)==0,"read/fetch frame has only actual CM EA and no invented pending writes");
 need(b.faults==1&&b.successes==0&&!b.repaired,"no target access or implicit repair before guest handler");++frames;
}
void operand(unsigned size,unsigned update,unsigned ccr,bool supervisor,unsigned value){
 Bus b;const unsigned sr=(supervisor?0x2500:0x500)|ccr;attach(b,sr);
 const unsigned initial=b.denied+(update==2?size:0);regs.regs[8]=initial;
 b.put(b.denied,size,value);b.put(0x100,2,(size==1?0x1210:size==2?0x3210:0x2210)+8*update);
 const auto r=execute(1);
 if(before){need(r.exit==Exit::MemoryFault&&host.exception_count==0&&b.faults==1,"before certified operand fault is terminal");return;}
 frame(b,r,sr,0x100,size,supervisor?5:1);
 need(regs.regs[8]==(bad_fixup?initial+1:initial)&&regs.regs[1]==0x11223344,"failed read restores only its address fixup and leaves destination unchanged");
 need(execute(2).exit==Exit::Budget&&regs.pc==0x100&&architectural_state().sr==sr&&regs.regs[8]==initial&&b.successes==0,"real repair and RTE restore original read context without target replay");
 need(execute(1).exit==Exit::Budget&&regs.pc==0x102,"returned guest explicitly retries operand instruction");
 const unsigned mask=size==1?255:size==2?65535:~0u,data=value&mask;
 need(regs.regs[1]==((0x11223344&~mask)|data)&&regs.regs[8]==(update==1?b.denied+size:b.denied),"retried MOVE produces exact destination and one address update");
 const unsigned ccr_out=(ccr&16)|(data==0?4:0)|((data>>(size*8-1))?8:0);
 const auto state=architectural_state();need(state.sr==((supervisor?0x2500:0x500)|ccr_out)&&state.usp==0x7000&&state.isp==0x9000&&state.msp==0x8000,"actual retry preserves X and computes NZVC while retaining stack banks");
 need(b.successes==(bad_count?2u:1u)&&b.faults==1,"exactly one failed request and one guest-directed successful read");
}
void fetch(unsigned size,unsigned ccr,bool supervisor,bool opcode){
 Bus b;b.kind=M040_FETCH;b.denied=opcode?0x100:size==1?0x103:0x102;
 const unsigned sr=(supervisor?0x2500:0x500)|ccr;attach(b,sr);
 const unsigned value=0x89abcdef,mask=size==1?255:size==2?65535:~0u;
 b.put(0x100,2,opcode?0x4e71:size==1?0x123c:size==2?0x323c:0x223c);
 if(!opcode)b.put(0x102,size==1?2:size,value);const auto r=execute(1);
 if(before){need(r.exit==Exit::MemoryFault&&host.exception_count==0&&b.faults==1,"before certified fetch fault is terminal");return;}
 frame(b,r,sr,0x100,opcode?2:size,supervisor?6:2);
 need(r.instructions==(opcode?0u:1u)&&regs.regs[1]==0x11223344,"opcode versus extension fetch retains real instruction count and unchanged D1");
 need(execute(2).exit==Exit::Budget&&regs.pc==0x100&&architectural_state().sr==sr&&b.successes==0,"guest RTE does not perform hidden opcode/extension fetch");
 need(execute(1).exit==Exit::Budget&&regs.pc==(opcode?0x102:size==4?0x106:0x104),"explicit fetch retry completes the real instruction once");
 need(regs.regs[1]==(opcode?0x11223344:(0x11223344&~mask)|(value&mask))&&b.successes==(bad_count?2u:1u)&&b.faults==1,"fetch retry has exact data and one successful target fetch");
}
void movem(unsigned ccr,bool supervisor){
 Bus b;b.denied=0x6004;const unsigned sr=(supervisor?0x2500:0x500)|ccr;attach(b,sr);regs.regs[8]=0x6000;
 b.put(0x100,2,0x4cd0);b.put(0x102,2,3);b.put(0x6000,4,0x11112222);b.put(0x6004,4,0x33334444);
 const auto r=execute(1);if(before){need(r.exit==Exit::MemoryFault&&host.exception_count==0,"before certified CM read fault is terminal");return;}
 frame(b,r,sr,0x100,4,supervisor?5:1,true,0x6000);
 need(regs.regs[0]==0x11112222&&regs.regs[1]==0x11223344&&b.first_reads==1,"CM read fault preserves only the completed prefix register");
 need(execute(2).exit==Exit::Budget&&regs.pc==0x100&&mmu040_movem==1&&b.first_reads==1,"actual RTE restores CM continuation without a prefix data read");
 need(execute(1).exit==Exit::Budget&&regs.pc==0x104&&regs.regs[0]==0x11112222&&regs.regs[1]==0x33334444&&architectural_state().sr==sr,"explicit restarted MOVEM consumes stream and finishes remaining results");
 need(b.first_reads==2&&b.faults==1&&b.successes==(bad_count?2u:1u),"CM prefix repeats only in the explicit guest retry, not in RTE");
}
void moves(unsigned size,unsigned ccr,unsigned selected){
 Bus b;const unsigned sr=0x2500|ccr;attach(b,sr);regs.sfc=selected;
 b.put(0x100,2,size==1?0x0e10:size==2?0x0e50:0x0e90);b.put(0x102,2,0x1000);b.put(b.denied,size,0x89abcdef);
 const auto r=execute(1);if(before){need(r.exit==Exit::MemoryFault&&host.exception_count==0,"before certified MOVES read is terminal");return;}
 const unsigned fc=(selected==2||selected==6)?selected-1:selected;frame(b,r,sr,0x100,size,fc);
 need(b.failed.has_alternate_function_code&&b.failed.alternate_function_code==selected&&b.failed.function_code==fc&&!b.failed.transfer_type,"MOVES read retains selected and normalized FC independent of supervisor state");
 need(execute(2).exit==Exit::Budget&&regs.pc==0x100&&execute(1).exit==Exit::Budget&&regs.pc==0x104,"MOVES read returns and explicitly retries its real body");
 const unsigned mask=size==1?255:size==2?65535:~0u;
 need(regs.regs[1]==((0x11223344&~mask)|(0x89abcdef&mask))&&architectural_state().sr==sr&&regs.sfc==selected&&b.faults==1&&b.successes==1,"retried MOVES preserves all flags and SFC while loading exact data once");
}
void peer_reads(){
 // Exact original font-engine fault PC/opcode with a present physical zero.
 // This owned repair fixture does not claim the owner's kernel handler does so.
 Bus null;null.denied=0;attach(null,0);regs.pc=regs.instruction_pc=0x0122a1e4;
 unsigned pc=0x100;for(unsigned w:{0x1210u,0x6700u,0x01eeu,0x5288u}){null.put(pc,2,w);pc+=2;}null.put(0,1,0x80);
 const auto first=execute(1);
 if(before)need(first.exit==Exit::MemoryFault&&first.instruction_pc==0x0122a1e4&&first.address==0&&host.exception_count==0,"before exact user NULL read stays terminal");
 else{
  frame(null,first,0,0x0122a1e4,1,1);
  need(execute(2).exit==Exit::Budget&&regs.pc==0x0122a1e4&&execute(1).exit==Exit::Budget&&regs.pc==0x0122a1e6&&regs.regs[1]==0x11223380&&null.faults==1&&null.successes==1,"present physical zero and exact original font opcode reach guest repair/RTE without host exit");
 }
 Bus probe;probe.denied=0xfffffa44;attach(probe,0x2000);probe.put(0x100,2,0x3238);probe.put(0x102,2,0xfa44);
 const auto second=execute(1);
 if(before)need(second.exit==Exit::MemoryFault&&second.address==0xfffffa44&&host.exception_count==0,"before full32bit supervisor hardware read stays terminal");
 else{
  frame(probe,second,0x2000,0x100,2,5);unsigned next=0x200;
  for(unsigned w:{0x4fefu,60u,0x4ef9u,0u,0x300u}){probe.put(next,2,w);next+=2;}probe.put(0x300,2,0x4e71);
  need(execute(2).exit==Exit::Budget&&regs.pc==0x300&&regs.regs[15]==0x9000&&probe.faults==1&&probe.successes==0,"owned hardware-probe discard handler avoids an implicit peripheral retry");
 }
}
#ifdef PHYSICAL_READ_ADMITTED
void rejection(){
 for(unsigned mode=1;mode<=6;++mode){Bus b;b.mode=mode;attach(b,0x2000);b.put(0x100,2,0x1210);need(execute(1).exit==Exit::MemoryFault&&host.exception_count==0,"unknown/ATC/partial/missing read evidence remains terminal");const auto calls=b.calls;need(execute(1).exit==Exit::Unsupported&&b.calls==calls,"rejected read certificate keeps reset-only latch");}
 for(unsigned sr:{0x6000u,0xa000u,0x3000u}){Bus b;attach(b,sr);b.put(0x100,2,0x1210);need(execute(1).exit==Exit::MemoryFault&&host.exception_count==0,"unqualified trace/master read profile remains terminal");}
 for(unsigned address:{8u,0x8ffcu}){Bus b;b.deny_nested=address;attach(b,0x2000);b.put(0x100,2,0x1210);need(execute(1).exit==Exit::DoubleFault&&host.halted&&!host.physical_writeback,"read-fault stack/vector failure halts without inventing writeback metadata");}
 Bus b;attach(b,0x2000);need(m040_physical_read_exceptions(2)==M040_BAD_ARGUMENT&&m040_physical_read_exceptions(0)==M040_OK,"read profile validates explicit boolean opt-in");b.put(0x100,2,0x1210);need(execute(1).exit==Exit::MemoryFault&&host.exception_count==0,"detailed bus selection alone retains its terminal contract");
 const m040_options options{M040_ABI_VERSION,1,0,0};need(m040_reset(&options)==M040_OK&&!host.physical_read_exceptions&&!host.physical_write_exceptions,"reset clears both independently opt-in physical profiles");
 for(unsigned fc:{0u,3u,4u,7u}){Bus alternate;attach(alternate,0x2000);regs.sfc=fc;alternate.put(0x100,2,0x0e10);alternate.put(0x102,2,0x1000);need(execute(1).exit==Exit::MemoryFault&&host.exception_count==0&&alternate.failed.transfer_type==2,"unqualified alternate CPU spaces cannot borrow ordinary physical read delivery");}
 Bus rte;rte.denied=0x8fd0;attach(rte,0x2000);regs.pc=regs.instruction_pc=0x200;regs.regs[15]=regs.isp=0x8fc4;
 rte.put(0x200,2,0x4e73);rte.put(0x8fc4,2,0x2700);rte.put(0x8fc6,4,0x100);rte.put(0x8fca,2,0x7008);
 need(execute(1).exit==Exit::DoubleFault&&host.halted&&host.exception_count==0&&host.bus_fault&&host.bus_fault->rte_internal_load,"certified internal RTE state-load fault keeps halt priority over read delivery");
 const auto calls=rte.calls;m040_result stopped{};int accepted=1;need(m040_interrupt(31,7,&accepted,&stopped)==M040_OK&&!accepted&&rte.calls==calls,"internal RTE halt rejects NMI without new bus operations");
 Bus snapshot;snapshot.flip_marker=true;attach(snapshot,0x201f);snapshot.put(0x100,2,0x1210);snapshot.put(snapshot.denied,1,0x80);
 const auto read=execute(1);frame(snapshot,read,0x201f,0x100,1,5);
 need(host.bus_fault&&host.bus_fault->restartable_read&&execute(2).exit==Exit::Budget&&regs.pc==0x100&&execute(1).exit==Exit::Budget&&regs.regs[1]==0x11223380,"read restartability is captured from the actual CPU before callback and cannot be replaced by reply state");
 Bus mixed;attach(mixed,0x2000);need(m040_physical_write_exceptions(1)==M040_OK,"read and qualified write profiles are independently selectable together");mixed.put(0x100,2,0x1210);mixed.put(mixed.denied,1,0x80);
 const auto first=execute(1);frame(mixed,first,0x2000,0x100,1,5);
 need(execute(2).exit==Exit::Budget&&execute(1).exit==Exit::Budget&&regs.regs[1]==0x11223380,"read recovery works while the qualified write profile is also enabled");
 mixed.kind=M040_WRITE;mixed.repaired=false;mixed.faults=mixed.successes=0;regs.pc=regs.instruction_pc=0x104;regs.regs[0]=0x12345678;mixed.put(0x104,2,0x2080);
 const auto write=execute(1);const auto sp=regs.regs[15];
 need(write.exit==Exit::Budget&&write.vector==2&&regs.pc==0x200&&mixed.get(sp+2,4)==0x106&&mixed.get(sp+12,2)==5&&mixed.get(sp+18,2)==0x85&&mixed.get(sp+14,2)==0&&mixed.get(sp+16,2)==0&&mixed.get(sp+40,4)==mixed.denied&&mixed.get(sp+44,4)==0x12345678,"read opt-in does not alter the qualified physical WB1 write frame");
 unsigned pc=0x200;for(unsigned w:{0x13fcu,1u,0u,0x4000u,0x202fu,44u,0x206fu,40u,0x2080u,0x426fu,18u,0x4e73u}){mixed.put(pc,2,w);pc+=2;}
 need(execute(6).exit==Exit::Budget&&regs.pc==0x106&&mixed.get(mixed.denied,4)==0x12345678&&mixed.faults==1&&mixed.successes==1&&!host.physical_writeback,"mixed-profile write cleanup/RTE completes once and destroys scoped WB1 state");
}
#endif
}
int main(int argc,char**argv){try{
 for(int i=1;i<argc;++i){std::string_view a=argv[i];if(a=="--before-audit")before=true;else if(a=="--negative-pc")bad_pc=true;else if(a=="--negative-fc")bad_fc=true;else if(a=="--negative-count")bad_count=true;else if(a=="--negative-fixup")bad_fixup=true;else throw std::runtime_error("bad mode");}
 for(unsigned ccr=0;ccr<32;++ccr)for(bool supervisor:{false,true}){
  for(unsigned size:{1u,2u,4u}){for(unsigned update=0;update<3;++update)for(unsigned value:{0u,0x80u,0x89abcdefu})operand(size,update,ccr,supervisor,value);fetch(size,ccr,supervisor,false);}
  fetch(2,ccr,supervisor,true);movem(ccr,supervisor);
 }
 for(unsigned ccr=0;ccr<32;++ccr)for(unsigned size:{1u,2u,4u})for(unsigned selected:{1u,2u,5u,6u})moves(size,ccr,selected);
 peer_reads();
#ifdef PHYSICAL_READ_ADMITTED
 if(!before)rejection();
#endif
 std::cout<<"PASS: "<<checks<<" certified physical read/fetch assertions; actual-frames="<<frames<<(before?" before-only":"")<<'\n';
}catch(const std::exception&e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}}
