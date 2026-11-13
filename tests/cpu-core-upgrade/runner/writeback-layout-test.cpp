/* SPDX-License-Identifier: GPL-2.0-or-later
 * Synthetic frame/layout proof with an independent byte oracle and genuine
 * guest MOVEC/MOVES cleanup/RTE. No CPU fault-origin or MMU integration.
 */
#include "writeback-layout.hpp"
#include "restart.hpp"
#include <iostream>
#include <stdexcept>
#include <string_view>
using namespace writeback_prototype;
namespace {
unsigned checks=0,models=0,handlers=0;bool bad_alignment=false,bad_address=false,bad_origin=false,bad_count=false;
void need(bool ok,const char* why){++checks;if(!ok)throw std::runtime_error(why);}
unsigned get(const std::array<std::uint8_t,60>& b,unsigned a,unsigned n){unsigned v=0;for(unsigned i=0;i<n;++i)v=(v<<8)|b.at(a+i);return v;}
Evidence sample(Origin origin,unsigned size,unsigned fc,unsigned low,unsigned value){return {origin,true,true,true,size,fc,0,1,0x6000+low,value,0x5000+low};}
void manual_literals(){
 const std::array<std::array<unsigned,4>,3> expected{{{0x78000000,0x00780000,0x00007800,0x00000078},{0x56780000,0x00567800,0x00005678,0x78000056},{0x12345678,0x78123456,0x56781234,0x34567812}}};
 unsigned row=0;for(unsigned size:{1u,2u,4u}){
  const unsigned value=size==1?0x78:size==2?0x5678:0x12345678;
  for(unsigned low=0;low<4;++low){const auto layout=single_completed_write(sample(Origin::ExternalBus,size,5,low,value));need(layout&&layout->wb[0].data==expected[row][low],"literal Motorola Table8-5 byte-lane examples including wrapped word");}
  ++row;
 }
}
void oracle(const Evidence&e){
 const auto proposed=single_completed_write(e);need(bool(proposed),"explicit certified origin produces a single completed-write layout");const auto& p=*proposed;
 const bool physical=e.origin==Origin::ExternalBus;const auto slot=physical?0:1;
 const auto address=physical?*e.physical_address:e.logical_address;
 const unsigned sizebits=e.size==1?0x20:e.size==2?0x40:0;
 const auto bytes=frame(p,std::uint16_t(0x500|(e.function_code==5?0x2000:0)),0x102);
 need(get(bytes,6,2)==0x7008&&get(bytes,2,4)==0x102,"format7 header retains caller-supplied completed PC");
 need(get(bytes,12,2)==(sizebits|e.function_code|(physical?0:0x400)),"independent SSW distinguishes external from supplied ATC origin");
 need(get(bytes,20,4)==(bad_address?e.logical_address+4:e.logical_address)&&get(bytes,physical?40:32,4)==address,"logical FA and selected writeback address remain distinct");
 need(get(bytes,physical?18:16,2)==(0x80|sizebits|e.function_code),"only the selected writeback stage is valid");
 for(unsigned other=0;other<3;++other)if(other!=unsigned(slot))need(p.wb[other].status==0&&p.wb[other].address==0&&p.wb[other].data==0,"no pending writes are invented in other stages");
 const unsigned data_offset=physical?44:36;
 for(unsigned i=0;i<e.size;++i){const unsigned byte=physical?((address+i)&3):4-e.size+i;
  const unsigned expected=(e.value>>(8*(e.size-1-i)))&255;
  need(bytes[data_offset+byte]==(bad_alignment?(expected^1):expected),"manual byte-lane/register-alignment oracle");
 }
 ++models;
}
struct Bus {
 std::array<uae_u8,65536> ram{};
 std::vector<modern040::BusRequest> operands;
 std::vector<unsigned> values;
 static modern040::BusCompletion access(void*o,const modern040::BusRequest&q,uae_u32&v){
  auto&b=*static_cast<Bus*>(o);const bool operand=(q.address>=0x5000&&q.address<0x5040)||(q.address>=0x6000&&q.address<0x6040);
  // Synthetic logical alias represents an already-restored test mapping.
  // It is not an ATC, page-table walk or emulated guest-MMU integration.
  const auto physical=q.address>=0x6000&&q.address<0x6040?q.address-0x1000:q.address;
  if(operand){b.operands.push_back(q);b.values.push_back(v);}
  if(q.kind==modern040::AccessKind::Write){auto data=v;for(unsigned i=q.size;i;--i){b.ram.at(physical+i-1)=std::uint8_t(data);data>>=8;}}
  else{v=0;for(unsigned i=0;i<q.size;++i)v=(v<<8)|b.ram.at(physical+i);}
  return modern040::BusCompletion::Completed;
 }
 void word(unsigned a,unsigned v){ram.at(a)=v>>8;ram.at(a+1)=v;}
 void longword(unsigned a,unsigned v){word(a,v>>16);word(a+2,v);}
};
void guest_handler(const Evidence&e,unsigned ccr){
 using namespace modern040;Bus bus;reset();host.deliver_exceptions=host.fault_recovery=true;host.precise_bus=Bus::access;host.bus_opaque=&bus;
 regs.usp=0x7000;regs.isp=0x8fc4;regs.msp=0x8000;regs.regs[15]=regs.isp;regs.pc=0x200;regs.sr=0x2700;MakeFromSR();
 for(unsigned i=0;i<15;++i)regs.regs[i]=0x12340000+i*0x101;
 regs.dfc=7;regs.sfc=2;
 const auto before=architectural_state();const bool physical=e.origin==Origin::ExternalBus;
 const auto saved_sr=uae_u16((e.function_code==5?0x2500:0x500)|ccr);auto supplied=*single_completed_write(e);
 // Hardware does not promise zero in unused data lanes. Poison those bits
 // independently of the prototype's deterministic zero-fill choice.
 if(e.size<4){
  unsigned used=0;
  if(physical)for(unsigned i=0;i<e.size;++i)used|=255u<<(24-8*(((*e.physical_address&3)+i)&3));
  else used=(1u<<(8*e.size))-1;
  supplied.wb[physical?0:1].data|=0xa5a5a5a5u&~used;
 }
 const auto f=frame(supplied,saved_sr,0x102);
 for(unsigned i=0;i<f.size();++i)bus.ram[0x8fc4+i]=f[i];
 for(unsigned i=0;i<64;++i)bus.ram[0x5000+i]=0xa5;
 // Select one known valid stage. These are synthetic frame inputs, not a
 // fault injected through the core. Preserve all GPRs and original DFC.
 unsigned pc=0x200;const auto emit=[&](unsigned value){bus.word(pc,value);pc+=2;};
 for(unsigned i=0;i<3;++i){emit(0x23c0+i);emit(0);emit(0x4000+4*i);} // save D0/D1/D2
 emit(0x23c8);emit(0);emit(0x400c); // save A0
 emit(0x4e7a);emit(0x2001); // MOVEC DFC,D2
 emit(0x302f);emit(physical?18:16); // status -> D0
 emit(0x2200);emit(0x0281);emit(0);emit(7); // D1=TM
 emit(0x4e7b);emit(0x1001); // MOVEC D1,DFC
 emit(0x206f);emit(physical?40:32);emit(0x202f);emit(physical?44:36);
 if(physical){
  emit(0x2208);emit(0x0281);emit(0);emit(3); // D1=A0&3
  emit(0xe789); // LSL.L #3,D1
  emit(0xe3b8); // ROL.L D1,D0
  if(e.size==1){emit(0xe088);emit(0xe088);emit(0xe088);} // LSR.L #8 three times
  else if(e.size==2){emit(0xe088);emit(0xe088);}
 }
 emit(e.size==1?0x0e10:e.size==2?0x0e50:0x0e90);emit(0x0800); // MOVES D0,(A0)
 emit(0x426f);emit(physical?18:16); // clear the consumed WB status
 emit(0x4e7b);emit(0x2001); // restore original DFC
 for(unsigned i=0;i<3;++i){emit(0x2039+(i<<9));emit(0);emit(0x4000+4*i);}
 emit(0x2079);emit(0);emit(0x400c);emit(0x4e73);
 bus.word(0x100,0x4afc);bus.word(0x102,0x2048);
 for(unsigned steps=0;regs.pc!=0x102&&steps<80;++steps)need(run(1).exit==Exit::Budget,"genuine guest cleanup executes without fabricated exceptions");
 need(regs.pc==0x102,"actual RTE restores supplied completed PC without instruction replay");
 need(bus.operands.size()==(bad_count?2u:1u)&&bus.operands[0].kind==AccessKind::Write,"guest cleanup performs one target write and no target reads");
 const auto&q=bus.operands[0];need(q.address==(physical?*e.physical_address:e.logical_address)&&q.size==e.size&&q.function_code==e.function_code&&q.alternate_function_code==e.function_code&&!q.transfer_type,"guest MOVES preserves requested width, address and normalized FC");
 for(unsigned i=0;i<64;++i){const unsigned start=*e.physical_address-0x5000;const auto expected=i>=start&&i<start+e.size?std::uint8_t(e.value>>(8*(e.size-1-(i-start)))):std::uint8_t(0xa5);need(bus.ram[0x5000+i]==expected,"actual cleanup reconstructs data and preserves adjacent RAM bytes");}
 const auto after=architectural_state();for(unsigned i=0;i<15;++i)need(after.regs[i]==before.regs[i],"guest cleanup/RTE preserves every non-stack GPR");
 need(after.sr==saved_sr&&after.dfc==7&&after.sfc==2&&after.regs[15]==(e.function_code==5?0x9000u:0x7000u)&&after.isp==0x9000,"RTE restores CCR/mode/banks and cleanup preserves FC registers");
 need(host.exception_count==0&&run(1).exit==Exit::Budget&&regs.pc==0x104,"no repeated completed instruction or spurious exception after RTE");++handlers;
}
void rejection(){
 auto e=sample(Origin::ExternalBus,4,5,0,0x12345678);
 for(unsigned raw=0;raw<256;++raw)if(raw!=0x40&&raw!=0x41){e.origin=Origin(raw);need(bool(single_completed_write(e))==bad_origin,"unspecified/legacy completion values cannot select a writeback stage");}
 e=sample(Origin::ExternalBus,4,5,0,1);e.physical_address.reset();need(!single_completed_write(e),"external origin requires explicit resolved physical address");
 e=sample(Origin::ExternalBus,4,5,0,1);e.no_commit_certified=false;need(!single_completed_write(e),"unknown/partial transfer cannot enter this isolated profile");
 e.no_commit_certified=true;e.whole_scalar=false;need(!single_completed_write(e),"failed-fragment certification cannot stand in for the whole original write");
 e.whole_scalar=true;e.instruction_complete=false;need(!single_completed_write(e),"unfinished instruction cannot borrow completed-write PC semantics");
 e.instruction_complete=true;for(unsigned n:{0u,2u,3u,4u}){e.pending_writes=n;need(!single_completed_write(e),"prototype cannot invent a multiwrite queue");}
 e.pending_writes=1;for(unsigned n:{0u,3u,8u,16u}){e.size=n;need(!single_completed_write(e),"only scalar B/W/L sizes are modeled");}
 e.size=4;for(unsigned fc:{0u,2u,3u,4u,6u,7u}){e.function_code=fc;need(!single_completed_write(e),"alternate/program spaces require separate proof");}
 e.function_code=5;e.transfer_type=2;need(!single_completed_write(e),"unsupported transfer type remains outside prototype");
 e=sample(Origin::AtcTranslation,4,5,0,1);e.physical_address.reset();const auto logical=single_completed_write(e);
 need(logical&&logical->wb[1].address==e.logical_address&&logical->wb[0].status==0,"supplied ATC-origin layout does not invent a resolved physical address");
}
}
int main(int argc,char**argv){try{
 for(int i=1;i<argc;++i){std::string_view a=argv[i];if(a=="--negative-alignment")bad_alignment=true;else if(a=="--negative-address")bad_address=true;else if(a=="--negative-origin")bad_origin=true;else if(a=="--negative-count")bad_count=true;else throw std::runtime_error("bad argument");}
 modern040::install_handlers();modern040::install_restart_handlers();rejection();manual_literals();
 const unsigned values[]={0,1,0x7f,0x80,0xff,0x100,0x7fff,0x8000,0xffff,0x10000,0x7fffffff,0x80000000,0xffffffff,0x12345678,0xdeadbeef};
 for(auto origin:{Origin::ExternalBus,Origin::AtcTranslation})for(unsigned size:{1u,2u,4u})for(unsigned fc:{1u,5u})for(unsigned low=0;low<4;++low){
  for(unsigned value:values){auto e=sample(origin,size,fc,low,value);oracle(e);for(unsigned ccr=0;ccr<32;++ccr)guest_handler(e,ccr);}
  const unsigned limit=size==1?256:size==2?65536:4096;unsigned value=0x2468ace1;
  for(unsigned i=0;i<limit;++i){value=size==4?value*1664525u+1013904223u:i;oracle(sample(origin,size,fc,low,value));}
 }
 // CPU physical addresses are not backend device-decode indices. These are
 // frame-byte checks only; no high-address target is touched by the guest.
 auto high=sample(Origin::ExternalBus,2,5,2,0x5678);high.logical_address=0xfffffa42;high.physical_address=0xfffffa42;oracle(high);
 auto translated=sample(Origin::ExternalBus,4,1,1,0x12345678);translated.logical_address=0x87654001;translated.physical_address=0x12345001;oracle(translated);
 std::cout<<"PASS: "<<checks<<" layout/guest-handler assertions; models="<<models<<" synthetic-frames="<<handlers<<" (isolated prototype; no fault-origin/MMU integration)\n";
}catch(const std::exception&e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}}
