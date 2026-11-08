/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "core.hpp"
#include <limits>
namespace modern040 {
State regs;
Host host;
std::array<Handler, 65536> handlers{};
std::array<bool, 65536> legal{};
int movem_index1[256], movem_index2[256], movem_next[256];
namespace {
[[noreturn]] void stop(Exit exit, int vector, uae_u32 address, unsigned size, const char *detail) {
    throw Stop{{exit, 0, vector, regs.instruction_pc, regs.pc, address, size, detail}};
}
bool valid(AccessKind kind, uae_u32 address, unsigned size) {
    if (address >= host.ram.size() || size > host.ram.size() - address) return false;
    if (host.denied && kind == host.denied->kind) {
        const auto first = host.denied->address;
        const auto end = std::uint64_t(first) + host.denied->size;
        if (address < end && std::uint64_t(address) + size > first) return false;
    }
    return true;
}
bool translation_enabled() { return (regs.tcr | regs.itt0 | regs.itt1 | regs.dtt0 | regs.dtt1) & 0x8000; }
}
uae_u32 read(AccessKind kind, uae_u32 address, unsigned size) {
    if (!valid(kind, address, size)) {
        if (host.log_accesses) host.accesses.push_back({kind, address, size, 0, false});
        stop(Exit::MemoryFault, 2, address, size, "flat RAM read/fetch fault; no guest frame delivered");
    }
    uae_u32 value = 0;
    for (unsigned i = 0; i < size; ++i) value = (value << 8) | host.ram[address + i];
    if (host.log_accesses) host.accesses.push_back({kind, address, size, value, true});
    return value;
}
void poke(uae_u32 address, unsigned size, uae_u32 value) {
    if (address >= host.ram.size() || size > host.ram.size() - address) throw std::out_of_range("runner RAM initialization");
    for (unsigned i = size; i > 0; --i) { host.ram[address + i - 1] = value & 255; value >>= 8; }
}
void write(uae_u32 address, unsigned size, uae_u32 value) {
    if (size < 4) value &= (uae_u32{1} << (size * 8)) - 1;
    const bool ok = valid(AccessKind::Write, address, size);
    if (host.log_accesses) host.accesses.push_back({AccessKind::Write, address, size, value, ok});
    if (!ok) stop(Exit::MemoryFault, 2, address, size, "flat RAM write fault; no guest frame delivered");
    poke(address, size, value);
}
void reset() {
    regs = State{}; host = Host{};
    for (int i = 0; i < 256; ++i) {
        int bit = 0;
        while (bit < 8 && !(i & (1 << bit))) ++bit;
        movem_index1[i] = bit; movem_index2[i] = 7 - bit; movem_next[i] = i & ~(1 << bit);
    }
}
void checkint() { ++host.irq_checks; }
void mmu_set_super(bool) { stop(Exit::UnsupportedMmu, 0, regs.pc, 0, "MMU privilege adapter unavailable"); }
void set_cpu_caches(bool) { ++host.cache_controls; }
uae_u32 mmu_set_tc(uae_u32) { stop(Exit::UnsupportedMmu, 0, regs.pc, 0, "MMU translation unavailable"); }
void mmu_tt_modified() { ++host.cache_controls; }
void flush_icache(int) { ++host.cache_controls; }
void write_log(const TCHAR *, ...) {}
void m68k_setstopped(int type) { if (!regs.stopped) regs.stopped = type; }
void do_cycles_stop(int cycles) { host.stop_cycles += cycles; }
void resume_stopped() {
    if (regs.stopped == 1) regs.pc += 4;
    else if (regs.stopped == 2) regs.pc += 6;
    regs.stopped = 0;
}
[[noreturn]] void Exception(int nr) { stop(Exit::GuestException, nr, regs.pc, 0, "terminal guest exception; frame delivery not enabled"); }
[[noreturn]] void Exception_cpu_oldpc(int nr, uae_u32) { Exception(nr); }
[[noreturn]] void op_illg(uae_u32 opcode) { Exception((opcode & 0xf000) == 0xa000 ? 10 : (opcode & 0xf000) == 0xf000 ? 11 : 4); }
[[noreturn]] void exception3_read_prefetch(uae_u32, uae_u32 address) {
    stop(Exit::GuestException, 3, address, 2, "address error; frame delivery not enabled");
}
[[noreturn]] void exception3_read_prefetch_only(uae_u32 opcode, uae_u32 address) { exception3_read_prefetch(opcode, address); }
[[noreturn]] void exception3_read_prefetch_68040bug(uae_u32 opcode, uae_u32 address, uae_u16) { exception3_read_prefetch(opcode, address); }
State architectural_state() {
    MakeSR();
    auto state = regs;
    if (!state.s) state.usp = state.regs[15];
    else if (state.m) state.msp = state.regs[15];
    else state.isp = state.regs[15];
    return state;
}
Result run(std::uint64_t budget) {
    std::uint64_t instructions = 0;
    try {
        if (regs.stopped) return {Exit::Stopped, 0};
        if (translation_enabled()) stop(Exit::UnsupportedMmu, 0, regs.pc, 0, "cannot resume while unsupported MMU/translation is enabled");
        while (instructions < budget) {
            regs.instruction_pc = regs.pc;
            regs.trace_pc = regs.pc;
            if (regs.pc & 1) exception3_read_prefetch(0, regs.pc);
            regs.opcode = read(AccessKind::Fetch, regs.pc, 2);
            ++instructions;
            const auto handler = handlers[regs.opcode];
            if (!handler) {
                if (!legal[regs.opcode]) op_illg(regs.opcode);
                stop(Exit::Unsupported, 0, regs.pc, 2, "legal instruction excluded by current helper/profile inventory");
            }
            const auto trace_before = regs.t1;
            handler(regs.opcode);
            if (translation_enabled())
                stop(Exit::UnsupportedMmu, 0, regs.pc, 0, "MMU/transparent translation enabled by control write");
            if (regs.stopped) return {Exit::Stopped, instructions, 0, regs.instruction_pc, regs.pc};
            if (regs.spcflags & SPCFLAG_DOTRACE) Exception(9);
            if (trace_before) { regs.spcflags |= SPCFLAG_DOTRACE; Exception(9); }
        }
        return {Exit::Budget, instructions, 0, regs.instruction_pc, regs.pc};
    } catch (const Stop &event) {
        auto result = event.result; result.instructions = instructions; return result;
    }
}
const char *exit_name(Exit exit) {
    switch (exit) {
    case Exit::Budget: return "budget"; case Exit::Stopped: return "stopped";
    case Exit::GuestException: return "guest_exception"; case Exit::MemoryFault: return "memory_fault";
    case Exit::Unsupported: return "unsupported"; case Exit::UnsupportedMmu: return "unsupported_mmu";
    case Exit::Reset: return "reset"; case Exit::DoubleFault: return "double_fault";
    }
    return "invalid";
}
}
