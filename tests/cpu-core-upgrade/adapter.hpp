/* SPDX-License-Identifier: GPL-2.0-or-later
 * Narrow synthetic host contract for generated integer handlers. Not a complete
 * regstruct, MMU, Atari machine, hardware bus or exception-frame implementation.
 */
#pragma once
#include <uae/types.h>
#include <array>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>
#define REGPARAM2

struct Registers {
    std::array<uae_u32, 16> r{};
    uae_u32 pc = 0x100;
    uae_u32 instruction_pc = 0x100;
    bool x = false, n = false, z = false, v = false, c = false;
    bool s = true, t0 = false;
    bool operator==(const Registers &) const = default;
};
enum class Kind { Fetch, Read, Write };
struct Access {
    Kind kind;
    uae_u32 address;
    unsigned size;
    uae_u32 value;
    bool completed = true;
    bool operator==(const Access &) const = default;
};
struct Fault {
    int vector;
    Kind kind;
    uae_u32 address;
    unsigned size;
    uae_u32 instruction_pc, current_pc;
    bool operator==(const Fault &) const = default;
};
struct NatCall {
    uae_u16 opcode;
    uae_u32 stack, argument;
    bool supervisor;
    bool operator==(const NatCall &) const = default;
};
struct Snapshot {
    Registers registers;
    std::array<uae_u8, 65536> ram{};
    std::vector<Access> accesses;
    std::optional<Fault> fault;
    std::vector<NatCall> calls;
    unsigned side_effect_reads = 0;
    unsigned trace_checks = 0;
    bool operator==(const Snapshot &) const = default;
};
extern Registers regs;
extern Snapshot machine;
using Handler = uae_u32 (*)(uae_u32);
extern std::array<Handler, 65536> handlers;
inline constexpr unsigned imm8_table[] = {8, 1, 2, 3, 4, 5, 6, 7};
inline constexpr unsigned areg_byteinc[] = {1, 1, 1, 1, 1, 1, 1, 2};
#define m68k_dreg(state, index) ((state).r[(index)])
#define m68k_areg(state, index) ((state).r[8 + (index)])
#define CLEAR_CZNV() do { regs.c = regs.z = regs.n = regs.v = false; } while (0)
#define SET_CFLG(value) (regs.c = bool(value))
#define SET_ZFLG(value) (regs.z = bool(value))
#define SET_NFLG(value) (regs.n = bool(value))
#define SET_VFLG(value) (regs.v = bool(value))
#define SET_XFLG(value) (regs.x = bool(value))
#define GET_XFLG() (regs.x)
#define GET_ZFLG() (regs.z)
#define COPY_CARRY() (regs.x = regs.c)
inline uae_u32 m68k_getpc() { return regs.pc; }
inline void m68k_incpc(unsigned amount) { regs.pc += amount; }
inline void m68k_setpc_j(uae_u32 pc) { regs.pc = pc; }
uae_u32 bus_read(Kind kind, uae_u32 address, unsigned size);
void bus_write(uae_u32 address, unsigned size, uae_u32 value);
inline uae_u32 get_byte_jit(uae_u32 address) { return bus_read(Kind::Read, address, 1); }
inline uae_u32 get_word_jit(uae_u32 address) { return bus_read(Kind::Read, address, 2); }
inline uae_u32 get_long_jit(uae_u32 address) { return bus_read(Kind::Read, address, 4); }
inline void put_byte_jit(uae_u32 address, uae_u32 value) { bus_write(address, 1, value); }
inline void put_word_jit(uae_u32 address, uae_u32 value) { bus_write(address, 2, value); }
inline void put_long_jit(uae_u32 address, uae_u32 value) { bus_write(address, 4, value); }
// Matches WinUAE include/newcpu.h m68k_do_rts ordering, but through this bus.
inline void m68k_do_rts() {
    const auto pc = get_long_jit(m68k_areg(regs, 7));
    // Upstream m68k_setpc (used by RTS) also updates instruction_pc;
    // m68k_setpc_j (used by JSR and rollback) deliberately does not.
    regs.instruction_pc = regs.pc = pc;
    m68k_areg(regs, 7) += 4;
}
inline void check_t0_trace() { ++machine.trace_checks; }
void exception3_read_prefetch(uae_u32 opcode, uae_u32 address);
void exception3_read_prefetch_only(uae_u32 opcode, uae_u32 address);
void install_generated_handlers();
void reset_machine();
void poke(uae_u32 address, unsigned size, uae_u32 value);
void deny_access(uae_u32 first, uae_u32 last, Kind kind);
bool step();
Snapshot snapshot();
std::string difference(const Snapshot &expected, const Snapshot &actual);
