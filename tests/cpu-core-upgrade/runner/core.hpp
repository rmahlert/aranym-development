/* SPDX-License-Identifier: GPL-2.0-or-later
 * Isolated 68040 integer runner host contract; no production emulator linkage.
 */
#pragma once
#include <uae/types.h>
#include <array>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>
#define REGPARAM2
#define REGPARAM3
#define REGPARAM
namespace modern040 {
struct State {
    uae_u32 regs[16]{};
    uae_u32 pc = 0x100, instruction_pc = 0x100, trace_pc = 0;
    uae_u32 usp = 0, isp = 0, msp = 0;
    uae_u16 sr = 0x2700, opcode = 0, ir = 0, irc = 0;
    bool x = false, n = false, z = false, v = false, c = false;
    bool t1 = false, t0 = false, s = true, m = false;
    int intmask = 7, stopped = 0;
    uae_u32 vbr = 0, sfc = 0, dfc = 0, cacr = 0, caar = 0;
    uae_u32 itt0 = 0, itt1 = 0, dtt0 = 0, dtt1 = 0;
    uae_u32 tcr = 0, mmusr = 0, urp = 0, srp = 0, buscr = 0, pcr = 0;
    uae_u32 spcflags = 0;
    int ipl[2]{}, ipl_pin = 0;
    bool operator==(const State &) const = default;
};
struct Preferences {
    const int cpu_model = 68040, mmu_model = 0, fpu_model = 0, cachesize = 0;
};
inline constexpr Preferences currprefs;
inline constexpr bool m68k_accurate_ipl = false;
inline constexpr unsigned SPCFLAG_TRACE = 1, SPCFLAG_DOTRACE = 2,
    SPCFLAG_INT = 4, SPCFLAG_DOINT = 8, SPCFLAG_END_COMPILE = 16;
extern State regs;
enum class AccessKind { Fetch, Read, Write };
struct Access {
    AccessKind kind;
    uae_u32 address;
    unsigned size;
    uae_u32 value;
    bool completed;
    bool operator==(const Access &) const = default;
};
enum class Exit { Budget, Stopped, GuestException, MemoryFault, Unsupported,
                  UnsupportedMmu, Reset, DoubleFault };
struct Result {
    Exit exit;
    std::uint64_t instructions = 0;
    int vector = 0;
    uae_u32 instruction_pc = 0, current_pc = 0, address = 0;
    unsigned size = 0;
    std::string detail{};
};
struct Stop { Result result; };
struct Host {
    std::array<uae_u8, 65536> ram{};
    std::vector<Access> accesses;
    std::optional<Access> denied;
    std::uint64_t irq_checks = 0, stop_cycles = 0, cache_controls = 0;
    bool log_accesses = true;
};
extern Host host;
using Handler = uae_u32 (*)(uae_u32);
extern std::array<Handler, 65536> handlers;
extern std::array<bool, 65536> legal;
extern const unsigned supported_families, supported_opcodes;
extern int movem_index1[256], movem_index2[256], movem_next[256];
inline constexpr int imm8_table[] = {8, 1, 2, 3, 4, 5, 6, 7};
inline constexpr int areg_byteinc[] = {1, 1, 1, 1, 1, 1, 1, 2};
#define m68k_dreg(state, index) ((state).regs[(index)])
#define m68k_areg(state, index) ((state).regs[8 + (index)])
#define CLEAR_CZNV() do { regs.c = regs.z = regs.n = regs.v = false; } while (0)
#define SET_CFLG(value) (regs.c = bool(value))
#define SET_ZFLG(value) (regs.z = bool(value))
#define SET_NFLG(value) (regs.n = bool(value))
#define SET_VFLG(value) (regs.v = bool(value))
#define SET_XFLG(value) (regs.x = bool(value))
#define GET_CFLG() (regs.c)
#define GET_ZFLG() (regs.z)
#define GET_NFLG() (regs.n)
#define GET_VFLG() (regs.v)
#define GET_XFLG() (regs.x)
#define COPY_CARRY() (regs.x = regs.c)
#define FLAGBIT_N 15
#define FLAGBIT_Z 14
#define FLAGBIT_C 8
#define FLAGBIT_V 0
#define FLAGVAL_N (1u << FLAGBIT_N)
#define FLAGVAL_Z (1u << FLAGBIT_Z)
#define FLAGVAL_C (1u << FLAGBIT_C)
#define FLAGVAL_V (1u << FLAGBIT_V)
#define GET_CZNV() ((uae_u32(regs.n) << FLAGBIT_N) | (uae_u32(regs.z) << FLAGBIT_Z) | (uae_u32(regs.c) << FLAGBIT_C) | (uae_u32(regs.v) << FLAGBIT_V))
#define SET_ALWAYS_CFLG(value) SET_CFLG(value)
#define SET_ALWAYS_NFLG(value) SET_NFLG(value)
inline uae_u32 m68k_getpc() { return regs.pc; }
inline void m68k_incpc(int amount) { regs.pc += uae_u32(amount); }
inline void m68k_incpci(int amount) { m68k_incpc(amount); }
inline void m68k_setpc_j(uae_u32 pc) { regs.pc = pc; }
inline void m68k_setpc(uae_u32 pc) { regs.pc = regs.instruction_pc = pc; }
inline void set_special(unsigned flags) { regs.spcflags |= flags; }
inline void unset_special(unsigned flags) { regs.spcflags &= ~flags; }
uae_u32 read(AccessKind kind, uae_u32 address, unsigned size);
void write(uae_u32 address, unsigned size, uae_u32 value);
inline uae_u32 get_byte_jit(uae_u32 a) { return read(AccessKind::Read, a, 1); }
inline uae_u32 get_word_jit(uae_u32 a) { return read(AccessKind::Read, a, 2); }
inline uae_u32 get_long_jit(uae_u32 a) { return read(AccessKind::Read, a, 4); }
inline uae_u32 get_byte(uae_u32 a) { return get_byte_jit(a); }
inline uae_u32 get_word(uae_u32 a) { return get_word_jit(a); }
inline uae_u32 get_long(uae_u32 a) { return get_long_jit(a); }
inline void put_byte_jit(uae_u32 a, uae_u32 v) { write(a, 1, v); }
inline void put_word_jit(uae_u32 a, uae_u32 v) { write(a, 2, v); }
inline void put_long_jit(uae_u32 a, uae_u32 v) { write(a, 4, v); }
inline uae_u32 get_dibyte(int offset) { return read(AccessKind::Fetch, regs.pc + offset + 1, 1); }
inline uae_u32 get_diword(int offset) { return read(AccessKind::Fetch, regs.pc + offset, 2); }
inline uae_u32 get_dilong(int offset) { return read(AccessKind::Fetch, regs.pc + offset, 4); }
inline uae_u32 next_diword() { const auto v = get_diword(0); m68k_incpc(2); return v; }
inline uae_u32 next_dilong() { const auto v = get_dilong(0); m68k_incpc(4); return v; }
inline void m68k_do_rts() { const auto pc = get_long(regs.regs[15]); m68k_setpc(pc); regs.regs[15] += 4; }
inline void m68k_do_bsr(uae_u32 oldpc, uae_s32 offset) { regs.regs[15] -= 4; put_long_jit(regs.regs[15], oldpc); m68k_incpc(offset); }
void MakeSR();
void MakeFromSR();
void MakeFromSR_T0();
void MakeFromSR_STOP();
void check_t0_trace();
void checkint();
void mmu_set_super(bool);
void set_cpu_caches(bool);
uae_u32 mmu_set_tc(uae_u32);
void mmu_tt_modified();
void flush_icache(int);
void write_log(const TCHAR *, ...);
int m68k_move2c(int, uae_u32 *);
int m68k_movec2(int, uae_u32 *);
uae_u32 get_disp_ea_020(uae_u32, int);
void m68k_setstopped(int);
void do_cycles_stop(int);
void resume_stopped();
[[noreturn]] void Exception(int);
[[noreturn]] void Exception_cpu_oldpc(int, uae_u32);
[[noreturn]] void op_illg(uae_u32);
[[noreturn]] void exception3_read_prefetch(uae_u32, uae_u32);
[[noreturn]] void exception3_read_prefetch_only(uae_u32, uae_u32);
[[noreturn]] void exception3_read_prefetch_68040bug(uae_u32, uae_u32, uae_u16);
void install_handlers();
void reset();
void poke(uae_u32 address, unsigned size, uae_u32 value);
State architectural_state();
Result run(std::uint64_t instruction_budget);
const char *exit_name(Exit);
}
