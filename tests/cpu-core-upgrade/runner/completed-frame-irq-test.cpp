/* SPDX-License-Identifier: GPL-2.0-or-later
 * Actual guest RTE/IRQ outcomes, reusing the frozen before observations.
 * No record-table shape or implementation identity is used as an oracle.
 */
#include "c-api.h"
#include <stdexcept>
namespace {
bool wrong_alias=false,omit_notify=false,wrong_reads=false,profile_off=false;
uint64_t zero_key_base=0;bool unknown_frame_mapping=false;
int storage_identity(void*,const m040_bus_request*q,uint64_t*key){
 if(m040_completed_frame_irq(0)!=M040_BUSY)throw std::runtime_error("storage callback may not reenter profile control");
 uint64_t address=q->address;
 if(address>=0xff000000&&!wrong_alias)address&=0x00ffffff;
 if(address+q->size>65536&&!wrong_alias)return M040_FRAME_UNKNOWN;
 if(unknown_frame_mapping&&q->size==60)return M040_FRAME_UNKNOWN;
 if(zero_key_base){if(address<zero_key_base){if(address+q->size>zero_key_base)return M040_FRAME_UNKNOWN;address+=0x100000000ULL;}else address-=zero_key_base;}
 *key=address;return M040_FRAME_RAM;
}
int attach_completed_profile(int enabled){
 const auto status=m040_legacy_fpu_operations(enabled);if(status!=M040_OK)return status;
 if(profile_off)return status;
 if(m040_set_frame_storage(storage_identity,nullptr)!=M040_OK)return M040_BAD_ARGUMENT;
 return m040_completed_frame_irq(1);
}
}
#define main baseline_observations_main
#define m040_legacy_fpu_operations attach_completed_profile
#include "completed-frame-before-test.cpp"
#undef m040_legacy_fpu_operations
#undef main
namespace {
unsigned immediate=0;
void irq_now(Bus&b,unsigned pc){
 int pending=1,accepted=0,event=0;m040_result result{};m040_state before{},after{};
 need(m040_completed_rte_event(&event)==M040_OK&&event,"native dispatch can observe actual completed RTE with raw restart holdoff intact");
 need(m040_get_state(&before)==M040_OK,"export completed context before immediate IRQ");
 need(m040_restart_pending(&pending)==M040_OK&&!pending,"qualified completed-frame RTE permits IRQ without entering next instruction");
 need(m040_native_restart_pending(&pending)==M040_OK&&pending,"qualified completed-frame RTE retains raw native successor holdoff");
 need(m040_run(0,&result)==M040_OK&&m040_restart_pending(&pending)==M040_OK&&!pending,"zero budget does not consume IRQ eligibility");
 need(m040_native_restart_pending(&pending)==M040_OK&&pending,"zero budget does not release raw native successor holdoff");
 need(m040_interrupt(69,6,&accepted,&result)==M040_OK&&accepted&&regs.pc==0x800,"completed-frame IRQ enters before unexecuted next PC");
 need(b.get(regs.regs[15]+2,4)==pc,"IRQ frame saves exactly the unexecuted next PC");
 need(step().exit==Exit::Budget&&regs.pc==pc,"actual IRQ RTE restores unexecuted continuation");
 need(m040_completed_rte_event(&event)==M040_OK&&!event,"ordinary IRQ return does not reuse the previous completed-RTE event");
 need(m040_get_state(&after)==M040_OK&&after.sr==before.sr&&after.usp==before.usp&&after.isp==before.isp&&after.msp==before.msp,"IRQ/RTE restores SR and all stack banks");
 for(unsigned i=0;i<16;++i)need(before.regs[i]==after.regs[i],"immediate IRQ preserves each architectural register");++immediate;
}
void ordinary_after(bool saved_sr){
 Bus b;setup(b);const auto origin=fault(b);
 if(saved_sr){unsigned pc=0x300;for(unsigned word:{0x0257u,0xf8ffu,0x0057u,0x0500u})emit(b,pc,word);jump(b,pc,0x200);regs.pc=0x300;}
 run_to(0x216);const auto requests=b.events.size();need(step().exit==Exit::Budget&&regs.pc==origin.pc,"genuine cleanup RTE completes");
 need(b.events.size()==requests+(wrong_reads?6u:5u),"RTE uses only its original fetch/SR/PC/format/SSW bus requests");
 irq_now(b,origin.pc);
}
void flags_and_banks(){
 for(bool precise:{false,true})for(unsigned mode:{0u,0x2000u,0x3000u})for(unsigned ccr=0;ccr<32;++ccr){
  if(!precise&&mode==0x3000)continue; // The independently qualified WB1 scalar profile excludes M.
  Bus b;setup(b);
  if(precise){need(m040_set_precise_bus(Bus::precise,&b)==M040_OK&&attach_completed_profile(1)==M040_OK,"legacy bounded profile explicitly reattaches storage identity after backend replacement");cleanup(b,0x200,false);}
  m040_state imported{};need(m040_get_state(&imported)==M040_OK,"read original canonical banks");imported.sr=mode|0x500|ccr;imported.regs[15]=mode&0x2000?(mode&0x1000?imported.msp:imported.isp):imported.usp;
  need(m040_set_state(&imported)==M040_OK,"import user/ISP/MSP state with every original CCR combination");
  regs.pc=0x100;regs.regs[8]=0x5000;b.put(0x100,2,0x2080);b.put(0x102,2,0x2048);b.deny=0x5000;b.denied_kind=M040_WRITE;
  const auto result=step();need(result.exit==Exit::Budget&&result.vector==2&&regs.pc==0x200&&host.bus_fault&&host.bus_fault->completed_producer,"actual banked completed write reaches its real handler");
  const unsigned sp=regs.regs[15];const Origin origin{sp,b.get(sp+2,4),b.get(sp,2),b.get(sp+12,2)};++frames;
  need(origin.pc==0x102&&origin.ssw==(mode&0x2000?5u:1u),"frame retains actual user or supervisor transfer space");
  need(origin.sr==(mode|0x500|(ccr&16)),"actual completed MOVE flags, including preserved X, reach the frame");
  run_to(origin.pc);irq_now(b,origin.pc);
 }
}
void nested_after(bool unfinished){
 Bus b;setup(b);const auto outer=fault(b);const auto inner=fault(b,0x300,0x5004,unfinished);
 if(unfinished)b.put(0x200,2,0x4e73);run_to(inner.pc);
 if(unfinished){held(b,inner.pc,"unfinished nested read retains restart holdoff despite outer completed frame");need(step().exit==Exit::Budget&&regs.pc==0x302,"explicit inner read retry completes");}
 else irq_now(b,inner.pc);
 cleanup(b);jump(b,0x302,0x200);run_to(outer.pc);irq_now(b,outer.pc);
}
void consumed_after(){
 Bus b;setup(b);const auto origin=fault(b);run_to(origin.pc);irq_now(b,origin.pc);
 unsigned pc=origin.pc;for(unsigned word:{0x4fefu,0xffc4u,0x4ef9u,0u,0x400u})emit(b,pc,word);b.put(0x400,2,0x4e73);
 need(step(3).exit==Exit::Budget&&regs.pc==origin.pc,"guest executes another genuine RTE on previously consumed bytes");
 held(b,origin.pc,"a successful completed-frame identity is consumed only once");
}
void notify_after(bool alias){
 Bus b;setup(b);const auto origin=fault(b);const unsigned address=(alias?0xff000000u:0u)+origin.sp+2;
 const m040_bus_request request{M040_ABI_VERSION,M040_WRITE,address,4,5,0,0,0};
 if(!omit_notify)need(m040_frame_write_notify(&request)==M040_OK,"external/native writer notifies before touching RAM");
 b.put(address,4,0xabc);b.put(address,4,origin.pc);run_to(origin.pc);
 held(b,origin.pc,"notified external or native alias stores invalidate even when final bytes match");
}
void stable_backend_reuse(){
 Bus b;setup(b);need(m040_set_precise_bus(Bus::precise,&b)==M040_OK&&attach_completed_profile(1)==M040_OK,"use one stable legacy backend throughout completed and unfinished writes");cleanup(b,0x200,false);
 const auto origin=fault(b);unsigned pc=0x300;for(unsigned word:{0x4fefu,60u,0x207cu,0u,0x4000u,0x4ef9u,0u,origin.pc})emit(b,pc,word);
 b.put(origin.pc,2,0x4e90);b.put(0x4000,2,0x4e71);b.deny=0x8ffc;b.denied_kind=M040_WRITE;regs.pc=0x300;
 need(step(3).exit==Exit::Budget&&step().vector==2&&regs.pc==0x200,"same backend constructs an unfinished JSR frame at the discarded completed frame address");
 need(regs.regs[15]==origin.sp&&b.get(origin.sp+2,4)==origin.pc&&b.get(origin.sp,2)==origin.sr&&b.get(origin.sp+12,2)==origin.ssw,"identical header and SP reuse is observed without a backend replacement barrier");
 run_to(origin.pc);held(b,origin.pc,"actual construction writes invalidate stale completion before unfinished JSR RTE");
}
void storage_contracts(){
 {
  Bus b;setup(b);zero_key_base=0x8fc4;const auto origin=fault(b);run_to(origin.pc);irq_now(b,origin.pc);zero_key_base=0;
 }
 {
  Bus b;setup(b);unknown_frame_mapping=true;const auto origin=fault(b);run_to(origin.pc);held(b,origin.pc,"unknown whole-frame storage identity cannot register completion");unknown_frame_mapping=false;
 }
}
void failed_header_store(){
 Bus b;setup(b);const auto origin=fault(b);jump(b,0x216,0x300);run_to(0x300);
 need(b.get(0x5000,4)==0x12345678,"original writeback completes before the overlapping failure");
 unsigned pc=0x300;for(unsigned word:{0x2f7cu,0u,origin.pc,2u})emit(b,pc,word);b.put(8,4,0x500);b.deny=origin.sp+2;b.denied_kind=M040_WRITE;
 need(step().vector==2&&regs.pc==0x500&&b.get(origin.sp+2,4)==origin.pc,"overlapping header write fails before transfer without changing its bytes");
 pc=0x500;for(unsigned word:{0x4fefu,60u,0x4ef9u,0u,0x400u})emit(b,pc,word);b.put(0x400,2,0x4e73);
 need(step(3).exit==Exit::Budget&&regs.pc==origin.pc,"guest discards the nested failed write and genuinely restores original frame");
 held(b,origin.pc,"even a before-transfer failed header store invalidates the old construction identity");
}
void repair_header_rte(){
 for(unsigned offset:{0u,2u,6u}){
  Bus b;setup(b);const auto origin=fault(b);regs.pc=0x400;b.put(0x400,2,0x4e73);b.deny=origin.sp+offset;b.denied_kind=M040_READ;
  need(step().vector==2&&regs.regs[15]+60==origin.sp,"partial RTE header failure creates independent unfinished frame");
  unsigned pc=0x200;for(unsigned word:{0x4fefu,60u,0x4ef9u,0u,0x400u})emit(b,pc,word);
  need(step(2).exit==Exit::Budget&&regs.pc==0x400&&regs.regs[15]==origin.sp,"guest discards failed-RTE wrapper without consuming original completion");
  need(step().exit==Exit::Budget&&regs.pc==origin.pc,"only later successful genuine RTE restores original completion");irq_now(b,origin.pc);
 }
}
void controls(){
 Bus b;setup(b);const auto origin=fault(b);need(m040_frame_barrier()==M040_OK,"explicit external memory barrier invalidates frame identities");run_to(origin.pc);held(b,origin.pc,"barrier leaves conservative restart holdoff");
 setup(b);fault(b);need(m040_completed_frame_irq(0)==M040_OK,"profile can remain disabled");run_to(0x102);held(b,0x102,"default-off profile retains previous ordinary IRQ behavior");
 const m040_options options{M040_ABI_VERSION,1,0,0};need(m040_reset(&options)==M040_OK&&m040_completed_frame_irq(1)==M040_BAD_ARGUMENT,"reset removes the required storage-identity contract");
}
void eligibility_lifetime(){
 Bus b;setup(b);const auto origin=fault(b);run_to(origin.pc);int pending=1;m040_result result{};
 need(m040_restart_pending(&pending)==M040_OK&&!pending&&m040_native_restart_pending(&pending)==M040_OK&&pending,"completed return separates architectural IRQ eligibility and native holdoff");
 b.put(origin.pc,2,0x4e71);
 need(step().exit==Exit::Budget&&regs.pc==origin.pc+2,"a genuine successor instruction consumes the retirement boundary");
 need(m040_native_restart_pending(&pending)==M040_OK&&!pending,"actual next instruction releases raw native holdoff");
 need(m040_run(0,&result)==M040_OK,"zero-budget observation after successor succeeds");
 b.put(origin.pc+2,2,0x4e73);regs.regs[15]=origin.sp;
 need(step().exit==Exit::Budget&&regs.pc==origin.pc,"a later RTE can restore old frame bytes");
 held(b,origin.pc,"IRQ eligibility expires at the next actual instruction and cannot bless a later untracked RTE");
}
}
int main(int argc,char**argv){try{
 for(int i=1;i<argc;++i){const std::string_view arg=argv[i];if(arg=="--negative-alias")wrong_alias=true;else if(arg=="--negative-notify")omit_notify=true;else if(arg=="--negative-reads")wrong_reads=true;else if(arg=="--profile-off")profile_off=true;else throw std::runtime_error("bad mode");}
 ordinary_after(false);ordinary_after(true);flags_and_banks();header_overlap();header_alias();discard_and_reuse();identical_header_reuse();stable_backend_reuse();nested_after(false);nested_after(true);relocated(false);ambiguous_copy();consumed_after();notify_after(false);notify_after(true);storage_contracts();failed_header_store();repair_header_rte();failed_construction();failed_rte();services();exhaustion();controls();eligibility_lifetime();
 std::cout<<"PASS: "<<checks<<" exact-frame IRQ assertions; actual-frames="<<frames<<" immediate-IRQs="<<immediate<<" conservative-RTEs="<<rtes<<"\n";
}catch(const std::exception&e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}}
