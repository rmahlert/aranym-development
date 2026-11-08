/* SPDX-License-Identifier: GPL-2.0-or-later
 * Expected frames derive from MC68040UM PDF pp47/48/234/235, consulted through
 * the Motorola-manual MCP and visually checked for table alignment. No legacy
 * emulator result is used as the architectural oracle.
 */
#include "core.hpp"
#include <iostream>
#include <stdexcept>
#include <string_view>
using namespace modern040;
namespace {
unsigned checks=0;
bool corrupt_m_before_rte=false, wrong_pc_expectation=false;
void expect(bool value,const char *message) { ++checks; if(!value) throw std::runtime_error(message); }
uae_u32 peek(uae_u32 address,unsigned size) {
    uae_u32 value=0; for(unsigned i=0;i<size;++i)value=(value<<8)|host.ram.at(address+i); return value;
}
void setup(uae_u16 sr) {
    reset(); host.deliver_exceptions=true;
    regs.regs[15]=regs.isp=0x9000; regs.usp=0x7000; regs.msp=0x8000;
    regs.sr=sr; MakeFromSR();
    for(unsigned vector=0;vector<256;++vector) poke(vector*4,4,0x200);
    poke(0x200,2,0x4e73); // actual generated RTE
}
void frame(uae_u32 stack,uae_u16 sr,uae_u32 pc,uae_u16 format_vector) {
    expect(peek(stack,2)==sr,"exact stacked SR");
    expect(peek(stack+2,4)==pc+(wrong_pc_expectation?2:0),"exact stacked PC");
    expect(peek(stack+6,2)==format_vector,"exact frame format/vector word");
}
void synchronous_and_rte() {
    for(uae_u16 sr:{0x0715,0x1715,0x2715,0x3715}) {
        setup(sr); poke(0x100,2,0x4e4f); // TRAP15
        const auto stack=(sr&0x1000)?0x8000u:0x9000u;
        const auto initial=architectural_state();
        expect(run(1).exit==Exit::Budget&&regs.pc==0x200,"TRAP enters actual vector within budget");
        frame(stack-8,sr,0x102,0xbc);
        expect(regs.s&&regs.m==bool(sr&0x1000)&&!regs.t0&&!regs.t1,"synchronous exception preserves M, sets S, clears tracing");
        expect(host.frames.size()==1&&host.frames[0].format==0&&!host.frames[0].interrupt,"one real synchronous format0 frame");
        expect(run(1).exit==Exit::Budget&&regs.pc==0x102,"actual generated RTE restores next PC");
        const auto after=architectural_state();
        expect(after.sr==sr&&after.usp==initial.usp&&after.isp==initial.isp&&after.msp==initial.msp,
               "RTE restores all banked stack pointers and SR");
        expect(after.regs[15]==initial.regs[15],"RTE restores active A7");
    }
    setup(0x3715); poke(0x100,2,0x4e4f);
    expect(run(1).exit==Exit::Budget&&regs.m,"master synchronous exception MUST retain M before RTE");
    poke(0x7ff8,2,0x2715); // A real handler requests return to ISP mode.
    if(corrupt_m_before_rte) regs.m=false; // Deliberate old-style bug, checker must reject.
    expect(run(1).exit==Exit::Budget&&regs.regs[15]==0x9000&&!regs.m,
           "master-frame RTE switching M1->M0 must load ISP, not leave MSP active");
    expect(architectural_state().msp==0x8000,"master-frame RTE saves consumed MSP");
}
void faults_with_real_recovery() {
    for(uae_u16 opcode:{0x4afc,0x40c0,0xa123,0xffff}) {
        const bool privilege=opcode==0x40c0;
        setup(privilege?0x0715:0x2715); poke(0x100,2,opcode); poke(0x102,2,0x4e71);
        // Actual guest handler: ADDQ.L #2,2(A7); RTE. It skips the faulted word.
        poke(0x200,2,0x54af); poke(0x202,2,2); poke(0x204,2,0x4e73);
        const auto vector=opcode==0x40c0?8:opcode==0xa123?10:opcode==0xffff?11:4;
        expect(run(1).exit==Exit::Budget&&regs.pc==0x200,"fault delivers handler instead of aborting host");
        frame(0x8ff8,privilege?0x0715:0x2715,0x100,uae_u16(vector*4));
        expect(run(2).exit==Exit::Budget&&regs.pc==0x102,"real handler adjusts stacked PC and RTE recovers");
        expect(run(1).exit==Exit::Budget&&regs.pc==0x104,"recovered guest executes following NOP");
        expect(regs.regs[15]==(privilege?0x7000u:0x9000u),"fault recovery restores correct active stack");
    }
}
void format2_and_trace() {
    setup(0xa715); poke(0x100,2,0x4e71); poke(0x102,2,0x4e71);
    expect(run(1).exit==Exit::Budget&&regs.pc==0x200,"T1 completes instruction then delivers trace");
    frame(0x8ff4,0xa715,0x102,0x2024);
    expect(peek(0x8ffc,4)==0x100,"trace format2 additional address is traced instruction");
    expect(run(1).exit==Exit::Budget&&regs.pc==0x102,"format2 RTE restores PC and consumes12 bytes");
    expect(regs.regs[15]==0x9000&&host.frames.size()==1,"RTE enabling trace does not immediately retrace itself");
    expect(run(1).exit==Exit::Budget&&host.frames.size()==2,"restored T1 traces the following instruction once");
    setup(0x2702); poke(0x100,2,0x4e76); // TRAPV, V already set.
    expect(run(1).exit==Exit::Budget&&regs.pc==0x200,"TRAPV reaches real format2 handler");
    frame(0x8ff4,0x2702,0x102,0x201c);
    expect(peek(0x8ffc,4)==0x100,"TRAPV format2 address is faulting instruction");
    setup(0xa700); poke(0x100,2,0x4e72); poke(0x102,2,0x2700);
    expect(run(1).exit==Exit::Budget&&regs.pc==0x200&&!regs.stopped,"pending trace resumes STOP before vectoring");
    frame(0x8ff4,0x2700,0x104,0x2024);
}
void master_interrupt_throwaway() {
    for(int vector:{27,64,192}) for(uae_u16 sr:{0x0015,0x1015,0x2015,0x3015}) {
        setup(sr); // All supervisor/user and latent-M modes, interrupt mask0.
        const auto original=architectural_state();
        expect(inject_interrupt(vector,3),"explicit interrupt request accepted independently of vector number");
        if(sr&0x1000) frame(0x7ff8,sr,0x100,uae_u16(vector*4));
        frame(0x8ff8,uae_u16((sr&0x1000)?sr|0x2000:sr),0x100,uae_u16(((sr&0x1000)?0x1000:0)|vector*4));
        expect(regs.s&&!regs.m&&regs.intmask==3&&regs.regs[15]==0x8ff8,"master interrupt switches to ISP with new mask");
        expect(run(1).exit==Exit::Budget&&regs.pc==0x100,"RTE loops through real throwaway and master frames");
        const auto state=architectural_state();
        expect(state.sr==sr&&state.regs[15]==original.regs[15]&&state.usp==original.usp&&state.msp==original.msp&&state.isp==original.isp,
               "format1 RTE restores master context and both consumed banks");
        expect(host.frames[0].stacked_sr==uae_u16((sr&0x1000)?sr|0x2000:sr),"interrupt frame metadata matches actual selected bank frame SR");
    }
    setup(0x2700);
    expect(!inject_interrupt(64,3)&&host.frames.empty(),"masked interrupt does not push a frame");
}
void errors_and_capability_gates() {
    setup(0x2700); poke(0x100,2,0x4e73);
    poke(0x9000,2,0x2700); poke(0x9002,4,0x300); poke(0x9006,2,0x5000);
    expect(run(1).exit==Exit::Budget,"invalid RTE frame reaches format-error vector");
    frame(0x8ff8,0x2700,0x100,0x38);
    setup(0x2700); poke(0x100,2,0x4e73);
    poke(0x9000,2,0x2700); poke(0x9002,4,0x300); poke(0x9006,2,0x7008);
    expect(run(1).exit==Exit::Unsupported&&regs.regs[15]==0x9000,"format7 continuation cannot silently skip an unimplemented contract");
    setup(0x2700); poke(0x100,2,0x4e4f); host.denied=Access{AccessKind::Write,0x8ff8,8,0,false};
    expect(run(1).exit==Exit::DoubleFault,"fault during frame construction halts explicitly");
    expect(run(10).exit==Exit::DoubleFault&&run(10).instructions==0,"double fault stays halted until reset");
    setup(0x2700); poke(0x100,2,0x2010); regs.regs[8]=0xffffffff;
    expect(run(1).exit==Exit::MemoryFault,"access-error continuation remains explicit unresolved gate");
    expect(run(1).exit==Exit::Unsupported&&run(1).instructions==0,"cannot retry partially committed access fault without restart support");
}
void frame7_layout_only() {
    // MCP manual PDF237/§8.4.6, visually checked. Inputs are synthetic;
    // actual access-error delivery/restart stays blocked and is NOT inferred.
    setup(0x2700); MakeSR();
    regs.mmu_effective_addr=0x11223344; regs.mmu_fault_addr=0x55667788;
    regs.wb3_data=0x99aabbcc; regs.wb2_address=0x1234abcd;
    regs.wb2_status=0xa5; regs.wb3_status=0x5a;
    for(unsigned i=0;i<4;++i) mmu040_move16[i]=0x10101010*(i+1);
    Exception_build_stack_frame(0x100,0x104,0x0145,2,7);
    const auto sp=regs.regs[15];
    expect(sp==0x9000-60,"format7 helper lays out exactly30 words");
    frame(sp,0x2700,0x104,0x7008);
    expect(peek(sp+8,4)==0x11223344&&peek(sp+12,2)==0x0145,"format7 EA/SSW offsets");
    expect(peek(sp+14,2)==0x5a&&peek(sp+16,2)==0xa5&&peek(sp+18,2)==0,"format7 WB status offsets");
    expect(peek(sp+20,4)==0x55667788&&peek(sp+24,4)==0x55667788,"format7 fault/WB3 address offsets");
    expect(peek(sp+28,4)==0x99aabbcc&&peek(sp+32,4)==0x1234abcd,"format7 WB3 data/WB2 address offsets");
    expect(peek(sp+36,4)==0&&peek(sp+40,4)==0,"format7 absent WB2 data/WB1 address");
    for(unsigned i=0;i<4;++i) expect(peek(sp+44+4*i,4)==0x10101010*(i+1),"format7 push-data longword offsets");
    expect(!regs.wb2_status&&!regs.wb3_status,"frame helper consumes pending WB status fields");
}
}
int main(int argc,char **argv) {
    const std::string_view mode=argc==2?argv[1]:"";
    try {
        if(argc>2||(argc==2&&mode!="--negative-m-clear"&&mode!="--negative-frame-pc")) throw std::runtime_error("unknown frame-test mode");
        corrupt_m_before_rte=mode=="--negative-m-clear"; wrong_pc_expectation=mode=="--negative-frame-pc";
        install_handlers(); synchronous_and_rte(); faults_with_real_recovery(); format2_and_trace();
        master_interrupt_throwaway(); errors_and_capability_gates(); frame7_layout_only();
        std::cout<<"PASS: "<<checks<<" manual-derived genuine frame/RTE/trace/bank/interrupt assertions\n";
        return 0; // Driver rejects unexpected acceptance in a negative run.
    } catch(const std::exception &error) {
        const std::string_view message=error.what();
        if((mode=="--negative-m-clear"&&message.find("M1->M0")!=std::string_view::npos)||
           (mode=="--negative-frame-pc"&&message=="exact stacked PC")) {
            std::cout<<"REJECTED deliberate "<<mode<<" by actual frame/RTE checker\n"; return 1;
        }
        std::cerr<<"FAIL: "<<error.what()<<'\n'; return 2;
    }
}
