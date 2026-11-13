/* SPDX-License-Identifier: GPL-2.0-or-later
 * Actual ACSIFDC::read_file body is extracted unchanged into the build.
 * Owned backing RAM/disk and a scripted DMA trigger replace controller decode.
 */
#include "c-api.h"
#include <array>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <vector>
#include <unistd.h>
using memptr=uint32_t;using uint8=unsigned char;
namespace {
unsigned checks=0,barriers=0;bool skip_barrier=false,no_dma=false;
const char*disk_name=nullptr;
void need(bool ok,const char*why){++checks;if(!ok)throw std::runtime_error(why);}
std::vector<uint8> ram(0x01001000);
uint8*Atari2HostAddr(memptr address){need(address<ram.size(),"DMA address belongs to owned backing RAM");return ram.data()+address;}
void modern040_memory_barrier(){++barriers;if(!skip_barrier)need(m040_frame_barrier()==M040_OK,"same-thread hardware barrier is allowed inside the active CPU bus callback");}
struct ACSIFDC{bool read_file(int,long,memptr,int,int);};
#include "frame-fdc-copy.inc"
constexpr unsigned dma_start=0x00ffff00,port=0x00fed380,stack_top=0x01000080;
unsigned failed=0,dma_calls=0,frame_sp=0,expected_pc=0;bool changed=false;FILE*disk=nullptr;
void put(unsigned a,unsigned n,unsigned value){for(unsigned i=n;i;--i){ram.at(a+i-1)=value;value>>=8;}}
unsigned get(unsigned a,unsigned n){unsigned value=0;for(unsigned i=0;i<n;++i)value=(value<<8)|ram.at(a+i);return value;}
int storage(void*,const m040_bus_request*q,uint64_t*key){if(q->address==port&&q->size==4)return M040_FRAME_DISJOINT;if(uint64_t(q->address)+q->size>ram.size())return M040_FRAME_UNKNOWN;*key=q->address;return M040_FRAME_RAM;}
int access(void*,const m040_bus_request*q,uint32_t*v,m040_bus_detail*d){
 if(q->kind==M040_WRITE&&q->address==0x5000&&!failed++){d->completion=M040_BUS_FAULT_BEFORE_TRANSFER;d->origin=M040_ORIGIN_EXTERNAL_BUS;d->whole_scalar_no_commit=1;d->has_cpu_physical_address=1;d->cpu_physical_address=0x5000;return 1;}
 if(q->kind==M040_WRITE&&q->address==port){
  if(!no_dma){ACSIFDC fdc;need(fdc.read_file(fileno(disk),0,dma_start,512,1),"actual FDC copy reads first owned sector");++dma_calls;changed=get(frame_sp+2,4)!=expected_pc;need(fdc.read_file(fileno(disk),512,dma_start,512,1),"actual FDC copy reads restoring owned sector");++dma_calls;need(get(frame_sp+2,4)==expected_pc,"second DMA restores original architectural header bytes");}
 }else if(q->kind==M040_WRITE)put(q->address,q->size,*v);else *v=get(q->address,q->size);
 d->completion=M040_BUS_COMPLETED;return 1;
}
void emit(unsigned&pc,unsigned word){put(pc,2,word);pc+=2;}
void run(){
 const m040_options options{M040_ABI_VERSION,1,0,0};need(m040_reset(&options)==M040_OK&&m040_fault_recovery(1)==M040_OK&&m040_set_detailed_bus(access,nullptr)==M040_OK&&m040_physical_write_exceptions(1)==M040_OK&&m040_set_frame_storage(storage,nullptr)==M040_OK&&m040_completed_frame_irq(1)==M040_OK,"attach actual core and explicit owned RAM identity");
 m040_state state{};need(m040_get_state(&state)==M040_OK,"read initial state");state.sr=0x2500;state.pc=state.instruction_pc=0x100;state.usp=0x7000;state.isp=state.regs[15]=stack_top;state.msp=0x8000;state.regs[0]=0x12345678;state.regs[8]=0x5000;need(m040_set_state(&state)==M040_OK,"install FastRAM stack immediately above16MiB");
 put(8,4,0x200);put(69*4,4,0x800);put(0x800,2,0x4e73);put(0x100,2,0x2080);put(0x102,2,0x2048);
 unsigned pc=0x200;for(unsigned word:{0x23fcu,0u,1u,0x00feu,0xd380u,0x206fu,40u,0x20afu,44u,0x426fu,18u,0x4e73u})emit(pc,word);
 m040_result result{};need(m040_run(1,&result)==M040_OK&&result.exit==M040_BUDGET&&result.vector==2,"actual completed write constructs format7 on the FastRAM stack");need(m040_get_state(&state)==M040_OK,"read actual frame stack");frame_sp=state.regs[15];expected_pc=get(frame_sp+2,4);
 need(frame_sp==stack_top-60&&expected_pc==0x102&&dma_start<0x01000000&&dma_start+512>frame_sp+60,"real DMA copy range crosses16MiB and overlaps full actual frame");
 std::vector<uint8> original(ram.begin()+dma_start,ram.begin()+dma_start+512),corrupt=original;corrupt.at(frame_sp+5-dma_start)^=2;
 need(disk_name!=nullptr,"runner provides a private DMA scratch path");disk=std::fopen(disk_name,"w+b");need(disk!=nullptr&&std::fwrite(corrupt.data(),1,512,disk)==512&&std::fwrite(original.data(),1,512,disk)==512&&std::fflush(disk)==0,"create owned two-sector DMA input");
 for(unsigned i=0;state.pc!=0x102&&i<20;++i){need(m040_run(1,&result)==M040_OK&&result.exit==M040_BUDGET,"actual DMA trigger, WB1 cleanup and RTE execute");need(m040_get_state(&state)==M040_OK,"read guest continuation");}
 need(state.pc==0x102&&state.regs[15]==stack_top&&get(0x5000,4)==0x12345678,"software cleanup and RTE preserve completed data and continuation");
 int pending=0,accepted=0;need(m040_restart_pending(&pending)==M040_OK&&m040_interrupt(69,6,&accepted,&result)==M040_OK,"query actual postRTE IRQ state");
 if(no_dma)need(!pending&&accepted,"control without DMA keeps qualified immediate IRQ completion");
 else{need(dma_calls==2&&barriers==2&&changed,"both actual FDC copy bodies changed and restored the live header");need(pending&&!accepted,"DMA overwrite must retain conservative holdoff even when final bytes match");}
 need(std::fclose(disk)==0,"close owned DMA disk");disk=nullptr;need(std::remove(disk_name)==0,"remove owned private DMA scratch file");
 std::cout<<"PASS: "<<checks<<" actual-FDC-body DMA assertions; copies="<<dma_calls<<" barriers="<<barriers<<" cross16MiB=1 control="<<no_dma<<"\n";
}
}
int main(int argc,char**argv){try{for(int i=1;i<argc;++i){const std::string_view arg=argv[i];if(arg=="--negative-barrier")skip_barrier=true;else if(arg=="--no-dma")no_dma=true;else if(arg=="--disk"&&i+1<argc)disk_name=argv[++i];else throw std::runtime_error("bad mode");}run();}catch(const std::exception&e){if(disk)std::fclose(disk);if(disk_name)std::remove(disk_name);std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}}
