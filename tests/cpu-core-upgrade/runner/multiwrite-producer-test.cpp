/* SPDX-License-Identifier: GPL-2.0-or-later
 * Before-only adapter audit: genuine guest software WB3/RTE and explicit retry.
 * Independent bit-by-bit RAM oracle; not MMIO or full hardware qualification.
 */
#include "restart.hpp"
#include <iostream>
#include <stdexcept>
#include <string_view>
using namespace modern040;
namespace {
unsigned checks=0;bool audit=false,controls=false,bad_count=false,bad_pointer=false,bad_data=false,bad_protocol=false;
void need(bool ok,const char*why){++checks;if(!ok)throw std::runtime_error(why);}
struct Event {BusRequest q;uae_u32 value;BusCompletion status;};
struct Bus {
 std::array<uae_u8,65536> ram{};std::vector<Event> events;unsigned deny=0,attempts=0;
 void put(unsigned a,unsigned n,unsigned v){for(unsigned i=n;i;--i){ram.at(a+i-1)=uae_u8(v);v>>=8;}}
 unsigned get(unsigned a,unsigned n)const{unsigned v=0;for(unsigned i=0;i<n;++i)v=(v<<8)|ram.at(a+i);return v;}
 static BusCompletion access(void*o,const BusRequest&q,uae_u32&v){auto&b=*static_cast<Bus*>(o);auto status=BusCompletion::Completed;
  if(q.kind==AccessKind::Write&&q.address>=0x5000&&q.address<0x5010&&++b.attempts==b.deny)status=BusCompletion::FaultBeforeTransfer;
  else if(q.kind==AccessKind::Write)b.put(q.address,q.size,v);else v=b.get(q.address,q.size);
  b.events.push_back({q,v,status});return status;
 }
 unsigned count(AccessKind kind,unsigned a,BusCompletion status=BusCompletion::Completed)const{unsigned n=0;for(const auto&e:events)if(e.q.kind==kind&&e.q.address==a&&e.status==status)++n;return n;}
 unsigned operand_reads()const{unsigned n=0;for(const auto&e:events)if(e.q.kind==AccessKind::Read&&e.q.address>=0x5000&&e.q.address<0x5010&&e.status==BusCompletion::Completed)++n;return n;}
};
struct Form {unsigned kind,ea,offset,width;};
struct Stats {unsigned cases=0,data_only=0,flags_only=0,both=0,duplicate=0,reads=0;};
Stats stats[5];
constexpr const char*names[]={"BFCHG","BFCLR","BFSET","BFINS","MOVEP"};
constexpr std::array<unsigned char,8> original{0x12,0x34,0x56,0x78,0x9a,0xbc,0xde,0xf0};
unsigned setup(Bus&b,const Form&f,unsigned mode,unsigned ccr){
 reset();host.deliver_exceptions=host.fault_recovery=true;host.precise_bus=Bus::access;host.bus_opaque=&b;
 regs.isp=regs.regs[15]=0x9000;regs.usp=0x7000;regs.msp=0x8000;regs.sr=uae_u16(mode|0x500|ccr);MakeFromSR();
 for(unsigned i=0;i<15;++i)regs.regs[i]=0x12340000+i*0x1111;
 regs.regs[0]=0xdeadbeef;regs.regs[8]=f.ea==1?0x4ffc:f.ea==2?0x5200:0x5000;
 for(unsigned i=0;i<8;++i)b.ram[0x5000+i]=original[i];b.put(0x5200,4,0x5000);
 b.put(8,4,0x200);b.put(36,4,0x300);b.put(69*4,4,0x700);
 if(f.kind==4){b.put(0x100,2,f.width==16?0x0188:0x01c8);b.put(0x102,2,0);return 4;}
 const unsigned ea[]={0x10,0x28,0x30,0x38,0x39};b.put(0x100,2,0xeac0+(f.kind==3?0x500:f.kind*0x200)+ea[f.ea]);
 b.put(0x102,2,(f.offset<<6)|(f.width&31));
 if(f.ea==1)b.put(0x104,2,4);if(f.ea==2)b.put(0x104,2,0x0151);if(f.ea==3)b.put(0x104,2,0x5000);if(f.ea==4)b.put(0x104,4,0x5000);
 return f.ea==0?4:f.ea==4?8:6;
}
std::array<unsigned char,8> oracle(const Form&f,unsigned ccr,unsigned& flags){
 auto out=original;flags=ccr;
 if(f.kind==4){for(unsigned i=0;i<f.width/8;++i)out[2*i]=uae_u8(0xdeadbeefu>>(f.width-8-8*i));return out;}
 unsigned field=0;for(unsigned i=0;i<f.width;++i){const auto bit=f.offset+i;field=(field<<1)|((original[bit/8]>>(7-bit%8))&1);}
 const auto mask=f.width==32?0xffffffffu:(1u<<f.width)-1;const auto value=f.kind==0?(field^mask):f.kind==1?0:f.kind==2?mask:0xdeadbeefu&mask;
 const auto metric=f.kind==3?value:field;flags=(ccr&16)|((metric>>(f.width-1))&1?8:0)|(metric==0?4:0);
 for(unsigned i=0;i<f.width;++i){const unsigned bit=f.offset+i,m=1u<<(7-bit%8);out[bit/8]=uae_u8((out[bit/8]&~m)|(((value>>(f.width-1-i))&1)?m:0));}
 return out;
}
bool data_equal(const Bus&b,const std::array<unsigned char,8>& expected){for(unsigned i=0;i<8;++i)if(b.ram[0x5000+i]!=expected[i])return false;return true;}
void one(const Form&f,unsigned mode,unsigned ccr){
 Bus reference;const unsigned length=setup(reference,f,mode,ccr);unsigned flags;auto expected=oracle(f,ccr,flags);if(bad_data)expected[7]^=1;
 const auto before=architectural_state();const auto result=run(1);
 need(result.exit==Exit::Budget&&data_equal(reference,expected)&&(architectural_state().sr&31)==flags,"no-fault output agrees with independent bit-by-bit data/CCR oracle");
 need(regs.pc==(mode==0x8000?0x300u:0x100+length),"no-fault PC/trace destination control");
 std::vector<Event> writes;for(const auto&e:reference.events)if(e.q.kind==AccessKind::Write&&e.q.address>=0x5000&&e.q.address<0x5010)writes.push_back(e);
 const auto reads=reference.operand_reads();need(!writes.empty()&&writes.size()<5,"actual helper control has bounded architectural transfers");
 const unsigned span=(f.offset+f.width+7)/8;
 const unsigned transfers=f.kind==4?f.width/8:span==3||span==5?2:1;
 need(writes.size()==transfers&&reads==(f.kind==4?0:transfers),"independent byte-span transfer decomposition control");
 std::vector<Event> operand;for(const auto&e:reference.events)if(e.q.address>=0x5000&&e.q.address<0x5010)operand.push_back(e);
 need(operand.size()==transfers*(f.kind==4?1:2),"independent ordered operand request count");
 for(unsigned i=0;i<operand.size();++i){
  const unsigned part=i%transfers;const bool writing=f.kind==4||i>=transfers;
  const unsigned offset=f.kind==4?2*part:part?(span==3?2:4):0;
  unsigned size=f.kind==4?1:part?1:span==3?2:span==5?4:span;if(bad_protocol&&i==0)++size;
  const auto&e=operand[i];unsigned value=0;for(unsigned j=0;j<size&&offset+j<8;++j)value=(value<<8)|(writing?expected[offset+j]:original[offset+j]);
  need(e.q.kind==(writing?AccessKind::Write:AccessKind::Read)&&e.q.address==0x5000+offset&&e.q.size==size&&e.q.function_code==1&&!e.q.alternate_function_code&&!e.q.transfer_type&&e.value==value&&e.status==BusCompletion::Completed,"independent request order/address/width/FC/value protocol");
 }
 if(controls)return;
 for(unsigned index=0;index<writes.size();++index){
  Bus b;need(setup(b,f,mode,ccr)==length,"identical before-fault setup");b.deny=index+1;
  const auto fault=run(1);const unsigned sp=0x9000-60;const auto&failed=writes[index];
  need(fault.exit==Exit::Budget&&fault.vector==2&&regs.pc==0x200,"write producer reaches genuine vector2 handler");
  need(b.attempts==index+1&&b.operand_reads()==reads,"no later operand access follows the failed transfer before handler");
  need(b.get(sp,2)==(mode|0x500|ccr)&&b.get(sp+2,4)==0x100&&b.get(sp+6,2)==0x7008,"unfinished producer restores original CCR/PC in format7");
  const unsigned ssw=1|(failed.q.size==1?0x20:failed.q.size==2?0x40:0);
  need(b.get(sp+12,2)==ssw&&b.get(sp+14,2)==(ssw|0x80)&&b.get(sp+24,4)==failed.q.address&&b.get(sp+28,4)==failed.value,"exact failed transfer WB3/FC/size/data without invented CT or CM");
  for(unsigned i=0;i<writes.size();++i)need(b.count(AccessKind::Write,writes[i].q.address)==unsigned(i<index),"successful write prefix recorded separately from failure");
  // Explicit guest writeback of the queued B/W/L value, preserving A0.
  unsigned a=0x200;for(unsigned w:{0x23c8u,0u,0x6000u,0x206fu,24u,failed.q.size==1?0x10afu:failed.q.size==2?0x30afu:0x20afu,failed.q.size==1?31u:failed.q.size==2?30u:28u,0x2079u,0u,0x6000u,0x426fu,14u,0x4e73u}){b.put(a,2,w);a+=2;}
  b.deny=0;need(run(6).exit==Exit::Budget&&regs.pc==0x100&&regs.regs[15]==0x7000,"genuine software WB3 and RTE returns to unfinished instruction");
  need(!inject_interrupt(69,6),"unfinished ordinary write retains current IRQ restart holdoff");
  need(run(1).exit==Exit::Budget&&regs.pc==(mode==0x8000?0x300u:0x100+length),"explicit instruction retry completes with selected trace mode");
  const bool data_ok=data_equal(b,expected),flags_ok=(architectural_state().sr&31)==flags;auto&st=stats[f.kind];++st.cases;
  if(!data_ok&&!flags_ok)++st.both;else if(!data_ok)++st.data_only;else if(!flags_ok)++st.flags_only;
  if(!audit)need(data_ok&&flags_ok,"recovery output differs from independent bit-field oracle");
  for(unsigned i=0;i<writes.size();++i){const unsigned n=i<=index?2:1;need(b.count(AccessKind::Write,writes[i].q.address)==(bad_count?n+1:n),"completed prefix and software-WB3 transfer repeat on explicit retry");if(n==2)++st.duplicate;}
  need(b.operand_reads()==2*reads,"bitfield operand reads repeat only after explicit guest WB3/RTE");st.reads+=reads;
  need(b.count(AccessKind::Read,0x5200)==(f.kind!=4&&f.ea==2?(bad_pointer?1u:2u):0u),"indirect RAM pointer repeats after actual guest return");
  const auto after=architectural_state();for(unsigned i=0;i<15;++i)need(after.regs[i]==before.regs[i],"all non-stack GPRs preserved through software WB3/RTE/retry");
 }
}
}
int main(int argc,char**argv){try{
 for(int i=1;i<argc;++i){std::string_view a=argv[i];if(a=="--audit-replay")audit=true;else if(a=="--only-controls")controls=true;else if(a=="--negative-count")bad_count=true;else if(a=="--negative-pointer")bad_pointer=true;else if(a=="--negative-data")bad_data=true;else if(a=="--negative-protocol")bad_protocol=true;else throw std::runtime_error("bad argument");}
 install_handlers();install_restart_handlers();
 for(unsigned kind=0;kind<5;++kind)for(unsigned ea=0;ea<(kind==4?1u:5u);++ea)
 for(auto layout:std::array<std::array<unsigned,2>,9>{{{0,1},{0,8},{7,2},{0,16},{7,17},{0,24},{7,25},{0,32},{7,32}}}){
  if(kind==4&&(layout[0]||!(layout[1]==16||layout[1]==32)))continue;
  for(unsigned mode:{0u,0x4000u,0x8000u})for(unsigned ccr=0;ccr<32;++ccr)one({kind,ea,layout[0],layout[1]},mode,ccr);
 }
 for(unsigned i=0;i<5;++i){const auto&st=stats[i];std::cout<<names[i]<<" cases="<<st.cases<<" data-only="<<st.data_only<<" flags-only="<<st.flags_only<<" both="<<st.both<<" duplicated-writes="<<st.duplicate<<" replayed-reads="<<st.reads<<'\n';}
 if(audit&&!controls)need(stats[0].cases&&stats[0].data_only+stats[0].both==stats[0].cases,"before audit exposes BFCHG data corruption in every injected case");
 std::cout<<"PASS: "<<checks<<" multiwrite producer audit assertions (owned RAM, not hardware qualification)\n";
}catch(const std::exception&e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}}
