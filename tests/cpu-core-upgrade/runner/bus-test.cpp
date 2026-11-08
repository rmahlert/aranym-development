/* SPDX-License-Identifier: GPL-2.0-or-later
 * Explicit host bus contract tests; mock device is not Atari MMIO integration.
 */
#include "core.hpp"
#include "c-api.h"
#include <iostream>
#include <map>
#include <stdexcept>
#include <string_view>
using namespace modern040;
namespace {
unsigned checks=0;bool wrong_fc=false,wrong_null_destination=false;
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
// Exact first Windows failure opcode: MOVE.B(A0),D1, sourceA0=0.
// This tests precise failure metadata/order, NOT generic frame7 continuation.
void peer_null_read() {
    for(unsigned ccr=0;ccr<32;++ccr) {
        Bus bus;attach(bus,false);host.deliver_exceptions=true;host.read_probe_exceptions=true;
        regs.sr=ccr;MakeFromSR();regs.regs[1]=0x12345678;regs.regs[8]=0;
        bus.put(0x01000100,2,0x1210);bus.put(0,1,0xab);
        bus.denied=BusRequest{AccessKind::Read,0,1,1};
        const auto before=architectural_state();const auto result=run(1);const auto after=architectural_state();
        expect(result.exit==Exit::MemoryFault&&result.instructions==1&&result.vector==2&&result.instruction_pc==0x01000100&&result.current_pc==0x01000100&&result.address==0&&result.size==1,"peer pure byte read exposes exact opcode-PC/address/width fault boundary");
        expect(after.regs[1]==(wrong_null_destination?0x123456abu:0x12345678u),"failed peer MOVE leaves destination D1 untouched");
        for(unsigned i=0;i<16;++i)expect(after.regs[i]==before.regs[i],"source byte read fails before ANY D/A update");
        expect(after.pc==before.pc&&after.sr==before.sr&&after.usp==before.usp&&after.isp==before.isp&&after.msp==before.msp,"source read fails before PC/CCR/SR/bank changes");
        expect(host.frames.empty()&&host.access_fault_latched&&host.bus_fault&&host.bus_fault->opcode==0x1210&&host.bus_fault->in_instruction&&host.bus_fault->request==BusRequest{AccessKind::Read,0,1,1},"unqualified ordinary read cannot enter narrow firmware probe profile");
        expect(bus.events.size()==2&&bus.events[0].request==BusRequest{AccessKind::Fetch,0x01000100,2,2}&&bus.events[1].request==BusRequest{AccessKind::Read,0,1,1}&&!bus.events[1].completed,"one user opcode fetch and one noncompleted data read; no hidden access");
        const auto accesses=bus.events.size();expect(run(1).instructions==0&&bus.events.size()==accesses,"ordinary access fault is not automatically replayed");
    }
    for(unsigned ccr=0;ccr<32;++ccr) {
        Bus bus;attach(bus,false);regs.sr=ccr;MakeFromSR();regs.regs[12]=0x01001000;regs.regs[8]=0x76543210;regs.regs[1]=0x12345678;
        bus.put(0x01000100,2,0x206c);bus.put(0x01000102,2,0x0014);bus.put(0x01000104,2,0x1210);bus.put(0x01001014,4,0);
        bus.denied=BusRequest{AccessKind::Read,0,1,1};
        expect(run(1).exit==Exit::Budget&&regs.pc==0x01000104&&regs.regs[8]==0&&architectural_state().sr==ccr,"actual preceding MOVEA.L20(A4),A0 loads optional NULL without changing CCR");
        m040_state exported;expect(m040_get_state(&exported)==M040_OK&&m040_set_state(&exported)==M040_OK,"plain integration state export/import preserves previous MOVEA result");
        expect(run(1).exit==Exit::MemoryFault&&regs.pc==0x01000104&&regs.regs[8]==0&&regs.regs[1]==0x12345678&&architectural_state().sr==ccr,"next real MOVE.B faults at the expected PC after state roundtrip");
    }
    for(bool supervisor:{false,true})for(unsigned value:{0u,0x7fu,0x80u,0xffu}) {
        Bus bus;attach(bus,supervisor);regs.regs[1]=0x12345678;regs.regs[8]=supervisor?0:0x800;
        bus.put(0x01000100,2,0x1210);bus.put(regs.regs[8],1,value);
        expect(run(1).exit==Exit::Budget&&regs.pc==0x01000102&&regs.regs[1]==(0x12345600|value)&&regs.x&&regs.n==bool(value&0x80)&&regs.z==(value==0)&&!regs.v&&!regs.c,"successful peer MOVE uses original generated byte/CCR/partial-register semantics");
        expect(bus.events.size()==2&&bus.events.back().request.function_code==(supervisor?5u:1u),"same opcode retains supervisor/user data function code");
    }
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
    try {if(argc>2)throw std::runtime_error("bad mode");if(argc==2){wrong_fc=std::string_view(argv[1])=="--negative-fc";wrong_null_destination=std::string_view(argv[1])=="--negative-null-destination";if(!wrong_fc&&!wrong_null_destination)throw std::runtime_error("bad mode");}install_handlers();ordinary_and_device();moves_all_fc();fault_order();peer_null_read();real_frames_external_bus();std::cout<<"PASS: "<<checks<<" real bus/MOVES/function-code assertions\n";return 0;}
    catch(const std::exception &e){std::cerr<<"FAIL: "<<e.what()<<"\n";return (wrong_fc&&std::string_view(e.what())=="MOVES uses selected SFC/DFC with exact address/size/direction")||(wrong_null_destination&&std::string_view(e.what())=="failed peer MOVE leaves destination D1 untouched")?1:2;}
}
