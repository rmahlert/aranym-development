/* SPDX-License-Identifier: GPL-2.0-or-later
 * Actual selector/SCC sources, owned Serial input and genuine core frames.
 * The other source requests are scripted here; connected VBL/MFP is separate.
 */
#include "c-api.h"
#include "sysdeps.h"
#include "parameters.h"
#include "scc.h"
#include <array>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string_view>
#include <vector>
struct HostRegisters{uae_u32 spcflags=0;int stopped=0;}regs;
#include "spcflags.h"
BXOptions bx_options{};
std::array<uint8,65536> fallback_ram{};
uint8 *owned_fallback(memptr address){return fallback_ram.data()+(address&65535);}
[[noreturn]]void panicbug(const char*,...){throw std::runtime_error("unexpected source panic");}
unsigned checks=0,total_irqs=0,total_scc_reads=0,reentry_checks=0;
bool bad_priority=false,bad_count=false,bad_ack=false,bad_bank=false,bad_stop=false,bad_tie=false,bad_pure=false,bad_restart=false,bad_trace=false,profile_off=false;
void need(bool ok,const char*why){++checks;if(!ok)throw std::runtime_error(why);}
#include "irq-priority-triggers.inc"
struct OwnedSerial final:Serial{
 std::vector<uint8>input;
 uint16 getStatus(int&count)override{count=int(input.size());return input.empty()?0:uint16((SCCB_RCV_INT<<8)|SCC_RCA);}
 uint8 getData()override{need(!input.empty(),"owned SCC input is present");auto value=input.front();input.erase(input.begin());++total_scc_reads;return value;}
};
struct Fixture;
Fixture*active=nullptr;
struct Fixture{
 std::array<uint8,65536>ram{};SCC scc{0x00ff8c81,8};OwnedSerial*serial=nullptr;
 unsigned bus_ops=0,mfp_acks=0,scc_acks=0,reads=0,writes=0;
 bool spurious_mfp=false,deny_reads=false,deny_writes=false;std::vector<unsigned>delivered;
 Fixture(){active=this;delete scc.serial;scc.serial=serial=new OwnedSerial;}
 ~Fixture(){active=nullptr;}
 void put(unsigned address,unsigned size,unsigned value){for(unsigned i=0;i<size;++i)ram.at(address+i)=uint8(value>>(8*(size-1-i)));}
 unsigned get(unsigned address,unsigned size){unsigned value=0;for(unsigned i=0;i<size;++i)value=(value<<8)|ram.at(address+i);return value;}
 static int bus(void*opaque,const m040_bus_request*q,uint32_t*value){
  auto&f=*static_cast<Fixture*>(opaque);++f.bus_ops;int ready=-1;
  need(m040_interrupt_ready(6,&ready)==M040_BUSY,"readiness callback reentry is rejected without target work");++reentry_checks;
  const auto address=q->address>=0xff000000?q->address&0x00ffffff:q->address;
  if(address>=0x00ff8c81&&address<0x00ff8c89){
   need(q->size==1,"SCC CPU request preserves byte width");
   if(q->kind==M040_WRITE){++f.writes;f.scc.handleWrite(address,uint8(*value));}
   else{++f.reads;*value=f.scc.handleRead(address);}return M040_BUS_COMPLETED;
  }
  if(uint64_t(address)+q->size>f.ram.size()||(f.deny_reads&&q->kind==M040_READ&&address==0x5000)||(f.deny_writes&&q->kind==M040_WRITE))return M040_BUS_FAULT_BEFORE_TRANSFER;
  if(q->kind==M040_WRITE)f.put(address,q->size,*value);else *value=f.get(address,q->size);
  return M040_BUS_COMPLETED;
 }
 void reg(unsigned channel,unsigned number,uint8 value){scc.handleWrite(0x00ff8c81+channel,number);scc.handleWrite(0x00ff8c81+channel,value);}
 void queue(uint8 value){serial->input.push_back(value);scc.IRQ();need(SPCFLAGS_TEST(SPCFLAG_SCC),"actual SCC source raises request for owned byte");}
};
unsigned handler(unsigned vector){return 0x2000+16*vector;}
m040_state state(){m040_state s{};need(m040_get_state(&s)==M040_OK,"export canonical CPU state");return s;}
m040_result step(unsigned budget=1){m040_result r{};need(m040_run(budget,&r)==M040_OK,"run real CPU body");return r;}
void setup(Fixture&f,unsigned sr=0x2000){
 regs={};f.ram.fill(0);f.delivered.clear();f.mfp_acks=f.scc_acks=f.bus_ops=f.reads=f.writes=0;f.deny_reads=f.deny_writes=f.spurious_mfp=false;
 f.scc.reset();f.serial->input.clear();
 const m040_options options{M040_ABI_VERSION,1,0,0};
 need(m040_reset(&options)==M040_OK&&m040_set_precise_bus(Fixture::bus,&f)==M040_OK&&m040_fault_recovery(1)==M040_OK,"attach owned exact-transfer bus and genuine restart bodies");
 auto s=state();s.pc=s.instruction_pc=0x1000;s.sr=sr;s.usp=0x6000;s.isp=0x9000;s.msp=0xa000;
 for(unsigned i=0;i<16;++i)s.regs[i]=0x11000000+i;
 s.regs[15]=(sr&0x2000)?((sr&0x1000)?s.msp:s.isp):s.usp;
 need(m040_set_state(&s)==M040_OK,"import canonical user/ISP/MSP bank");
 f.put(0x1000,2,0x4e71);
 for(unsigned vector:{2u,9u,27u,28u,29u,69u,164u}){f.put(vector*4,4,handler(vector));f.put(handler(vector),2,0x4e73);}
 f.reg(0,2,0xa0);f.reg(4,1,0x18);f.reg(0,9,9);
 // Real SCC byte read, RAM observation and WR0 Reset Highest IUS, preserving D0.
 unsigned pc=handler(164);for(unsigned word:{0x2f00u,0x1039u,0xffffu,0x8c87u,0x13c0u,0u,0x700u,0x13fcu,0x38u,0xffffu,0x8c85u,0x201fu,0x4e73u}){f.put(pc,2,word);pc+=2;}
}
bool modern040_interrupt_ready(unsigned level){
 const auto count=active->bus_ops;const auto before=state();int ready=-1;
 need(m040_interrupt_ready(level,&ready)==M040_OK,"pure core readiness query succeeds");
 const auto after=state();
 need(active->bus_ops==count+(bad_pure?1u:0u)&&before.pc==after.pc&&before.sr==after.sr&&before.regs[15]==after.regs[15],"pure readiness adds no target read or CPU frame");
 return ready;
}
void deliver(unsigned vector,unsigned level){
 m040_result result{};int accepted=0;
 need(m040_interrupt(vector,level,&accepted,&result)==M040_OK&&accepted&&result.exit==M040_BUDGET,"selected source remains accepted after acknowledgement");
 active->delivered.push_back(vector);++total_irqs;
}
void Interrupt(int level){deliver(24+unsigned(level),unsigned(level));}
int MFPdoInterrupt(){++active->mfp_acks;TriggerMFP(false);return active->spurious_mfp?0:69;}
int SCCdoInterrupt(){++active->scc_acks;return active->scc.doInterrupt();}
void MFPInterrupt(int vector){deliver(unsigned(vector),6);}
void SCCInterrupt(int vector){deliver(unsigned(vector),5);}
#include "irq-priority-selector.inc"
void dispatch(bool reset_stop=false){need(modern040_serve_priority_irqs(reset_stop),"enabled selector owns the dispatch");}
void pure_query(){
 int ready=0;need(m040_interrupt_ready(0,&ready)==M040_BAD_ARGUMENT&&m040_interrupt_ready(8,&ready)==M040_BAD_ARGUMENT&&m040_interrupt_ready(6,nullptr)==M040_BAD_ARGUMENT,"readiness validates arguments");
 for(unsigned mask=0;mask<8;++mask)for(unsigned level=1;level<=7;++level){
  Fixture f;setup(f,0x2000|(mask<<8));const auto before=state();const auto requests=f.bus_ops;
  const bool expected=level>mask||level==7;
  need(modern040_interrupt_ready(level)==expected,"readiness matches architectural level/mask acceptance");
  need(f.bus_ops==requests,"readiness does not inspect RAM or device vectors");
  m040_result result{};int accepted=-1;need(m040_interrupt(69,level,&accepted,&result)==M040_OK&&bool(accepted)==expected,"unchanged injection predicate agrees with pure query");
  if(!expected)need(state().pc==before.pc,"masked injection leaves PC unchanged");
 }
}
void priority_and_blocking(){
 Fixture f;setup(f);f.queue(0x5a);TriggerMFP(true);TriggerInt5();TriggerVBL();TriggerInt3();dispatch();
 need(f.delivered.size()==(bad_count?2u:1u),"one accepted interrupt per dispatch");
 need(f.delivered[0]==(bad_priority?27u:69u)&&f.mfp_acks==1&&f.scc_acks==0,"highest eligible source is selected before other acknowledgements");
 need(SPCFLAGS_TEST(SPCFLAG_SCC|SPCFLAG_INT5|SPCFLAG_VBL|SPCFLAG_INT3)==(SPCFLAG_SCC|SPCFLAG_INT5|SPCFLAG_VBL|SPCFLAG_INT3),"lower pending sources survive priority selection");
 const auto requests=f.bus_ops;dispatch();
 need(f.mfp_acks==(bad_ack?2u:1u)&&f.scc_acks==0&&f.bus_ops==requests,"masked sources are not acknowledged");
 setup(f);need(m040_private_operations(1)==M040_OK,"enable actual private service suspension");f.put(0x1000,2,0x7300);
 need(step().exit==M040_HOST_SERVICE,"real guest instruction suspends with service pending");f.queue(0x5a);TriggerMFP(true);TriggerVBL();
 const auto service_requests=f.bus_ops;dispatch();
 need(!f.mfp_acks&&!f.scc_acks&&f.delivered.empty()&&f.bus_ops==service_requests&&SPCFLAGS_TEST(SPCFLAG_MFP|SPCFLAG_SCC|SPCFLAG_VBL)==(SPCFLAG_MFP|SPCFLAG_SCC|SPCFLAG_VBL),"service-pending CPU leaves source acknowledgements and requests untouched");
 setup(f);f.put(0x1000,2,0x4e40);f.deny_writes=true;
 need(step().exit==M040_DOUBLE_FAULT,"actual failed exception-frame construction halts CPU");TriggerMFP(true);TriggerVBL();const auto halted_requests=f.bus_ops;dispatch();
 need(f.mfp_acks==0&&f.delivered.empty()&&f.bus_ops==halted_requests,"halted CPU cannot consume a source");
 setup(f);auto s=state();s.regs[8]=0x5000;need(m040_set_state(&s)==M040_OK,"set actual read-fault EA");f.put(0x1000,2,0x1210);f.deny_reads=true;
 need(step().vector==2,"actual operand read fault delivers format7");f.deny_reads=false;
 need(step().exit==M040_BUDGET&&state().pc==0x1000,"actual format7 RTE restores unfinished instruction");
 int pending=0;need(m040_native_restart_pending(&pending)==M040_OK&&pending,"actual RTE retains native restart holdoff");
 need(modern040_interrupt_ready(6)==bad_restart,"unfinished restart blocks readiness");
 TriggerMFP(true);const auto held_requests=f.bus_ops;dispatch();need(!f.mfp_acks&&f.bus_ops==held_requests,"restart holdoff protects peripheral acknowledgement");
 need(step().exit==M040_BUDGET&&state().pc==0x1002,"actual restarted read completes");dispatch();need(f.mfp_acks==1&&f.delivered.size()==1,"completed retry releases pending source once");
}
void banks_and_scc(){
 for(unsigned mode:{0u,0x2000u,0x3000u}){
  Fixture f;setup(f,mode);const auto original=state();f.queue(0x5a);dispatch();auto entry=state();
  const unsigned sp=original.isp-8;
  need(entry.pc==handler(164)&&entry.regs[15]==sp+(bad_bank?4u:0u)&&f.get(sp+2,4)==original.pc,"IRQ frame uses correct bank and unexecuted PC");
  need(f.get(sp+6,2)==((mode&0x1000?0x1000:0)|164*4),"master entry has genuine ISP throwaway format1");
  if(mode&0x1000)need(f.get(original.msp-6,4)==original.pc&&f.get(original.msp-2,2)==164*4,"master original context remains on MSP format0");
  need(step(6).exit==M040_BUDGET&&state().pc==original.pc,"actual SCC read/EOI/RTE restores original continuation");
  const auto after=state();need(after.sr==original.sr&&after.usp==original.usp&&after.isp==original.isp&&after.msp==original.msp,"SCC IRQ/RTE preserves SR and all three stack banks");
  for(unsigned i=0;i<16;++i)need(after.regs[i]==original.regs[i],"SCC handler preserves each actual GPR");
  need(f.get(0x700,1)==0x5a&&f.reads==1&&f.writes==1&&f.serial->input.empty(),"actual SCC byte and WR0 EOI consume owned input exactly once");
  const auto count=f.delivered.size();dispatch();need(f.delivered.size()==count,"consumed SCC input cannot duplicate acknowledgement");
  f.queue(0x6b);dispatch();need(step(6).exit==M040_BUDGET&&f.get(0x700,1)==0x6b&&f.delivered.size()==2&&f.reads==2,"actual SCC source can reassert with a new owned byte");
 }
}
void spurious_tie_stop_nested(){
 Fixture f;setup(f);f.spurious_mfp=true;TriggerMFP(true);TriggerSCC(true);TriggerVBL();dispatch();
 need(f.mfp_acks==1&&f.scc_acks==1&&f.delivered==std::vector<unsigned>{28}&&!SPCFLAGS_TEST(SPCFLAG_MFP|SPCFLAG_SCC),"zero-vector sources clear only themselves and permit lower eligible request");
 setup(f);f.queue(0x5a);TriggerInt5();dispatch();
 need(f.delivered==std::vector<unsigned>{bad_tie?164u:29u}&&f.scc_acks==0,"equal-level INT5 retains priority before SCC");
 need(step().exit==M040_BUDGET,"actual INT5 RTE completes");dispatch();need(step(6).exit==M040_BUDGET&&f.delivered==std::vector<unsigned>({29,164}),"equal-level SCC remains pending until actual INT5 return");
 setup(f);f.put(0x1000,2,0x4e72);f.put(0x1002,2,0x2700);step();regs.stopped=1;SPCFLAGS_SET(SPCFLAG_STOP);TriggerMFP(true);dispatch(true);
 need(regs.stopped==1&&bool(SPCFLAGS_TEST(SPCFLAG_STOP))!=bad_stop&&!f.mfp_acks,"blocked STOP retains STOP and performs no acknowledge");
 setup(f);f.put(0x1000,2,0x4e72);f.put(0x1002,2,0x2000);step();regs.stopped=1;SPCFLAGS_SET(SPCFLAG_STOP);TriggerMFP(true);dispatch(true);
 need(!regs.stopped&&!SPCFLAGS_TEST(SPCFLAG_STOP)&&!state().stopped&&f.delivered==std::vector<unsigned>{69},"accepted IRQ releases actual STOP and host flag");
 setup(f);const auto original=state();f.queue(0x5a);TriggerMFP(true);TriggerInt5();TriggerVBL();TriggerInt3();
 f.put(handler(69),2,0x46fc);f.put(handler(69)+2,2,0x2000);f.put(handler(69)+4,2,0x4e73);dispatch();
 need(step().exit==M040_BUDGET&&state().pc==handler(69)+4,"actual highest handler lowers its mask for nested sources");
 for(unsigned vector:{29u,164u,28u,27u}){
  const auto before=f.delivered.size();dispatch();auto current=state();
  need(f.delivered.size()==before+1&&f.delivered.back()==vector&&f.get(current.regs[15]+2,4)==handler(69)+4&&current.regs[15]==original.isp-16,"nested arbitration preserves the suspended handler PC and one frame");
  need(step(vector==164?6:1).exit==M040_BUDGET&&state().pc==handler(69)+4,"actual nested handler returns to suspended continuation");
 }
 need(step().exit==M040_BUDGET&&state().pc==original.pc&&state().regs[15]==original.regs[15],"outer actual RTE restores original context after nested sources");
 setup(f,0xa000);f.put(0x1000,2,0x4e71);auto trace_result=step();m040_exception_event event{};
 need(trace_result.exit==M040_BUDGET&&m040_get_exception_event(&event)==M040_OK&&event.vector==9&&event.count==1&&event.format==2&&state().pc==handler(9)&&f.get(event.stack+6,2)==0x2024,"actual traced NOP builds format2 Trace9 before IRQ");
 const auto traced=state();TriggerMFP(true);dispatch();auto irq=state();
 need(f.get(irq.regs[15]+2,4)==traced.pc+(bad_trace?2u:0u)&&irq.regs[15]==traced.regs[15]-8,"IRQ after trace entry saves the unexecuted trace-handler PC");
}
int main(int argc,char**argv){try{
 for(int i=1;i<argc;++i){std::string_view a=argv[i];if(a=="--profile-off")profile_off=true;else if(a=="--negative-priority")bad_priority=true;else if(a=="--negative-count")bad_count=true;else if(a=="--negative-ack")bad_ack=true;else if(a=="--negative-bank")bad_bank=true;else if(a=="--negative-stop")bad_stop=true;else if(a=="--negative-tie")bad_tie=true;else if(a=="--negative-pure")bad_pure=true;else if(a=="--negative-restart")bad_restart=true;else if(a=="--negative-trace")bad_trace=true;else throw std::runtime_error("bad mode");}
 pure_query();
 if(profile_off){Fixture f;setup(f);f.queue(0x5a);TriggerMFP(true);TriggerVBL();const auto flags=regs.spcflags,requests=f.bus_ops;need(!modern040_serve_priority_irqs(true)&&regs.spcflags==flags&&f.bus_ops==requests&&f.delivered.empty()&&!f.mfp_acks&&!f.scc_acks,"default-off selector leaves legacy path and sources untouched");}
 else{priority_and_blocking();banks_and_scc();spurious_tie_stop_nested();}
 std::cout<<"PASS: "<<checks<<" IRQ priority platform assertions; real-SCC-reads="<<total_scc_reads<<" actual-IRQ-entries="<<total_irqs<<" busy-query-controls="<<reentry_checks<<"\n";
}catch(const std::exception&e){std::cerr<<"FAIL: "<<e.what()<<"\n";return 1;}}
