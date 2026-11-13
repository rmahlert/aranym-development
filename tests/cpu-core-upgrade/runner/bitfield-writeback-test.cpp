/* SPDX-License-Identifier: GPL-2.0-or-later
 * Independent bit-by-bit result oracle and real guest WB1/WB3 cleanup/RTE.
 * A read-sensitive owned bus exposes rereads and duplicate target writes.
 */
#include "c-api.h"
#include "restart.hpp"
#include <iostream>
#include <string_view>
using namespace modern040;
namespace {
unsigned checks=0,frames=0,controls=0;bool before=false,quick=false,bad_data=false,bad_flags=false,bad_count=false,bad_address=false;
void need(bool ok,const char*why){++checks;if(!ok)throw std::runtime_error(why);}
Result execute(unsigned n){m040_result r{};need(m040_run(n,&r)==M040_OK,"public execution boundary");return {Exit(r.exit),r.instructions,int(r.vector),r.instruction_pc,r.current_pc,r.address,r.size,r.detail};}
struct Event {m040_bus_request q;unsigned value;bool completed;};
struct Bus {
 std::array<unsigned char,65536> ram{};std::vector<Event> events;
 unsigned base=0x5008,deny=0,attempts=0,reads=0,failed_at=0,evidence_mode=0,deny_nested=~0u;bool sensitive=false,alias=false,reentry=false;
 unsigned map(unsigned a)const{return a>=0x40005000&&a<0x40005040?a-0x40000000:a;}
 bool target(unsigned a)const{a=map(a);return a>=0x5000&&a<0x5040;}
 void put(unsigned a,unsigned n,unsigned v){a=map(a);for(unsigned i=n;i;--i){ram.at(a+i-1)=v;v>>=8;}}
 unsigned get(unsigned a,unsigned n)const{a=map(a);unsigned v=0;for(unsigned i=0;i<n;++i)v=(v<<8)|ram.at(a+i);return v;}
 static int access(void*o,const m040_bus_request*q,uint32_t*v,m040_bus_detail*d){
  auto&b=*static_cast<Bus*>(o);
#ifdef BITFIELD_TAIL_ADMITTED
  if(!b.reentry){need(m040_bitfield_write_exceptions(0)==M040_BUSY,"bitfield profile cannot change during a callback");b.reentry=true;}
#endif
  const unsigned input=*v;
  if(q->address==b.deny_nested){d->completion=M040_BUS_FAULT_BEFORE_TRANSFER;d->origin=M040_ORIGIN_EXTERNAL_BUS;d->whole_scalar_no_commit=1;d->has_cpu_physical_address=1;d->cpu_physical_address=q->address;b.events.push_back({*q,input,false});return 1;}
  if(q->kind==M040_WRITE&&b.target(q->address)&&++b.attempts==b.deny){
   d->completion=M040_BUS_FAULT_BEFORE_TRANSFER;d->origin=M040_ORIGIN_EXTERNAL_BUS;
   d->whole_scalar_no_commit=1;d->has_cpu_physical_address=1;d->cpu_physical_address=q->address+(b.alias?0x40000000:0);
   b.events.push_back({*q,input,false});b.failed_at=b.events.size();*v^=0xffffffff;
   if(b.evidence_mode==1)d->origin=M040_ORIGIN_UNSPECIFIED;
   if(b.evidence_mode==2)d->origin=M040_ORIGIN_ATC_TRANSLATION;
   if(b.evidence_mode==3)d->whole_scalar_no_commit=0;
   if(b.evidence_mode==4)d->has_cpu_physical_address=0;
   if(b.evidence_mode==5){d->completion=M040_BUS_UNKNOWN_FAULT;d->whole_scalar_no_commit=0;}
   if(b.evidence_mode==6)return 0;
   return 1;
  }
  if(q->kind==M040_WRITE)b.put(q->address,q->size,*v);
  else if(q->kind==M040_READ&&b.target(q->address)&&b.sensitive){
   *v=0x12345678u^(0x13579bdu*++b.reads);if(q->size<4)*v&=(1u<<(8*q->size))-1;
  }else{*v=b.get(q->address,q->size);if(q->kind==M040_READ&&b.target(q->address))++b.reads;}
  b.events.push_back({*q,q->kind==M040_WRITE?input:*v,true});d->completion=M040_BUS_COMPLETED;return 1;
 }
 std::vector<Event> operands()const{std::vector<Event> out;for(const auto&e:events)if(target(e.q.address))out.push_back(e);return out;}
 unsigned pointer_reads()const{unsigned n=0;for(const auto&e:events)if(e.q.kind==M040_READ&&e.q.address==0x5200)++n;return n;}
};
struct Form {unsigned kind,ea,offset,width;};
constexpr std::array<unsigned char,8> original{0x92,0x34,0x56,0x78,0x9a,0xbc,0xde,0xf0};
unsigned top(unsigned mode){return mode&0x1000?0x8000:0x9000;}
unsigned setup(Bus&b,const Form&f,unsigned mode,unsigned trace,unsigned ccr){
 const m040_options options{M040_ABI_VERSION,1,0,0};
 need(m040_reset(&options)==M040_OK&&m040_fault_recovery(1)==M040_OK&&m040_set_detailed_bus(Bus::access,&b)==M040_OK&&m040_physical_write_exceptions(1)==M040_OK&&m040_physical_read_exceptions(1)==M040_OK,"attach qualified detailed physical profiles");
#ifdef BITFIELD_TAIL_ADMITTED
 need(m040_bitfield_write_exceptions(1)==M040_OK,"explicit bounded bitfield tail opt-in");
#endif
 regs.regs[15]=regs.isp=0x9000;regs.usp=0x7000;regs.msp=0x8000;regs.sr=mode|trace|0x500|ccr;MakeFromSR();
 for(unsigned i=0;i<15;++i)regs.regs[i]=0x12340000+i*0x1111;
 regs.regs[0]=b.sensitive?0:0xdeadbeef;regs.regs[8]=f.ea==1?b.base-4:f.ea==2?0x5200:b.base;regs.dfc=7;regs.sfc=2;
 for(unsigned i=0;i<64;++i)b.ram[0x5000+i]=0xa5;
 for(unsigned i=0;i<8;++i)b.ram[b.base+i]=original[i];b.put(0x5200,4,b.base);
 b.put(8,4,0x200);b.put(36,4,0x700);b.put(0x700,2,0x0257);b.put(0x702,2,0x3fff);b.put(0x704,2,0x4e73);
 const unsigned ea[]={0x10,0x28,0x30,0x38,0x39};b.put(0x100,2,0xeac0+(f.kind==3?0x500:f.kind*0x200)+ea[f.ea]);b.put(0x102,2,(f.offset<<6)|(f.width&31));
 if(f.ea==1)b.put(0x104,2,4);if(f.ea==2)b.put(0x104,2,0x0151);if(f.ea==3)b.put(0x104,2,b.base);if(f.ea==4)b.put(0x104,4,b.base);
 const unsigned length=f.ea==0?4:f.ea==4?8:6;b.put(0x100+length,2,0x2048);
 for(unsigned i=0;i<12;++i)b.put(top(mode)+i,1,0xa5^i);return length;
}
std::array<unsigned char,8> oracle(const Form&f,const Bus&b,unsigned ccr,unsigned&flags){
 auto out=original;
 // Reconstruct the data actually returned by the scripted read-sensitive bus.
 // The bit algorithm below shares no implementation with the CPU helper.
 for(const auto&e:b.events)if(e.q.kind==M040_READ&&b.target(e.q.address))for(unsigned i=0;i<e.q.size;++i)out[b.map(e.q.address)-b.base+i]=(e.value>>(8*(e.q.size-1-i)))&255;
 unsigned field=0;for(unsigned i=0;i<f.width;++i){const auto bit=f.offset+i;field=(field<<1)|((out[bit/8]>>(7-bit%8))&1);}
 const unsigned mask=f.width==32?~0u:(1u<<f.width)-1,source=b.sensitive?0:0xdeadbeef;
 const unsigned value=f.kind==0?field^mask:f.kind==1?0:f.kind==2?mask:source&mask;
 const auto metric=f.kind==3?value:field;flags=(ccr&16)|((metric>>(f.width-1))&1?8:0)|(metric==0?4:0);
 for(unsigned i=0;i<f.width;++i){const unsigned bit=f.offset+i,m=1u<<(7-bit%8);out[bit/8]=(out[bit/8]&~m)|(((value>>(f.width-1-i))&1)?m:0);}
 return out;
}
void data(const Bus&b,const std::array<unsigned char,8>&expected){for(unsigned i=0;i<64;++i)need(b.ram[0x5000+i]==(0x5000+i>=b.base&&0x5000+i<b.base+8?expected[0x5000+i-b.base]:0xa5u),"independent bit-by-bit data and adjacent-byte oracle");}
void trace_return(Bus&b,unsigned mode,unsigned trace,unsigned next,unsigned flags){
 if(trace==0x8000){const unsigned sp=top(mode)-12;need(regs.pc==0x700&&host.last_frame&&host.last_frame->vector==9&&b.get(sp+2,4)==next&&b.get(sp+6,2)==0x2024&&b.get(sp+8,4)==0x100,"exactly one pending trace identifies completed bitfield before next opcode");need(execute(2).exit==Exit::Budget&&regs.pc==next,"genuine trace handler clears T and returns without replay");}
 else need(regs.pc==next,"non-T1 bitfield returns directly to completed PC");
 const auto state=architectural_state();need(state.sr==(mode|0x500|(trace==0x8000?0:trace)|(bad_flags?flags^1:flags))&&state.usp==0x7000&&state.isp==0x9000&&state.msp==0x8000,"bitfield cleanup preserves completed flags and all stack banks");
 for(unsigned i=0;i<12;++i)need(b.get(top(mode)+i,1)==(0xa5u^i),"CT/RTE does not write above the original stack top");
}
void handler(Bus&b,unsigned first_size,bool pending){
 unsigned pc=0x200;const auto e=[&](unsigned w){b.put(pc,2,w);pc+=2;};
 for(unsigned i=0;i<3;++i){e(0x23c0+i);e(0);e(0x4000+4*i);}e(0x23c8);e(0);e(0x400c);e(0x4e7a);e(0x2001);
 const auto store=[&](unsigned status,unsigned address,unsigned payload,unsigned size,bool memory_lanes){
  e(0x302f);e(status);e(0x2200);e(0x0281);e(0);e(7);e(0x4e7b);e(0x1001);e(0x206f);e(address);e(0x202f);e(payload);
  if(memory_lanes){e(0x2208);e(0x0281);e(0);e(3);e(0xe789);e(0xe3b8);if(size==1){e(0xe088);e(0xe088);e(0xe088);}else if(size==2){e(0xe088);e(0xe088);}}
  e(size==1?0x0e10:size==2?0x0e50:0x0e90);e(0x0800);e(0x426f);e(status);
 };
 store(18,40,44,first_size,true);if(pending)store(14,24,28,1,false);
 e(0x4e7b);e(0x2001);for(unsigned i=0;i<3;++i){e(0x2039+(i<<9));e(0);e(0x4000+4*i);}e(0x2079);e(0);e(0x400c);e(0x4e73);
 need(pc<0x400,"owned handler does not overlap trace program");
}
void one(const Form&f,unsigned mode,unsigned trace,unsigned ccr,bool sensitive){
 Bus reference;reference.sensitive=sensitive;reference.alias=sensitive;reference.base+=f.ea%4;
 const unsigned length=setup(reference,f,mode,trace,ccr),next=0x100+length;const auto initial=architectural_state();
 need(execute(1).exit==Exit::Budget,"no-fault bitfield control executes once");unsigned flags;auto expected=oracle(f,reference,ccr,flags);if(bad_data)expected[7]^=1;data(reference,expected);trace_return(reference,mode,trace,next,flags);
 std::vector<Event>writes;for(const auto&e:reference.operands())if(e.q.kind==M040_WRITE)writes.push_back(e);
 const unsigned span=(f.offset+f.width+7)/8,count=span==3||span==5?2:1;
 need(writes.size()==count&&reference.reads==count,"independent helper byte-span transfer decomposition");++controls;
 for(unsigned failed=0;failed<count;++failed){
  Bus b;b.base=reference.base;b.sensitive=sensitive;b.alias=sensitive;setup(b,f,mode,trace,ccr);b.deny=failed+1;
  const auto result=execute(1);const auto sp=top(mode)-60;const auto&primary=writes[failed];
  if(before){need(result.exit==Exit::MemoryFault&&host.exception_count==0&&b.attempts==failed+1,"before certified bitfield write remains terminal without a frame");continue;}
  need(result.exit==Exit::Budget&&result.vector==2&&regs.pc==0x200&&host.exception_count==1,"certified bitfield tail completes into actual vector2 handler");
  const bool pending=failed==0&&count==2;const unsigned fc=mode&0x2000?5u:1u,size=primary.q.size,sizebits=size==1?0x20:size==2?0x40:0;
  need(b.get(sp,2)==(mode|trace|0x500|flags)&&b.get(sp+2,4)==next&&b.get(sp+6,2)==0x7008,"actual bitfield frame retains completed PC and independently computed SR");
  need(b.get(sp+8,4)==(trace==0x8000?0x100u:0u)&&b.get(sp+12,2)==(sizebits|fc|(trace==0x8000?0x2000:0))&&b.get(sp+16,2)==0,"only genuine pending trace EA/CT and no CM/WB2 are generated");
  const unsigned physical=primary.q.address+(b.alias?0x40000000:0);unsigned lanes=0,used=0;
  for(unsigned i=0;i<size;++i){const unsigned shift=24-8*((physical+i)&3);lanes|=((primary.value>>(8*(size-1-i)))&255)<<shift;used|=255u<<shift;}
  need(b.get(sp+18,2)==(0x80|sizebits|fc)&&b.get(sp+20,4)==primary.q.address&&b.get(sp+40,4)==physical&&b.get(sp+44,4)==lanes,"WB1 binds original failed scalar to explicit full32 physical address and memory lanes");
  need(b.get(sp+14,2)==(pending?0xa0|fc:0)&&(!pending||(b.get(sp+24,4)==(bad_address?writes[1].q.address+1:writes[1].q.address)&&b.get(sp+28,4)==writes[1].value)),"only the actually unissued logical IU write occupies register-aligned WB3");
  const auto prior=b.operands();need(prior.size()==count+failed+1&&b.reads==count&&b.pointer_reads()==(f.ea==2?1u:0u),"no later target or pointer request follows failure before guest handling");
  for(unsigned i=b.failed_at;i<b.events.size();++i){const auto&q=b.events[i].q;need((q.kind==M040_WRITE&&q.address>=sp&&q.address<top(mode))||(q.kind==M040_READ&&q.address==8),"post-failure bus requests before handler are only architectural frame/vector accesses");}
  b.put(sp+44,4,lanes|(0xa5a5a5a5u&~used));if(pending)b.put(sp+28,4,writes[1].value|0xa5a5a500u);
  handler(b,size,pending);
  for(unsigned steps=0;regs.pc!=next&&regs.pc!=0x700&&steps<100;++steps)need(execute(1).exit==Exit::Budget,"genuine guest WB1/WB3 cleanup executes without replaying bitfield");
  trace_return(b,mode,trace,next,flags);data(b,expected);
  for(unsigned i=0;i<15;++i)need(regs.regs[i]==initial.regs[i],"software cleanup/RTE preserves every non-stack GPR");
  need(regs.dfc==7&&regs.sfc==2&&regs.regs[15]==initial.regs[15],"cleanup preserves original FC registers and active A7");
  const auto final=b.operands();std::vector<Event>done;unsigned faults=0;for(const auto&e:final){if(e.q.kind==M040_WRITE){if(e.completed)done.push_back(e);else ++faults;}}
  need(done.size()==(bad_count?count+1:count)&&faults==1&&b.reads==count&&b.pointer_reads()==(f.ea==2?1u:0u),"one successful target write per original scalar and no repeated operand/pointer reads");
  for(unsigned i=0;i<count;++i){const auto&e=done[i];const unsigned address=writes[i].q.address+(i==failed&&b.alias?0x40000000:0);need(e.q.address==address&&e.q.size==writes[i].q.size&&e.q.function_code==fc&&!e.q.transfer_type&&e.value==writes[i].value,"cleanup preserves exact target ordering/address/width/FC/value protocol");}
  need(execute(1).exit==Exit::Budget&&regs.pc==next+2&&b.operands().size()==final.size(),"following MOVEA executes without reapplying bitfield or touching its target");++frames;
 }
}
#ifdef BITFIELD_TAIL_ADMITTED
uae_u32 unexpected_read(uae_u32){write(0x5008,4,0x12345678);(void)read(AccessKind::Read,0x500c,1);regs.pc+=4;return 0;}
uae_u32 unexpected_fetch(uae_u32){write(0x5008,4,0x12345678);(void)read(AccessKind::Fetch,0x104,2);regs.pc+=4;return 0;}
uae_u32 unexpected_overflow(uae_u32){write(0x5008,4,0x12345678);write(0x500c,1,0x9a);write(0x500d,1,0xbc);regs.pc+=4;return 0;}
uae_u32 unexpected_space(uae_u32){write(0x5008,4,0x12345678);ismoves_nommu=true;regs.dfc=7;write(0x500c,1,0x9a);regs.pc+=4;return 0;}
uae_u32 unexpected_exception(uae_u32){write(0x5008,4,0x12345678);Exception(4);return 0;}
uae_u32 unexpected_service(uae_u32){write(0x5008,4,0x12345678);m68k_natfeat_call();return 0;}
void rejection(){
 const Form form{0,0,7,32};
 for(unsigned mode=1;mode<=6;++mode)for(unsigned index:{1u,2u}){
  Bus b;setup(b,form,0,0,0);b.deny=index;b.evidence_mode=mode;
  need(execute(1).exit==Exit::MemoryFault&&host.access_fault_latched&&host.exception_count==0&&!host.deferred_bitfield&&!host.physical_writeback&&b.events.size()==b.failed_at,"unknown/ATC/partial/missing bitfield evidence cannot start or continue a tail");
  const auto requests=b.events.size();need(execute(17).exit==Exit::Unsupported&&b.events.size()==requests,"rejected bitfield fault cannot acquire additional IO before reset");
 }
 for(unsigned selection:{0u,1u,2u}){
  Bus b;setup(b,form,0,0,0);b.deny=1;
  if(selection==0)need(m040_bitfield_write_exceptions(0)==M040_OK,"tail profile can remain disabled");
  if(selection==1)need(m040_physical_write_exceptions(0)==M040_OK,"tail profile requires qualified physical write delivery");
  if(selection==2)host.deliver_exceptions=false;
  need(execute(1).exit==Exit::MemoryFault&&host.exception_count==0&&!host.deferred_bitfield&&b.events.size()==b.failed_at,"disabled or non-delivering profiles retain default terminal containment");
 }
 for(auto injected:{unexpected_read,unexpected_fetch,unexpected_overflow,unexpected_space,unexpected_exception,unexpected_service}){
  Bus b;setup(b,form,0,0,0);b.deny=1;restart_handlers[0xead0]=injected;need(m040_private_operations(1)==M040_OK,"enable service-abort adversarial control");
  need(execute(1).exit==Exit::MemoryFault&&host.access_fault_latched&&host.exception_count==0&&!host.deferred_bitfield&&!host.physical_writeback&&!host.service&&b.events.size()==b.failed_at&&b.operands().size()==1&&!ismoves_nommu,"unexpected future read/fetch/overflow/space/exception/service aborts before any additional backend IO");
 }
 for(unsigned address:{8u,0x8ffcu}){
  Bus b;setup(b,form,0,0,0);b.deny=1;b.deny_nested=address;
  need(execute(1).exit==Exit::DoubleFault&&host.halted&&!host.deferred_bitfield&&!host.physical_writeback,"tail frame/vector failure halts and destroys both pending scopes");
  const auto requests=b.events.size();need(execute(1).exit==Exit::DoubleFault&&b.events.size()==requests,"halted buffered write never retries a target or frame access");
 }
 Bus reset_bus;setup(reset_bus,form,0,0,0);need(m040_bitfield_write_exceptions(2)==M040_BAD_ARGUMENT,"bounded tail control validates boolean value");
 const m040_options options{M040_ABI_VERSION,1,0,0};need(m040_reset(&options)==M040_OK&&!host.bitfield_write_exceptions&&!host.deferred_bitfield&&!host.physical_writeback&&!host.physical_write_exceptions&&!host.physical_read_exceptions,"reset destroys pending data and clears all independent opt-ins");
}
#endif
}
int main(int argc,char**argv){try{
 for(int i=1;i<argc;++i){std::string_view a=argv[i];if(a=="--before-audit")before=true;else if(a=="--quick")quick=true;else if(a=="--negative-data")bad_data=true;else if(a=="--negative-flags")bad_flags=true;else if(a=="--negative-count")bad_count=true;else if(a=="--negative-address")bad_address=true;else throw std::runtime_error("bad mode");}
 for(unsigned kind=0;kind<4;++kind)for(unsigned ea=0;ea<5;++ea)for(auto layout:std::array<std::array<unsigned,2>,9>{{{0,1},{0,8},{7,2},{0,16},{7,17},{0,24},{7,25},{0,32},{7,32}}})
 for(unsigned mode:{0u,0x2000u,0x3000u})for(unsigned trace:{0u,0x4000u,0x8000u})for(unsigned ccr=0;ccr<32;++ccr)for(bool sensitive:{false,true}){
  if(quick&&(kind||ea||mode||trace||ccr||sensitive||layout!=std::array<unsigned,2>{7,32}))continue;
  one({kind,ea,layout[0],layout[1]},mode,trace,ccr,sensitive);
 }
#ifdef BITFIELD_TAIL_ADMITTED
 if(!before&&!quick)rejection();
#endif
 std::cout<<"PASS: "<<checks<<" bitfield tail/writeback assertions; actual-frames="<<frames<<" controls="<<controls<<(before?" before-only":"")<<'\n';
}catch(const std::exception&e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}}
