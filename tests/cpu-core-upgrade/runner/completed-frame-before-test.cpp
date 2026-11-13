/* SPDX-License-Identifier: GPL-2.0-or-later
 * Before-only observations for completed-frame IRQ provenance design.
 * Actual instruction faults, frames, software cleanup and RTE; no proposed
 * production record algorithm is implemented or treated as an oracle here.
 */
#include "c-api.h"
#include "restart.hpp"
#include <iostream>
#include <string_view>
using namespace modern040;
namespace {
unsigned checks=0,frames=0,rtes=0;bool expect_immediate=false;
void need(bool ok,const char*why){++checks;if(!ok)throw std::runtime_error(why);}
Result step(unsigned budget=1){m040_result out{};need(m040_run(budget,&out)==M040_OK,"public run boundary");return {Exit(out.exit),out.instructions,int(out.vector),out.instruction_pc,out.current_pc,out.address,out.size,out.detail};}
struct Bus {
 struct Event{m040_bus_request q;unsigned value;bool completed;};
 std::array<unsigned char,65536> ram{};std::vector<Event> events;
 unsigned deny=~0u,denied_kind=M040_WRITE,failures=0,external_writes=0,second_deny=~0u;
 unsigned map(unsigned a)const{return a>=0xff000000?a&0x00ffffff:a;}
 void put(unsigned a,unsigned n,unsigned value){a=map(a);for(unsigned i=n;i;--i){ram.at(a+i-1)=value;value>>=8;}}
 unsigned get(unsigned a,unsigned n)const{a=map(a);unsigned value=0;for(unsigned i=0;i<n;++i)value=(value<<8)|ram.at(a+i);return value;}
 static int access(void*opaque,const m040_bus_request*q,uint32_t*v,m040_bus_detail*d){
  auto&b=*static_cast<Bus*>(opaque);const auto input=*v;
  if((q->address==b.deny&&q->kind==b.denied_kind)||q->address==b.second_deny){
   if(q->address==b.deny)b.deny=~0u;else b.second_deny=~0u;
   ++b.failures;b.events.push_back({*q,input,false});
   d->completion=M040_BUS_FAULT_BEFORE_TRANSFER;d->origin=M040_ORIGIN_EXTERNAL_BUS;
   d->whole_scalar_no_commit=1;d->has_cpu_physical_address=1;d->cpu_physical_address=q->address;return 1;
  }
  if(q->kind==M040_WRITE)b.put(q->address,q->size,*v);else *v=b.get(q->address,q->size);
  b.events.push_back({*q,q->kind==M040_WRITE?input:*v,true});d->completion=M040_BUS_COMPLETED;return 1;
 }
 static int precise(void*opaque,const m040_bus_request*q,uint32_t*v){m040_bus_detail detail{M040_BUS_DETAIL_VERSION,0,0,0,0,0};access(opaque,q,v,&detail);return detail.completion;}
 unsigned writes(unsigned a,unsigned n)const{unsigned count=0;for(const auto&e:events)if(e.completed&&e.q.kind==M040_WRITE&&e.q.address<a+n&&a<e.q.address+e.q.size)++count;return count;}
};
struct Origin{unsigned sp,pc,sr,ssw;};
void emit(Bus&b,unsigned&pc,unsigned word){b.put(pc,2,word);pc+=2;}
void jump(Bus&b,unsigned pc,unsigned target){emit(b,pc,0x4ef9);emit(b,pc,target>>16);emit(b,pc,target&65535);}
void cleanup(Bus&b,unsigned pc=0x200,bool wb1=true){
 // Save D0/A0 on the real supervisor stack, complete physical WB1 and RTE.
 for(unsigned word:{0x48e7u,0x8080u,0x206fu,wb1?48u:32u,0x202fu,wb1?52u:36u,0x2080u,0x426fu,wb1?26u:22u,0x4cdfu,0x0101u,0x4e73u})emit(b,pc,word);
}
void setup(Bus&b){
 const m040_options options{M040_ABI_VERSION,1,0,0};
 need(m040_reset(&options)==M040_OK&&m040_fault_recovery(1)==M040_OK&&m040_set_detailed_bus(Bus::access,&b)==M040_OK&&m040_physical_write_exceptions(1)==M040_OK&&m040_physical_read_exceptions(1)==M040_OK,"attach qualified physical profiles");
 need(m040_private_operations(1)==M040_OK&&m040_legacy_fpu_operations(1)==M040_OK,"enable genuine private/FPU service boundaries");
 regs.usp=0x7000;regs.isp=regs.regs[15]=0x9000;regs.msp=0x8000;regs.sr=0x2500;MakeFromSR();
 regs.regs[0]=0x12345678;regs.regs[8]=0x5000;
 b.put(8,4,0x200);b.put(69*4,4,0x800);b.put(0x800,2,0x4e73);cleanup(b);
}
Origin fault(Bus&b,unsigned pc=0x100,unsigned target=0x5000,bool read=false){
 regs.pc=pc;regs.regs[8]=target;b.put(pc,2,read?0x2210:0x2080);b.put(pc+2,2,0x2048);
 b.deny=target;b.denied_kind=read?M040_READ:M040_WRITE;
 const auto result=step();need(result.exit==Exit::Budget&&result.vector==2&&regs.pc==0x200,"actual operand fault completes frame/vector delivery");
 need(host.last_frame&&host.last_frame->format==7&&host.bus_fault,"successful frame retains original producer evidence");
 const auto sp=regs.regs[15];Origin origin{sp,b.get(sp+2,4),b.get(sp,2),b.get(sp+12,2)};
 need(origin.pc==(read?pc:pc+2)&&host.bus_fault->completed_producer==!read,"CPU completion distinguishes identical transfer direction from unfinished state");
 need(b.get(sp+6,2)==0x7008&&origin.ssw==(read?0x105u:5u),"actual external fault header and status");++frames;return origin;
}
void run_to(unsigned pc){for(unsigned i=0;regs.pc!=pc&&i<100;++i)need(step().exit==Exit::Budget,"genuine guest cleanup/control/RTE executes");need(regs.pc==pc,"bounded guest continuation reaches requested PC");}
void held(Bus&b,unsigned pc,const char*why){
 const auto requests=b.events.size();int pending=0,accepted=0;m040_result result{};
 need(m040_restart_pending(&pending)==M040_OK&&pending,"before core retains actual format7 RTE restart flag");
 need(m040_interrupt(69,6,&accepted,&result)==M040_OK&&!accepted&&regs.pc==pc&&b.events.size()==requests,why);++rtes;
}
void ordinary(){
 Bus b;setup(b);const auto origin=fault(b);run_to(origin.pc);
 if(expect_immediate){int accepted=0;m040_result result{};need(m040_interrupt(69,6,&accepted,&result)==M040_OK&&accepted,"completed write RTE admits IRQ before following instruction");return;}
 held(b,origin.pc,"before ordinary completed write delays IRQ without issuing an extra bus request");
 need(step().exit==Exit::Budget&&regs.pc==origin.pc+2,"existing holdoff is consumed only by the following instruction");
 int accepted=0;m040_result result{};need(m040_interrupt(69,6,&accepted,&result)==M040_OK&&accepted&&regs.pc==0x800,"IRQ becomes eligible after unwanted following instruction");
 need(step().exit==Exit::Budget&&regs.pc==origin.pc+2,"real interrupt RTE returns after the unwanted instruction");
}
void header_overlap(){
 Bus b;setup(b);const auto origin=fault(b);unsigned pc=0x300;
 for(unsigned value:{0xabcu,origin.pc})for(unsigned word:{0x2f7cu,0u,value,2u})emit(b,pc,word);
 jump(b,pc,0x200);regs.pc=0x300;const auto writes=b.writes(origin.sp,8);run_to(origin.pc);
 need(b.get(origin.sp+2,4)==origin.pc&&b.writes(origin.sp,8)==writes+2,"header changes then restores the same raw signature through actual CPU writes");
 held(b,origin.pc,"overwritten exact-SP/header signature must not acquire completion by equality alone");
}
void header_alias(){
 Bus b;setup(b);const auto origin=fault(b);unsigned pc=0x300;
 for(unsigned word:{0x207cu,0xff00u,origin.sp+2,0x203cu,0u,origin.pc,0x2080u})emit(b,pc,word);
 jump(b,pc,0x200);regs.pc=0x300;const auto old_writes=b.writes(origin.sp+2,4);
 run_to(origin.pc);bool alias_seen=false;
 for(const auto&e:b.events)if(e.q.kind==M040_WRITE&&e.q.address==0xff000000+origin.sp+2&&e.completed)alias_seen=true;
 need(alias_seen&&b.get(origin.sp+2,4)==origin.pc&&b.writes(origin.sp+2,4)==old_writes,"full32 CPU alias changes the same frame backing without intersecting its numeric stack address");
 held(b,origin.pc,"frame identity coherence needs explicit backend storage aliasing, not guessed CPU physical bits");
}
void saved_sr_control(){
 Bus b;setup(b);const auto origin=fault(b);unsigned pc=0x300;
 for(unsigned word:{0x0257u,0xf8ffu,0x0057u,0x0500u})emit(b,pc,word);
 jump(b,pc,0x200);regs.pc=0x300;const auto writes=b.writes(origin.sp,2);run_to(origin.pc);
 need(b.writes(origin.sp,2)==writes+2&&b.get(origin.sp+2,4)==origin.pc,"real MFP handler-style saved-SR IPL edits leave completed PC intact");
 held(b,origin.pc,"before core also delays ordinary IRQ after legitimate saved-SR updates");
}
void discard_and_reuse(){
 Bus b;setup(b);const auto old=fault(b);unsigned pc=0x300;
 for(unsigned word:{0x4fefu,60u,0x4ef9u,0u,0x400u})emit(b,pc,word);
 b.put(0x400,2,0x2210);regs.regs[8]=0x5004;b.deny=0x5004;b.denied_kind=M040_READ;regs.pc=0x300;
 need(step(2).exit==Exit::Budget&&regs.pc==0x400&&regs.regs[15]==0x9000,"guest discards old frame without RTE");
 const auto writes=b.writes(old.sp,8);need(step().vector==2&&regs.pc==0x200,"new unfinished read constructs another frame");
 need(regs.regs[15]==old.sp&&b.get(old.sp+12,2)==0x105&&b.get(old.sp+2,4)==0x400&&b.writes(old.sp,8)>writes,"same SP is reused by an unfinished frame with observed header replacement");
 b.put(0x200,2,0x4e73);need(step().exit==Exit::Budget&&regs.pc==0x400,"genuine unfinished read RTE");
 held(b,0x400,"stale global or SP-only completion cannot release IRQ for reused unfinished frame");
 need(step().exit==Exit::Budget&&regs.pc==0x402,"explicit unfinished read retry completes");
}
void identical_header_reuse(){
 Bus b;setup(b);const auto old=fault(b);unsigned pc=0x300;
 for(unsigned word:{0x4fefu,60u,0x207cu,0u,0x4000u,0x4ef9u,0u,old.pc})emit(b,pc,word);
 b.put(old.pc,2,0x4e90);b.put(0x4000,2,0x4e71);b.deny=0x8ffc;b.denied_kind=M040_WRITE;
 need(m040_set_precise_bus(Bus::precise,&b)==M040_OK,"explicit legacy backend permits the independently audited unfinished JSR profile");
 regs.pc=0x300;need(step(3).exit==Exit::Budget&&regs.pc==old.pc,"guest discards frame then enters unfinished JSR at prior completed PC");
 need(step().vector==2&&regs.pc==0x200&&mmu_restart,"actual JSR stack fault retains unfinished CPU state");
 need(regs.regs[15]==old.sp&&b.get(old.sp+2,4)==old.pc&&b.get(old.sp,2)==old.sr&&b.get(old.sp+12,2)==old.ssw,"SP plus complete SR/PC/format/SSW signature collides with prior completed frame");
 need(b.get(old.sp+14,2)==0x85&&b.get(old.sp+18,2)==0,"WB stage differs despite identical header and write SSW");
 cleanup(b,0x200,false);run_to(old.pc);held(b,old.pc,"SP/header/writeSSW reuse cannot prove unfinished JSR completion");
 need(step().exit==Exit::Budget&&regs.pc==0x4000,"only guest-directed JSR retry commits control transfer");
}
void nested(bool unfinished){
 Bus b;setup(b);const auto outer=fault(b);const auto inner=fault(b,0x300,0x5004,unfinished);
 need(inner.sp+60==outer.sp&&inner.pc==(unfinished?0x300u:0x302u),"nested fault frames are independent actual stack objects");
 if(unfinished)b.put(0x200,2,0x4e73);run_to(inner.pc);held(b,inner.pc,"before nested RTE retains its own restart holdoff");
 if(unfinished)need(step().exit==Exit::Budget&&regs.pc==0x302,"only explicit inner read retry completes unfinished instruction");
 cleanup(b);jump(b,0x302,0x200);run_to(outer.pc);held(b,outer.pc,"inner return must not erase outer completed-frame identity");
}
void copy_frame(Bus&b,unsigned old_sp,unsigned new_sp,unsigned code,bool use_copy){
 unsigned pc=code;
 for(unsigned word:{0x207cu,old_sp>>16,old_sp&65535,0x227cu,new_sp>>16,new_sp&65535,0x720eu})emit(b,pc,word);
 const unsigned loop=pc;emit(b,pc,0x22d8);emit(b,pc,0x51c9);const unsigned displacement=loop-pc;emit(b,pc,displacement&65535);
 if(use_copy)for(unsigned word:{0x2e7cu,new_sp>>16,new_sp&65535})emit(b,pc,word);
 jump(b,pc,0x200);regs.pc=code;
}
void relocated(bool copied_then_original){
 Bus b;setup(b);const auto origin=fault(b);copy_frame(b,origin.sp,0x7e00,0x300,!copied_then_original);
 run_to(origin.pc);need(b.get(0x7e02,4)==origin.pc&&b.get(0x7e06,2)==0x7008,"real guest longword loop copies the frame without a host memory copy");
 held(b,origin.pc,"relocated or copied frames require conservative identity treatment");
 if(copied_then_original){
  // Consume the original with genuine RTE first, then attempt its copied image.
  regs.pc=0x400;regs.regs[15]=regs.isp=0x7e00;b.put(0x400,2,0x4e73);
  need(step().exit==Exit::Budget&&regs.pc==origin.pc,"copied image can be restored by another genuine RTE");
  held(b,origin.pc,"copied stale image cannot borrow a previously consumed completion");
 }
}
void ambiguous_copy(){
 Bus b;setup(b);const auto outer=fault(b);const auto inner=fault(b);
 for(unsigned i=0;i<60;++i)need(b.ram[outer.sp+i]==b.ram[inner.sp+i],"two independent completed frames can have identical full architectural bytes");
 need(outer.sp!=inner.sp,"identical byte signatures belong to different live frames");
 copy_frame(b,inner.sp,0x7e00,0x300,true);run_to(inner.pc);held(b,inner.pc,"copied bytes cannot choose between independent live completion identities");
}
void consume_once(){
 Bus b;setup(b);const auto origin=fault(b);run_to(origin.pc);held(b,origin.pc,"first real RTE is the only possible consume point");
 unsigned pc=origin.pc;for(unsigned word:{0x4fefu,0xffc4u,0x4ef9u,0u,0x400u})emit(b,pc,word);b.put(0x400,2,0x4e73);
 need(step(2).exit==Exit::Budget&&regs.pc==0x400&&regs.regs[15]==origin.sp,"guest returns to previously popped stack bytes without constructing another fault");
 need(step().exit==Exit::Budget&&regs.pc==origin.pc,"same old frame bytes support a second genuine RTE");held(b,origin.pc,"already consumed frame identity cannot be reused by another RTE");
}
void external_write(){
 Bus b;setup(b);const auto origin=fault(b);const auto events=b.events.size();
 b.put(origin.sp+2,4,0xabc);b.put(origin.sp+2,4,origin.pc);b.external_writes+=2;
 need(b.events.size()==events&&b.get(origin.sp+2,4)==origin.pc,"backing-RAM header changes can restore identical bytes without any modern bus event");
 run_to(origin.pc);held(b,origin.pc,"native or external stores require separate coherence rather than a core-write-only observer");
}
void failed_construction(){
 for(unsigned address:{8u,0x8ffcu}){
  Bus b;setup(b);b.second_deny=address;b.deny=0x5000;regs.pc=0x100;b.put(0x100,2,0x2080);
  need(step().exit==Exit::DoubleFault&&host.halted&&host.exception_count==0&&!host.last_frame&&b.failures==2,"failed frame/vector construction cannot register a successfully delivered completed frame");
 }
}
void failed_rte(){
 for(unsigned offset:{0u,2u,6u,12u}){
  Bus b;setup(b);const auto origin=fault(b);regs.pc=0x400;b.put(0x400,2,0x4e73);b.deny=origin.sp+offset;b.denied_kind=M040_READ;
  const auto requests=b.events.size();const auto result=step();
  if(offset==12){
   need(result.exit==Exit::DoubleFault&&host.halted&&regs.pc==0x400,"failed internal RTE state load halts before a successful consume point");
   int accepted=0;m040_result out{};need(m040_interrupt(69,7,&accepted,&out)==M040_OK&&!accepted,"failed internal RTE cannot release IRQ or NMI");
  }else need(result.exit==Exit::Budget&&result.vector==2&&host.last_frame->stack<origin.sp,"partial header RTE faults into an independent unfinished frame");
  need(b.events.size()>requests,"actual RTE bus failure is observed");
 }
}
void services(){
 for(unsigned opcode:{0x7300u,0x7301u,0x7101u,0xf200u}){
  Bus b;setup(b);const auto origin=fault(b);b.put(0x400,2,opcode);b.put(0x402,2,0);jump(b,opcode==0xf200?0x404:0x402,0x200);regs.pc=0x400;
  const auto requests=b.events.size();need(step().exit==Exit::HostService&&host.service,"real private or legacy FPU body suspends for external service");
  const auto state=host.service->kind;
  // A host service can touch backing RAM without a modern CPU bus request.
  b.put(origin.sp+2,4,origin.pc);++b.external_writes;
  if(state==ServiceKind::LegacyFpu){m040_state imported{};need(m040_get_state(&imported)==M040_OK,"read suspended FPU state");imported.pc=0x404;need(m040_set_state(&imported)==M040_OK,"external original-FPU completion imports its resulting PC");}
  m040_result result{};need(m040_complete_service(&result)==M040_OK&&result.exit==M040_BUDGET,"genuine private body completes once");
  need(b.events.size()>=requests&&b.external_writes==1,"external backing write is separate from CPU bus ledger");run_to(origin.pc);
  held(b,origin.pc,"unobserved host-service writes cannot leave trusted completed-frame identity");
 }
}
void exhaustion(){
 Bus b;setup(b);std::vector<Origin> origins;
 for(unsigned i=0;i<40;++i){const unsigned pc=0x1000+8*i;origins.push_back(fault(b,pc,0x5000+4*i));jump(b,pc+2,0x200);}
 need(origins.back().sp==0x9000-40*60,"forty actual nested frames exceed proposed bounded record capacity");
 for(auto i=origins.rbegin();i!=origins.rend();++i){run_to(i->pc);held(b,i->pc,"untracked or exhausted frame state must conservatively retain holdoff");}
}
}
int main(int argc,char**argv){try{
 for(int i=1;i<argc;++i){const std::string_view arg=argv[i];if(arg=="--expect-immediate")expect_immediate=true;else throw std::runtime_error("bad mode");}
 ordinary();if(!expect_immediate){header_overlap();header_alias();saved_sr_control();discard_and_reuse();identical_header_reuse();nested(false);nested(true);relocated(false);relocated(true);ambiguous_copy();consume_once();external_write();failed_construction();failed_rte();services();exhaustion();}
 std::cout<<"PASS: "<<checks<<" before-only completed-frame observations; actual-frames="<<frames<<" observed-rtes="<<rtes<<"\n";
}catch(const std::exception&e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}}
