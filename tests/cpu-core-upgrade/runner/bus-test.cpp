/* SPDX-License-Identifier: GPL-2.0-or-later
 * Explicit host bus contract tests; mock device is not Atari MMIO integration.
 */
#include "core.hpp"
#include <iostream>
#include <map>
#include <stdexcept>
#include <string_view>
using namespace modern040;
namespace {
unsigned checks=0;bool wrong_fc=false;
void expect(bool pass,const char *message) {++checks;if(!pass)throw std::runtime_error(message);}
struct Bus {
    std::map<uae_u32,uae_u8> bytes;
    struct Event {BusRequest request;uae_u32 value;bool completed;};
    std::vector<Event> events;
    std::optional<BusRequest> denied;
    uae_u32 side_effect_address=0xff000100;
    unsigned side_effect_reads=0;
    void put(uae_u32 address,unsigned size,uae_u32 value) {for(unsigned i=size;i>0;--i){bytes[address+i-1]=uae_u8(value);value>>=8;}}
    uae_u32 peek(uae_u32 address,unsigned size) const {uae_u32 value=0;for(unsigned i=0;i<size;++i){auto it=bytes.find(address+i);if(it==bytes.end())throw std::runtime_error("missing mock bytes");value=(value<<8)|it->second;}return value;}
    static bool access(void *opaque,const BusRequest &request,uae_u32 &value) {
        auto &bus=*static_cast<Bus *>(opaque);bool ok=!(bus.denied&&request==*bus.denied);
        for(unsigned i=0;i<request.size;++i)if(!bus.bytes.contains(request.address+i))ok=false;
        if(ok&&request.kind==AccessKind::Write)bus.put(request.address,request.size,value);
        else if(ok){value=bus.peek(request.address,request.size);if(request.kind==AccessKind::Read&&request.address==bus.side_effect_address)++bus.side_effect_reads;}
        bus.events.push_back({request,value,ok});return ok;
    }
};
void attach(Bus &bus,bool supervisor=true) {
    reset();host.bus=Bus::access;host.bus_opaque=&bus;regs.pc=regs.instruction_pc=0x01000100;
    regs.isp=regs.regs[15]=0x01009000;regs.usp=0x01007000;regs.sr=supervisor?0x271f:0x071f;MakeFromSR();
}
void ordinary_and_device() {
    for(bool supervisor:{false,true}) {
        Bus bus;attach(bus,supervisor);regs.regs[8]=bus.side_effect_address;
        bus.put(0x01000100,2,0x2010);bus.put(bus.side_effect_address,4,0x89abcdef);
        expect(run(1).exit==Exit::Budget&&regs.regs[0]==0x89abcdef&&regs.pc==0x01000102,"external bus executes real MOVE from high MMIO address");
        expect(bus.events.size()==2&&bus.events[0].request==BusRequest{AccessKind::Fetch,0x01000100,2,unsigned((supervisor?4:0)|2)}&&bus.events[1].request==BusRequest{AccessKind::Read,bus.side_effect_address,4,unsigned((supervisor?4:0)|1)},"ordinary bus distinguishes program/data and supervisor/user FC");
        expect(bus.side_effect_reads==1,"side-effecting device receives exactly one successful read");
        bus.put(0x01000102,2,0x2080); // MOVE.L D0,(A0)
        expect(run(1).exit==Exit::Budget&&bus.events.back().request==BusRequest{AccessKind::Write,bus.side_effect_address,4,unsigned((supervisor?4:0)|1)},"external bus receives actual full-width write");
        expect(bus.side_effect_reads==1,"write causes no hidden pre-read");
    }
}
void moves_all_fc() {
    for(unsigned size:{1u,2u,4u})for(unsigned fc=0;fc<8;++fc)for(bool writing:{false,true})for(bool address_register:{false,true}) {
        Bus bus;attach(bus);regs.sfc=regs.dfc=fc;regs.regs[8]=0xff100000;regs.regs[1]=0x76543210;regs.regs[9]=0x76543210;
        bus.put(0x01000100,2,0x0e10|(size==1?0:size==2?0x40:0x80));bus.put(0x01000102,2,(address_register?0x9000:0x1000)|(writing?0x800:0));bus.put(0xff100000,4,0x89abcdef);
        expect(run(1).exit==Exit::Budget&&regs.pc==0x01000104&&!ismoves_nommu,"real MOVES helper executes and clears transient access state");
        const unsigned expected_tm[]={0,1,1,3,4,5,5,7},expected_tt[]={2,0,0,2,2,0,0,2};
        expect(bus.events.size()==3&&bus.events.back().request==BusRequest{writing?AccessKind::Write:AccessKind::Read,0xff100000,size,wrong_fc?(expected_tm[fc]^1):expected_tm[fc],fc,expected_tt[fc]},"MOVES uses selected SFC/DFC with exact address/size/direction");
        expect(bus.events[0].request.function_code==6&&bus.events[1].request.function_code==6,"MOVES opcode/extension stay supervisor program space");
        const auto value=size==1?0x89u:size==2?0x89abu:0x89abcdefu;
        const auto expected=address_register?(size==1?uae_u32(uae_s8(value)):size==2?uae_u32(uae_s16(value)):value):size==1?0x76543289u:size==2?0x765489abu:value;
        expect(writing?bus.peek(0xff100000,size)==(0x76543210u&(size==1?0xffu:size==2?0xffffu:0xffffffffu)):regs.regs[address_register?9:1]==expected,"MOVES destination width, sign extension and partial-register preservation");
        expect(architectural_state().sr==0x271f,"MOVES preserves every CCR bit");
    }
    Bus bus;attach(bus,false);bus.put(0x01000100,2,0x0e90);
    expect(run(1).exit==Exit::GuestException&&bus.events.size()==1,"user MOVES privilege precedes extension and data access");
    reset();poke(0x100,2,0x0e10);poke(0x102,2,0);regs.regs[8]=0x400;regs.sfc=7;
    expect(run(1).exit==Exit::Unsupported,"flat RAM must not invent CPU-space mapping");
}
void fault_order() {
    Bus bus;attach(bus);regs.regs[8]=0xff100004;regs.regs[0]=0x12345678;regs.dfc=5;
    bus.put(0x01000100,2,0x0ea0);bus.put(0x01000102,2,0x0800);bus.put(0xff100000,4,0xaabbccdd);
    bus.denied=BusRequest{AccessKind::Write,0xff100000,4,5,5};
    expect(run(1).exit==Exit::MemoryFault&&regs.regs[8]==0xff100000&&bus.peek(0xff100000,4)==0xaabbccdd,"real MOVES predecrement is visible before failed store, RAM unchanged");
    expect(!ismoves_nommu&&!bus.events.back().completed,"host unwind clears transient MOVES flag after precise failure");
    const auto count=bus.events.size();expect(run(1).instructions==0&&bus.events.size()==count,"failed external access cannot replay side effects without restart adapter");
    Bus second;attach(second);regs.regs[8]=second.side_effect_address;second.put(0x01000100,2,0x2010);second.put(second.side_effect_address,4,0xaabbccdd);
    second.denied=BusRequest{AccessKind::Read,second.side_effect_address,4,5};
    expect(run(1).exit==Exit::MemoryFault&&second.side_effect_reads==0,"failed callback is recorded once without completing device read");
}
void real_frames_external_bus() {
    Bus bus;attach(bus,false);host.deliver_exceptions=true;
    bus.put(0x01000100,2,0x4e4f);bus.put(0xbc,4,0x01000200);bus.put(0x01000200,2,0x4e73);
    for(unsigned i=0;i<8;++i)bus.put(0x01008ff8+i,1,0);
    expect(run(1).exit==Exit::Budget&&regs.pc==0x01000200&&regs.regs[15]==0x01008ff8,"user TRAP delivers genuine frame through attached bus");
    expect(bus.peek(0x01008ff8,2)==0x071f&&bus.peek(0x01008ffa,4)==0x01000102&&bus.peek(0x01008ffe,2)==0xbc,"external frame memory has independently expected SR/nextPC/vector");
    for(unsigned i=1;i<bus.events.size();++i)expect(bus.events[i].request.function_code==5,"frame stack writes and vector read use supervisor data FC");
    expect(run(1).exit==Exit::Budget&&regs.pc==0x01000102&&regs.regs[15]==0x01007000&&!regs.s,"actual RTE across external bus restores user bank/PC");
}
}
int main(int argc,char **argv) {
    try {if(argc>2||(argc==2&&std::string_view(argv[1])!="--negative-fc"))throw std::runtime_error("bad mode");wrong_fc=argc==2;install_handlers();ordinary_and_device();moves_all_fc();fault_order();real_frames_external_bus();std::cout<<"PASS: "<<checks<<" real bus/MOVES/function-code assertions\n";return 0;}
    catch(const std::exception &e){std::cerr<<"FAIL: "<<e.what()<<"\n";return wrong_fc&&std::string_view(e.what())=="MOVES uses selected SFC/DFC with exact address/size/direction"?1:2;}
}
