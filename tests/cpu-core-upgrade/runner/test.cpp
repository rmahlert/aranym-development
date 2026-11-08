/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "core.hpp"
#include <iostream>
#include <stdexcept>
using namespace modern040;
namespace {
unsigned checks = 0;
void expect(bool pass, const char *message) { ++checks; if (!pass) throw std::runtime_error(message); }
void word(uae_u32 address, uae_u16 value) { poke(address, 2, value); }
void stacks() { regs.usp = 0x7000; regs.isp = 0x9000; regs.msp = 0x8000; regs.regs[15] = 0x9000; }
void sr_roundtrip() {
    for (unsigned sr = 0; sr < 65536; ++sr) {
        reset(); stacks(); host.log_accesses = false;
        word(0x100, 0x46c0); // MOVE.W D0,SR: real generated privilege/bank helper.
        regs.regs[0] = sr;
        const auto result = run(1);
        const auto state = architectural_state();
        const auto sp = !(sr & 0x2000) ? 0x7000u : (sr & 0x1000) ? 0x8000u : 0x9000u;
        expect(result.exit == Exit::Budget && result.instructions == 1, "SR set must complete one instruction without premature trace");
        expect(state.sr == (sr & 0xf71f), "all SR encodings/reserved bits/CCR round trip");
        expect(state.regs[15] == sp && state.usp == 0x7000 && state.isp == 0x9000 && state.msp == 0x8000,
               "SR user/interrupt/master stack banking");
        expect(state.pc == 0x102 && state.regs[0] == sr, "SR PC/source preservation");
    }
    for (int oldbank = 0; oldbank < 3; ++oldbank) for (int newbank = 0; newbank < 3; ++newbank) {
        reset(); stacks();
        regs.sr = oldbank == 0 ? 0 : oldbank == 1 ? 0x2000 : 0x3000;
        MakeFromSR();
        regs.regs[15] += 0x40;
        regs.sr = newbank == 0 ? 0 : newbank == 1 ? 0x2000 : 0x3000;
        MakeFromSR();
        auto state = architectural_state();
        const uae_u32 expected[] = {0x7000u + (oldbank == 0 ? 0x40u : 0),
                                    0x9000u + (oldbank == 1 ? 0x40u : 0),
                                    0x8000u + (oldbank == 2 ? 0x40u : 0)};
        expect(state.usp == expected[0] && state.isp == expected[1] && state.msp == expected[2], "all bank transitions save outgoing A7");
        expect(state.regs[15] == expected[newbank], "all bank transitions load correct A7");
    }
}
void privilege_and_control() {
    for (uae_u16 opcode : {0x46c0, 0x40c0, 0x4e60, 0x4e68, 0x4e72, 0x4e73, 0x4e7a, 0x4e7b}) {
        reset(); stacks(); regs.sr = 0; MakeFromSR();
        word(0x100, opcode); word(0x102, 0x0801);
        auto before = regs;
        auto result = run(1);
        expect(result.exit == Exit::GuestException && result.vector == 8, "user privileged instruction must report vector8");
        expect(regs.regs[15] == before.regs[15] && regs.pc == 0x100 && regs.vbr == 0, "privilege failure must not change stack/PC/control");
    }
    for (int regno : {0, 1, 2, 3, 4, 5, 6, 7, 0x800, 0x801, 0x803, 0x804, 0x805, 0x806, 0x807}) {
        reset(); stacks();
        word(0x100, 0x4e7b); word(0x102, uae_u16(regno)); // MOVEC D0,control
        word(0x104, 0x4e7a); word(0x106, uae_u16(0x1000 | regno)); // MOVEC control,D1
        // Keep translation disabled while checking every legal control register.
        const uae_u32 input = (regno >= 3 && regno <= 7) ? 0x12344004 : 0xffffffff;
        regs.regs[0] = input;
        const auto result = run(2);
        const auto mask = regno < 2 ? 7u : regno == 2 ? 0x80008000u : regno == 3 ? 0xc000u :
                          regno >= 4 && regno <= 7 ? 0xffffe364u : 0xffffffffu;
        expect(result.exit == Exit::Budget && regs.pc == 0x108, "legal 040 MOVEC must execute");
        expect(regs.regs[1] == (input & mask), "MOVEC 040 masks/readback");
    }
    for (int regno : {8, 0x802, 0x808, 0xfff}) {
        reset(); stacks(); word(0x100, 0x4e7b); word(0x102, uae_u16(regno));
        auto result = run(1);
        expect(result.exit == Exit::GuestException && result.vector == 4 && regs.pc == 0x100,
               "invalid 040 MOVEC register must report illegal vector4");
    }
    reset(); stacks(); word(0x100, 0x4e7b); word(0x102, 3); regs.regs[0] = 0x8000;
    expect(run(1).exit == Exit::UnsupportedMmu && regs.tcr == 0x8000, "enabling translation must leave explicit unsupported exit");
    const auto access_count = host.accesses.size();
    expect(run(1).instructions == 0 && host.accesses.size() == access_count, "unsupported MMU state must prevent resumed instruction fetch");
}
void bounded_program() {
    reset(); stacks();
    const uae_u16 code[] = {0x7003, 0x7200, 0x5281, 0x51c8, 0xfffc, 0x41f9, 0, 0x0400, 0x2081, 0x4e72, 0x2700};
    for (unsigned i = 0; i < std::size(code); ++i) word(0x100 + 2*i, code[i]);
    auto first = run(2);
    expect(first.exit == Exit::Budget && first.instructions == 2 && regs.pc == 0x104, "budget must bound real decoded execution");
    auto rest = run(100);
    expect(rest.exit == Exit::Stopped && rest.instructions == 11, "loop/subroutine-free demo must stop in bounded execution");
    expect(regs.regs[0] == 0xffff && regs.regs[1] == 4 && read(AccessKind::Read, 0x400, 4) == 4, "real ADDQ/DBF/LEA/store sequence result");
    expect(regs.pc == 0x112 && regs.stopped == 1 && host.stop_cycles == 4, "040 STOP retains PC until resume");
    expect(run(100).instructions == 0, "stopped runner cannot execute more instructions");
    resume_stopped(); word(0x116, 0x4e71);
    expect(run(1).exit == Exit::Budget && regs.pc == 0x118, "STOP resume advances exactly four bytes");
}
void arithmetic_and_stack_dispatch() {
    for (unsigned bits : {8u,16u,32u}) {
        const auto modulus = std::uint64_t{1} << bits;
        const auto mask = uae_u32(modulus-1), sign = uae_u32(modulus/2);
        const uae_u32 values[] = {0,1,2,sign-1,sign,sign+1,mask-1,mask};
        const auto signed_value = [=](uae_u32 x) { return (x & sign) ? std::int64_t(x)-std::int64_t(modulus) : std::int64_t(x); };
        for (bool subtract : {false,true}) for (auto src : values) for (auto dst : values)
        for (unsigned ccr=0; ccr<32; ++ccr) {
            reset(); stacks(); host.log_accesses=false;
            regs.sr=uae_u16(0x2700|ccr); MakeFromSR();
            regs.regs[0]=(0x76543210 & ~mask)|src;
            const auto initial=(0xfedcba98 & ~mask)|dst; regs.regs[1]=initial;
            const auto opcode=uae_u16((subtract?0x9300:0xd300)|(bits==8?0:bits==16?0x40:0x80));
            word(0x100,opcode);
            const auto x=unsigned(bool(ccr&16));
            const auto wide=subtract?std::uint64_t(dst)-(std::uint64_t(src)+x):std::uint64_t(dst)+src+x;
            const auto result=uae_u32(wide)&mask;
            const bool carry=subtract?std::uint64_t(src)+x>dst:wide>=modulus;
            const auto math=subtract?signed_value(dst)-signed_value(src)-x:signed_value(dst)+signed_value(src)+x;
            const bool overflow=math < -std::int64_t(sign) || math>=std::int64_t(sign);
            const auto expected_ccr=(carry?17:0)|((result&sign)?8:0)|((ccr&4)&&!result?4:0)|(overflow?2:0);
            const auto exit=run(1);
            const auto state=architectural_state();
            expect(exit.exit==Exit::Budget && state.pc==0x102,"runner ADDX/SUBX execution budget and PC");
            expect(state.regs[1]==((initial&~mask)|result),"runner ADDX/SUBX result and upper bits");
            expect(state.sr==unsigned(0x2700|expected_ccr),"runner ADDX/SUBX independent carry/overflow/sticky-Z oracle");
            expect(state.regs[0]==((0x76543210&~mask)|src)&&state.regs[15]==0x9000,"runner arithmetic preserves source/stack");
        }
    }
    for (bool pre : {false,true}) for (bool denied : {false,true}) {
        reset(); stacks(); regs.regs[0]=0x12345678; regs.x=true;
        const auto address=pre?0x8ffeu:0x9000u;
        word(0x100,pre?0x1027:0x101f); poke(address,1,0x90);
        if (denied) host.denied=Access{AccessKind::Read,address,1,0,false};
        const auto result=run(1);
        expect(result.exit==(denied?Exit::MemoryFault:Exit::Budget),"runner A7 byte access/fault outcome");
        expect(regs.regs[15]==(denied?0x9000u:pre?0x8ffeu:0x9002u),"runner byte A7 stride/fault register-update ordering");
        expect(regs.regs[0]==(denied?0x12345678u:0x12345690u)&&regs.x,"runner byte result/extend preservation");
        expect(host.accesses.size()==2&&host.accesses[1]==Access{AccessKind::Read,address,1,denied?0u:0x90u,!denied},"runner exact A7 byte read exposure");
    }
    reset(); stacks(); regs.regs[8]=0x200;
    word(0x100,0x4e90); word(0x200,0x4e75);
    expect(run(2).exit==Exit::Budget&&regs.pc==0x102&&regs.instruction_pc==0x102&&regs.regs[15]==0x9000,
           "runner actual JSR/RTS helper PC/cursor/A7");
    const std::vector<Access> expected={{AccessKind::Fetch,0x100,2,0x4e90,true},{AccessKind::Write,0x8ffc,4,0x102,true},
        {AccessKind::Fetch,0x200,2,0x4e75,true},{AccessKind::Read,0x8ffc,4,0x102,true}};
    expect(host.accesses==expected,"runner exact JSR/RTS ordered return-stack accesses");
    reset(); stacks(); word(0x100,0x46c0); word(0x102,0x4e71); regs.regs[0]=0xa700;
    expect(run(1).exit==Exit::Budget,"setting T1 must defer trace until following instruction");
    const auto trace=run(1);
    expect(trace.exit==Exit::GuestException&&trace.vector==9&&regs.pc==0x104,"existing T1 traces next completed instruction once");
}
void memory_and_exits() {
    reset(); stacks(); word(0x100, 0x2080); regs.regs[0] = 0x89abcdef; regs.regs[8] = 0x401;
    expect(run(1).exit == Exit::Budget && host.ram[0x401] == 0x89 && host.ram[0x404] == 0xef, "big-endian unaligned 040 RAM store");
    expect(host.accesses.size() == 2 && host.accesses[1] == Access{AccessKind::Write,0x401,4,0x89abcdef,true}, "exact fetch/store ordering");
    reset(); stacks(); word(0x100, 0x2080); regs.regs[8] = 0xffff;
    auto fault = run(1);
    expect(fault.exit == Exit::MemoryFault && fault.vector == 2 && regs.pc == 0x100, "failed modern store retains pre-store PC");
    reset(); stacks(); word(0x100, 0x4e90); regs.regs[8] = 0x201;
    auto odd = run(1);
    expect(odd.exit == Exit::GuestException && odd.vector == 3 && regs.pc == 0x201 && regs.regs[15] == 0x9000,
           "modern odd JSR fails before stack write");
    reset(); stacks(); word(0x100, 0x4afc);
    expect(run(1).vector == 4, "illegal instruction explicit guest exception");
    reset(); stacks(); word(0x100, 0xa123);
    expect(run(1).vector == 10, "line A instruction explicit guest exception");
    reset(); stacks(); word(0x100, 0xf200);
    expect(run(1).exit == Exit::Unsupported, "FPU is an explicit unsupported profile, not fabricated emulation");
}
void conditions_and_indexed_addressing() {
    for (unsigned flags=0; flags<16; ++flags) for (unsigned cc=0; cc<16; ++cc) {
        reset(); stacks();
        const bool n=flags&8,z=flags&4,v=flags&2,c=flags&1;
        const bool expected[]={true,false,!c&&!z,c||z,!c,c,!z,z,!v,v,!n,n,n==v,n!=v,!z&&(n==v),z||(n!=v)};
        regs.sr=uae_u16(0x2710|flags); MakeFromSR(); regs.regs[0]=0x12345678;
        word(0x100,uae_u16(0x50c0|(cc<<8)));
        const auto result=run(1); const auto state=architectural_state();
        expect(result.exit==Exit::Budget&&state.pc==0x102,"all16 real Scc condition handlers complete");
        expect(state.regs[0]==(expected[cc]?0x123456ffu:0x12345600u),"portable condition helper packed-flag binding");
        expect(state.sr==unsigned(0x2710|flags),"Scc must preserve complete SR");
    }
    for (unsigned form=0; form<3; ++form) {
        reset(); stacks(); regs.regs[8]=0x400; regs.regs[1]=3; regs.x=true;
        word(0x100,0x2030); // MOVE.L indexed(A0,D1.L*2),D0
        word(0x102,form==0?0x1a04:form==1?0x1b20:0x1b22);
        if (form) word(0x104,0x10);
        if (form==2) { word(0x106,4); poke(0x416,4,0x600); }
        const auto address=form==0?0x40au:form==1?0x416u:0x604u;
        poke(address,4,0x89abcdef);
        const auto result=run(1);
        expect(result.exit==Exit::Budget&&regs.pc==0x104+2*form,"real020 brief/full/indirect EA extension consumption");
        expect(regs.regs[0]==0x89abcdef&&regs.n&&!regs.z&&regs.x,"indexed load/address/flag result");
        expect(host.accesses.back()==Access{AccessKind::Read,address,4,0x89abcdef,true},"indexed final read width/address");
        if (form==2) expect(host.accesses[4]==Access{AccessKind::Read,0x416,4,0x600,true},"full preindex indirect pointer read ordering");
    }
}
}
int main() {
    try {
        install_handlers();
        expect(supported_families > 16 && supported_opcodes > 2770, "runner must expand actual generated coverage");
        sr_roundtrip(); privilege_and_control(); bounded_program(); arithmetic_and_stack_dispatch();
        conditions_and_indexed_addressing(); memory_and_exits();
        std::cout << "PASS: " << checks << " runner state/banking/control/privilege/budget/memory assertions\n";
        return 0;
    } catch (const std::exception &error) { std::cerr << "FAIL: " << error.what() << '\n'; return 1; }
}
