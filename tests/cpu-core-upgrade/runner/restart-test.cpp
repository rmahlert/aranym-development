/* SPDX-License-Identifier: GPL-2.0-or-later
 * Real pinned instruction bodies, real guest handlers and real format7 RTE.
 * Synthetic physical-bus denial is not a claim of arbitrary MMIO atomicity.
 */
#include "restart.hpp"
#include "c-api.h"
#include <iostream>
#include <stdexcept>
#include <string_view>
using namespace modern040;
namespace {
unsigned checks=0;
bool wrong_pc=false,wrong_writeback=false;
void expect(bool p,const char *s){++checks;if(!p)throw std::runtime_error(s);}
struct Bus {
    std::array<uae_u8,65536> ram{};
    struct Event {BusRequest req;uae_u32 data;BusCompletion completion;};
    std::vector<Event> events;
    AccessKind denied_kind=AccessKind::Read;
    uae_u32 denied_address=0x500;
    bool repaired=false,unknown=false;
    void put(uae_u32 a,unsigned n,uae_u32 v){for(unsigned i=n;i;--i){ram[a+i-1]=uae_u8(v);v>>=8;}}
    uae_u32 peek(uae_u32 a,unsigned n)const{uae_u32 v=0;for(unsigned i=0;i<n;++i)v=(v<<8)|ram[a+i];return v;}
    static BusCompletion access(void *p,const BusRequest &r,uae_u32 &v){
        auto &b=*static_cast<Bus *>(p);auto done=BusCompletion::Completed;
        if((!b.repaired&&r.address==b.denied_address&&r.kind==b.denied_kind)||r.address>=b.ram.size()||r.size>b.ram.size()-r.address)
            done=b.unknown?BusCompletion::UnknownFault:BusCompletion::FaultBeforeTransfer;
        else if(r.kind==AccessKind::Write){b.put(r.address,r.size,v);if(r.address==0x4000)b.repaired=true;}
        else v=b.peek(r.address,r.size);
        b.events.push_back({r,v,done});return done;
    }
    unsigned count(AccessKind k,uae_u32 a,BusCompletion c)const{unsigned n=0;for(auto &e:events)if(e.req.kind==k&&e.req.address==a&&e.completion==c)++n;return n;}
};
void attach(Bus &b,unsigned sr=0x001f){
    reset();install_restart_handlers();host.fault_recovery=host.deliver_exceptions=true;
    host.precise_bus=Bus::access;host.bus_opaque=&b;
    regs.regs[15]=regs.isp=0x9000;regs.usp=0x7000;regs.msp=0x8000;regs.sr=sr;MakeFromSR();
    b.put(8,4,0x200);b.put(0x200,2,0x4e73);
}
void code(Bus &b,uae_u32 a,std::initializer_list<unsigned> words){for(auto w:words){b.put(a,2,w);a+=2;}}
void mapping_handler(Bus &b){code(b,0x200,{0x13fc,1,0,0x4000,0x4e73});}
void read_restart(){
    for(unsigned size:{1u,2u,4u})for(unsigned mode:{0u,0x2000u,0x3000u})for(unsigned ccr=0;ccr<32;++ccr){
        Bus b;attach(b,mode|ccr);regs.regs[8]=0x500;regs.regs[1]=0x12345678;
        b.put(0x500,4,0x89abcdef);b.put(0x100,2,size==1?0x1210:size==2?0x3210:0x2210);mapping_handler(b);
        const auto initial=architectural_state();const auto f=run(1);const auto sp=regs.regs[15];
        expect(f.exit==Exit::Budget&&f.vector==2&&regs.pc==0x200,"read fault enters real bus handler");
        expect(b.peek(sp+2,4)==(wrong_pc?0x102u:0x100u)&&b.peek(sp+6,2)==0x7008,"read fault stacks restart PC and format7");
        expect(b.peek(sp,2)==(mode|ccr)&&b.peek(sp+12,2)==((size==1?0x20:size==2?0x40:0)|0x100|((mode&0x2000)?5:1)),"read frame retains SR, size, direction, privilege");
        expect(regs.regs[1]==0x12345678&&b.peek(sp+14,2)==0,"failed read leaves D1 and has no writeback");
        expect(b.count(AccessKind::Read,0x500,BusCompletion::FaultBeforeTransfer)==1,"failed read happens once before handler");
        expect(run(2).exit==Exit::Budget&&regs.pc==0x100&&architectural_state().sr==(mode|ccr),"guest repairs mapping and genuine RTE restores faulted PC/SR");
        expect(b.count(AccessKind::Read,0x500,BusCompletion::Completed)==0,"RTE itself performs no hidden operand replay");
        expect(run(1).exit==Exit::Budget&&regs.pc==0x102,"returned guest instruction retries and completes");
        expect(regs.regs[1]==(size==1?0x12345689u:size==2?0x123489abu:0x89abcdefu),"retried MOVE partial register semantics");
        expect(regs.regs[15]==initial.regs[15]&&regs.x==bool(ccr&16)&&regs.n&&!regs.z&&!regs.v&&!regs.c,"read restart restores correct banks and then real MOVE flags");
        expect(b.count(AccessKind::Read,0x500,BusCompletion::Completed)==1,"successful retry is exactly one guest-directed operand read");
    }
}
void writeback(){
    for(unsigned size:{1u,2u,4u})for(unsigned ccr=0;ccr<32;++ccr){
        Bus b;attach(b,ccr);b.denied_kind=AccessKind::Write;regs.regs[8]=0x500;regs.regs[0]=0x89abcdef;
        b.put(0x500,4,0x11223344);b.put(0x100,2,size==1?0x1080:size==2?0x3080:0x2080);
        // Real guest handler maps the destination, copies the queued WB3 value
        // to WB3A at its actual width, clears valid status, then RTE. Save A0.
        code(b,0x200,{0x13fc,1,0,0x4000,0x2f08,0x206f,24,
             size==1?0x10afu:size==2?0x30afu:0x20afu,size==1?35u:size==2?34u:32u,
             0x205f,0x426f,14,0x4e73});
        expect(run(1).vector==2&&regs.pc==0x200,"failed write delivers access error");const auto sp=regs.regs[15];
        expect(b.peek(sp+2,4)==0x102&&b.peek(sp+20,4)==0x500&&b.peek(sp+24,4)==0x500,"completed store stacks next PC and WB3 destination");
        expect(b.peek(sp+14,2)==(0x80|(size==1?0x20:size==2?0x40:0)|1)&&b.peek(sp+28,4)==((0x89abcdefu&(size==1?255:size==2?65535:0xffffffffu))^(wrong_writeback?1:0)),"writeback status and register-aligned data describe exact failed write");
        expect(b.peek(0x500,4)==0x11223344,"failed write was not silently performed");
        expect(run(7).exit==Exit::Budget&&regs.pc==0x102&&regs.regs[8]==0x500&&regs.regs[15]==0x7000,"real handler completes WB3 and RTE consumes60bytes without reexecuting store");
        expect(b.peek(0x500,size)==(0x89abcdefu&(size==1?255:size==2?65535:0xffffffffu)),"guest handler performs width-correct writeback");
        expect(b.count(AccessKind::Write,0x500,BusCompletion::Completed)==1&&b.count(AccessKind::Write,0x500,BusCompletion::FaultBeforeTransfer)==1,"one failed write and one software writeback, no implicit duplicate");
        expect(architectural_state().sr==((ccr&16)|8),"write fault preserves completed instruction flags through handler/RTE");
    }
}
void updates_and_movem(){
    // Modern recovery must supersede the old probe-only next-PC shortcut.
    // Otherwise a generic failed TST would be silently skipped by RTE.
    {
        Bus tst;attach(tst);host.read_probe_exceptions=true;regs.regs[8]=0x500;tst.put(0x500,1,0x80);tst.put(0x100,2,0x4a10);mapping_handler(tst);
        expect(run(1).vector==2&&tst.peek(regs.regs[15]+2,4)==0x100,"restart-aware TST stacks faulted PC even when legacy probe option is enabled");
        expect(run(2).exit==Exit::Budget&&regs.pc==0x100&&run(1).exit==Exit::Budget&&regs.pc==0x102,"guest RTE retries failed TST instead of skipping it");
        expect(regs.x&&regs.n&&!regs.z&&!regs.v&&!regs.c&&tst.count(AccessKind::Read,0x500,BusCompletion::Completed)==1,"retried TST has genuine flags and one successful operand read");
    }
    for(unsigned op:{0x1218u,0x1220u}){
        Bus b;attach(b);regs.regs[8]=op==0x1220?0x501:0x500;b.put(0x100,2,op);b.put(0x500,1,0x80);mapping_handler(b);
        auto before=regs.regs[8];expect(run(1).vector==2&&regs.regs[8]==before,"failed update read restores address-register fixup");
        expect(run(2).exit==Exit::Budget&&run(1).exit==Exit::Budget&&regs.regs[8]==(op==0x1220?0x500u:0x501u),"guest restart updates address once");
    }
    Bus b;attach(b);regs.regs[8]=0x500;b.denied_address=0x504;code(b,0x100,{0x4cd0,3});b.put(0x500,4,0x11112222);b.put(0x504,4,0x33334444);mapping_handler(b);
    expect(run(1).vector==2&&regs.regs[0]==0x11112222,"MOVEM fault retains prior completed register load");auto sp=regs.regs[15];
    expect((b.peek(sp+12,2)&0x1000)&&b.peek(sp+8,4)==0x500,"MOVEM format7 records CM and original calculated EA");
    expect(run(2).exit==Exit::Budget&&mmu040_movem==1&&run(1).exit==Exit::Budget,"RTE restores MOVEM continuation for genuine restarted body");
    expect(regs.regs[0]==0x11112222&&regs.regs[1]==0x33334444,"MOVEM restart completes remaining register results");
    expect(b.count(AccessKind::Read,0x500,BusCompletion::Completed)==2,"MOVEM architectural restart repeats prior operand only AFTER guest RTE");
}
void exact_peer_write_and_unknown(){
    Bus b;attach(b,0x2000);code(b,0x100,{0x31fc,3,0xfa42,0x33fc});
    expect(run(1).vector==2&&regs.pc==0x200,"exact peer31fc0003fa42 absent-device write reaches bus handler");auto sp=regs.regs[15];
    expect(b.peek(sp+2,4)==0x106&&b.peek(sp+12,2)==0x45&&b.peek(sp+14,2)==0xc5&&b.peek(sp+20,4)==0xfffffa42&&b.peek(sp+28,4)==3,"exact peer write frame has next PC, supervisor word SSW and WB3 value3");
    code(b,0x200,{0x4fef,60,0x4ef9,0,0x300});b.put(0x300,2,0x4e71);
    expect(run(2).exit==Exit::Budget&&regs.pc==0x300&&regs.regs[15]==0x9000,"actual discard-frame probe handler recovers without hardware write retry");
    expect(b.count(AccessKind::Write,0xfffffa42,BusCompletion::FaultBeforeTransfer)==1,"absent peripheral receives one attempted write only");
    Bus unknown;attach(unknown);unknown.unknown=true;regs.regs[8]=0x500;unknown.put(0x100,2,0x1210);
    expect(run(1).exit==Exit::MemoryFault&&host.frames.empty()&&run(1).exit==Exit::Unsupported,"unknown/partial bus failure remains terminal without replay");
}
void composite_and_nested(){
    // MOVE.L (A0)+,(A1)+: source read completes before a different EA faults.
    Bus b;attach(b);b.denied_kind=AccessKind::Write;b.denied_address=0x600;
    regs.regs[8]=0x500;regs.regs[9]=0x600;b.put(0x500,4,0x89abcdef);b.put(0x100,2,0x22d8);
    expect(run(1).vector==2&&regs.regs[8]==0x504&&regs.regs[9]==0x604,"two-EA write fault commits genuine source/destination postupdates");
    auto sp=regs.regs[15];expect(b.peek(sp+2,4)==0x102&&b.peek(sp+28,4)==0x89abcdef,"two-EA failure queues source value with next PC");
    expect(b.count(AccessKind::Read,0x500,BusCompletion::Completed)==1,"two-EA fault does not reread successful source");
    Bus rmw;attach(rmw,0x10);rmw.denied_kind=AccessKind::Write;regs.regs[8]=0x500;rmw.put(0x500,2,0x7fff);rmw.put(0x100,2,0x5258);
    expect(run(1).vector==2&&regs.regs[8]==0x502,"RMW write fault commits exactly one address update");sp=regs.regs[15];
    expect(rmw.peek(sp,2)==0x0a&&rmw.peek(sp+2,4)==0x102&&rmw.peek(sp+28,4)==0x8000,"RMW write fault saves completed ADDQ flags and writeback result");
    expect(rmw.count(AccessKind::Read,0x500,BusCompletion::Completed)==1,"RMW write fault has one completed read and no hidden retry");
    // Fault while stacking an access error must halt, not recurse/replay.
    reset();install_restart_handlers();host.fault_recovery=host.deliver_exceptions=true;
    regs.regs[15]=regs.isp=0x9000;regs.regs[8]=0x10000;poke(0x100,2,0x1210);host.denied=Access{AccessKind::Write,0x8ffc,4,0,false};
    expect(run(1).exit==Exit::DoubleFault&&host.halted&&run(1).exit==Exit::DoubleFault,"fault during access-error frame construction halts until reset");
}
int api_bus(void *opaque,const m040_bus_request *q,uint32_t *v){
    m040_state reentrant{};expect(m040_get_state(&reentrant)==M040_BUSY,"precise bus callback rejects state reentry");
    return int(Bus::access(opaque,{AccessKind(q->kind),q->address,q->size,q->function_code,q->has_alternate_function_code?std::optional<unsigned>(q->alternate_function_code):std::nullopt,q->transfer_type},*v));
}
void c_boundary_and_irq(){
    Bus b;mapping_handler(b);b.put(8,4,0x200);b.put(0x100,2,0x1210);b.put(0x500,1,0x80);b.put(28*4,4,0x300);
    const m040_options options{M040_ABI_VERSION,1,0,0};m040_state state{};m040_result result{};
    expect(m040_reset(&options)==0&&m040_fault_recovery(1)==0&&m040_set_precise_bus(api_bus,&b)==0&&m040_get_state(&state)==0,"C API selects precise bus/restart profile");
    state.sr=0;state.regs[15]=state.usp=0x7000;state.isp=0x9000;state.regs[8]=0x500;
    expect(m040_set_state(&state)==0&&m040_run(1,&result)==0&&result.vector==2&&result.current_pc==0x200,"C API publishes delivered user fault and handler PC");
    expect(m040_run(2,&result)==0&&result.current_pc==0x100,"C boundary executes real repair/RTE handler");
    int pending=0,accepted=0;expect(m040_restart_pending(&pending)==0&&pending,"C API exposes post-RTE restart window");
    expect(m040_interrupt(28,4,&accepted,&result)==0&&!accepted&&result.current_pc==0x100,"IRQ recognition deferred between RTE and faulted instruction");
    expect(m040_run(1,&result)==0&&result.current_pc==0x102&&m040_restart_pending(&pending)==0&&!pending,"one restarted instruction clears IRQ holdoff");
    expect(m040_interrupt(28,4,&accepted,&result)==0&&accepted&&result.current_pc==0x300,"pending IRQ can enter immediately after restarted instruction");
    expect(m040_restart_pending(nullptr)==M040_BAD_ARGUMENT&&m040_fault_recovery(2)==M040_BAD_ARGUMENT,"new API validates arguments");
    expect(m040_set_precise_bus([](void *,const m040_bus_request *,uint32_t *){return 7;},nullptr)==0&&m040_run(1,&result)==M040_HOST_ERROR,"invalid bus completion cannot invent successful data");
    expect(m040_run(1,&result)==0&&result.exit==M040_UNSUPPORTED,"host callback contract failure latches unsafe continuation");
}
void bounded_exception_history(){
    const m040_options options{M040_ABI_VERSION,1,0,0};m040_state s{};m040_result r{};m040_exception_event event{};
    expect(m040_reset(&options)==0&&m040_fault_recovery(1)==0&&m040_exception_history_limit(0)==0&&m040_get_state(&s)==0,"production profile selects zero retained exception history");
    s.sr=0x2000;s.regs[15]=s.isp=0x9000;s.usp=0x7000;
    expect(m040_set_state(&s)==0&&m040_poke(0x100,2,0x4e41)==0&&m040_poke(0x102,2,0x60fc)==0&&m040_poke(33*4,4,0x200)==0&&m040_poke(0x200,2,0x4e73)==0,"prepare actual TRAP/RTE/branch loop");
    expect(m040_run(30000,&r)==0&&r.exit==M040_BUDGET&&m040_get_exception_event(&event)==0&&event.count==10000&&event.vector==33&&event.stacked_pc==0x102,"ten thousand real traps retain exact total count and last event");
    expect(host.frames.empty()&&host.frames.capacity()==0&&m040_get_state(&s)==0&&s.pc==0x100&&s.regs[15]==0x9000,"production trap history allocates no growing frame vector and preserves guest stack/PC");
    expect(m040_exception_history_limit(8)==0&&m040_run(3000,&r)==0&&m040_get_exception_event(&event)==0&&event.count==11000&&host.frames.size()==8,"bounded history keeps only requested entries while count continues");
    expect(m040_exception_history_limit(0)==0&&host.frames.capacity()==0&&m040_get_exception_event(&event)==0&&event.count==11000&&event.vector==33,"dropping history frees storage without losing last diagnostic event");
    expect(m040_reset(&options)==0&&m040_fault_recovery(1)==0&&m040_exception_history_limit(0)==0&&m040_get_state(&s)==0,"reset bounded trace/exception test");
    s.sr=0xa000;s.regs[15]=s.isp=0x9000;
    expect(m040_set_state(&s)==0&&m040_poke(0x100,2,0x4afc)==0&&m040_poke(16,4,0x300)==0&&m040_poke(36,4,0x400)==0,"prepare traced illegal instruction with distinct handlers");
    expect(m040_run(1,&r)==0&&r.current_pc==0x300&&m040_get_exception_event(&event)==0&&event.count==1&&event.vector==4&&host.frames.empty(),"trace detection uses event counter, not retained history size; no spurious second exception");
    expect(m040_reset(&options)==0&&m040_fault_recovery(1)==0&&m040_get_state(&s)==0,"reset production cache diagnostic stress");
    s.sr=0x2000;s.regs[15]=s.isp=0x9000;
    expect(m040_set_state(&s)==0&&m040_poke(0x100,2,0xf4f8)==0&&m040_poke(0x102,2,0x60fc)==0&&m040_run(20000,&r)==0&&r.exit==M040_BUDGET&&host.cache_controls>=10000&&host.cache_operations.empty(),"real cache maintenance retains controls but allocates no disabled diagnostic history");
}
}
int main(int argc,char **argv){try{if(argc>2)throw std::runtime_error("bad mode");if(argc==2){wrong_pc=std::string_view(argv[1])=="--negative-frame-pc";wrong_writeback=std::string_view(argv[1])=="--negative-writeback";if(!wrong_pc&&!wrong_writeback)throw std::runtime_error("bad mode");}install_handlers();read_restart();writeback();updates_and_movem();exact_peer_write_and_unknown();composite_and_nested();c_boundary_and_irq();bounded_exception_history();std::cout<<"PASS: "<<checks<<" precise fault/frame/writeback/guest-RTE assertions\n";}catch(const std::exception &e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}}
