/* SPDX-License-Identifier: GPL-2.0-or-later
 * 68040 CPU state and host interface.
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
#define UVAL64(value) value##ULL
namespace modern040 {
struct State {
    uae_u32 regs[16]{};
    uae_u32 pc = 0x100, instruction_pc = 0x100, trace_pc = 0;
    uae_u32 usp = 0, isp = 0, msp = 0;
    uae_u16 sr = 0x2700, opcode = 0, ir = 0, irc = 0;
    uae_u16 read_buffer = 0;
    bool x = false, n = false, z = false, v = false, c = false;
    bool t1 = false, t0 = false, s = true, m = false;
    int intmask = 7, stopped = 0;
    uae_u32 vbr = 0, sfc = 0, dfc = 0, cacr = 0, caar = 0;
    uae_u32 itt0 = 0, itt1 = 0, dtt0 = 0, dtt1 = 0;
    uae_u32 tcr = 0, mmusr = 0, urp = 0, srp = 0, buscr = 0, pcr = 0;
    uae_u32 spcflags = 0;
    int ipl[2]{}, ipl_pin = 0;
    uae_u32 mmu_fault_addr = 0, mmu_effective_addr = 0, wb2_address = 0, wb3_data = 0;
    uae_u16 mmu_ssw = 0;
    uae_u8 wb2_status = 0, wb3_status = 0;
    // Canonical external-extended state.  This is intentionally stored as
    // architectural bits, not host long double, so NaNs and denormals survive
    // every C boundary and future x86/ARM backend unchanged.
    std::array<std::array<uae_u32,3>,8> fp{};
    uae_u32 fpcr = 0, fpsr = 0;
    uae_u32 fp_ea = 0, fp_opword = 0, fp_unimp_pend = 0, fp_exp_pend = 0, fpiar = 0, fpu_state = 0;
    bool fp_unimp_ins = false, fpu_exp_pre = false, fp_exception = false, fp_ea_set = false, fp_branch = false;
    bool operator==(const State &) const = default;
};
struct Preferences {
    const int cpu_model = 68040, mmu_model = 68040, fpu_model = 0, cachesize = 0;
    const int fpu_revision = 0;
    const bool compfpu = false, int_no_unimplemented = false;
};
inline constexpr Preferences currprefs;
inline constexpr bool m68k_accurate_ipl = false;
inline constexpr unsigned SPCFLAG_TRACE = 1, SPCFLAG_DOTRACE = 2,
    SPCFLAG_INT = 4, SPCFLAG_DOINT = 8, SPCFLAG_END_COMPILE = 16;
extern State regs;
enum class AccessKind { Fetch, Read, Write };
struct BusRequest {
    AccessKind kind;
    uae_u32 address;
    unsigned size, function_code;
    std::optional<unsigned> alternate_function_code{};
    unsigned transfer_type=0;
    bool operator==(const BusRequest &) const = default;
};
// One architectural access per callback. Read returns data in value; write
// supplies value. false means this access did not complete. No implicit retry.
using BusAccess = bool (*)(void *opaque,const BusRequest &,uae_u32 &value);
// False from the legacy callback does not certify a side-effect-free fault.
enum class BusCompletion { UnknownFault=0, Completed=1, FaultBeforeTransfer=2 };
using PreciseBusAccess = BusCompletion (*)(void *,const BusRequest &,uae_u32 &);
// Explicit per-request evidence. A completion code is not a pipeline origin.
enum class FaultOrigin : unsigned { Unspecified=0, ExternalBus=0x40, AtcTranslation=0x41 };
struct FaultEvidence {
    FaultOrigin origin=FaultOrigin::Unspecified;
    bool whole_scalar_no_commit=false;
    std::optional<uae_u32> cpu_physical_address;
    bool operator==(const FaultEvidence &) const = default;
};
struct BusOutcome {
    BusCompletion completion=BusCompletion::UnknownFault;
    std::optional<FaultEvidence> evidence;
};
using DetailedBusAccess = BusOutcome (*)(void *,const BusRequest &,uae_u32 &);
using ResetHardware = bool (*)(void *opaque);
struct Access {
    AccessKind kind;
    uae_u32 address;
    unsigned size;
    uae_u32 value;
    bool completed;
    bool operator==(const Access &) const = default;
};
enum class Exit { Budget, Stopped, GuestException, MemoryFault, Unsupported,
                  UnsupportedMmu, Reset, DoubleFault, HostService, HostReturn };
enum class ServiceKind { NatFeatId=1, NatFeatCall, EmulOp, EmulOpReturn, LegacyFpu };
struct Service { ServiceKind kind; uae_u16 opcode; uae_u32 pc, stack; bool supervisor, trace; };
struct Result {
    Exit exit;
    std::uint64_t instructions = 0;
    int vector = 0;
    uae_u32 instruction_pc = 0, current_pc = 0, address = 0;
    unsigned size = 0;
    std::string detail{};
};
struct Stop { Result result; };
struct Frame {
    int vector;
    unsigned format;
    uae_u32 stack, handler, stacked_pc;
    uae_u16 stacked_sr;
    bool interrupt;
};
struct CacheOperation { int cache,scope;uae_u32 address;bool push,invalidate; };
struct BusFault {
    BusRequest request;uae_u16 opcode;bool in_instruction;BusCompletion completion;
    uae_u32 data=0;bool rte_internal_load=false;
    bool detailed_profile=false,completed_producer=false;
    std::optional<FaultEvidence> evidence;
    bool restartable_read=false;
};
struct MmuFault {BusRequest request;uae_u32 data=0,status=0;};
struct PendingIuWrite { BusRequest request;uae_u32 data; };
struct PhysicalWriteback {
    uae_u16 status;uae_u32 address,data;
    std::optional<PendingIuWrite> iu_write{};
};
struct DeferredBitfieldWrite {
    BusFault primary;
    std::optional<PendingIuWrite> iu_write{};
};
enum class FrameStorageClass { Unknown, Ram, Disjoint };
struct FrameStorage { FrameStorageClass kind=FrameStorageClass::Unknown;std::uint64_t address=0; };
// Pure backend storage identity, never an architectural physical-address guess
// or another target read. RAM aliases must resolve to the same opaque key.
using FrameStorageIdentity=FrameStorage (*)(void*,const BusRequest&);
struct CompletedFrame {
    bool valid=false;
    std::uint64_t id=0,storage=0;
    uae_u32 stack=0,pc=0;
    uae_u16 ssw=0;
};
struct RteCompletion { std::uint64_t id;uae_u32 pc;bool ssw_loaded=false; };
struct Host {
    std::array<uae_u8, 65536> ram{};
    std::vector<Access> accesses;
    std::optional<Access> denied;
    std::uint64_t irq_checks = 0, stop_cycles = 0, cache_controls = 0;
    bool log_accesses = true;
    bool deliver_exceptions = false;
    bool in_instruction = false, rte_internal_load = false, access_fault_latched = false, halted = false;
    bool lc040_probes = false, capability_latched = false;
    BusAccess bus = nullptr;
    PreciseBusAccess precise_bus = nullptr;
    DetailedBusAccess detailed_bus = nullptr;
    bool physical_write_exceptions=false,physical_read_exceptions=false;
    bool bitfield_write_exceptions=false;
    std::optional<DeferredBitfieldWrite> deferred_bitfield;
    bool completed_frame_irq=false,completed_frame_overflow=false,completed_write_delivery=false,completed_rte_event=false;
    bool completed_irq_ready=false;
    FrameStorageIdentity frame_storage=nullptr;void*frame_storage_opaque=nullptr;
    std::array<CompletedFrame,32> completed_frames{};
    unsigned completed_frame_count=0;
    std::uint64_t completed_frame_serial=0;
    std::optional<uae_u32> rte_header_pc;
    std::optional<RteCompletion> rte_completion;
    // Present only during the corresponding actual format7 construction.
    std::optional<PhysicalWriteback> physical_writeback;
    void *bus_opaque = nullptr;
    ResetHardware reset_hardware=nullptr;
    void *reset_opaque=nullptr;
    bool private_operations=false, service_resuming=false;
    bool legacy_fpu_operations=false;
    bool modern_fpu_operations=false;
    bool read_probe_exceptions=false, delivering_read_probe=false;
    uae_u32 read_probe_next_pc=0;
    bool fault_recovery=false, using_restart=false, delivering_fault=false;
    uae_u32 start_pc=0,recovery_pc=0;
    unsigned initial_ccr=0;
    bool initial_t1=false,rte_trace_pending=false;
    uae_u32 rte_trace_pc=0;
    std::optional<BusFault> bus_fault;
    std::optional<MmuFault> mmu_fault;
    std::optional<Service> service;
    unsigned exception_depth = 0;
    std::vector<Frame> frames;
    std::uint64_t exception_count=0;
    std::optional<Frame> last_frame;
    std::size_t frame_history_limit=std::size_t(-1);
    std::vector<CacheOperation> cache_operations;
};
extern Host host;
void invalidate_completed_frames() noexcept;
void observe_frame_write(const BusRequest&);
extern bool ismoves_nommu;
extern bool mmu_ttr_enabled,mmu_ttr_enabled_ins,mmu_ttr_enabled_data;
extern uae_u8 mmu_cache_state;
int mmu_match_ttr(uae_u32,bool,bool);
using Handler = uae_u32 (*)(uae_u32);
using PrivateHandler = void (*)(uae_u32);
extern std::array<PrivateHandler,65536> private_handlers;
void install_private_handlers();
Result complete_service();
bool execute_modern_fpu(uae_u16 opcode);
bool execute_mmu_control(uae_u16 opcode);
void m68k_natfeat_id();
void m68k_natfeat_call();
void m68k_emulop(uae_u32);
void m68k_emulop_return();
bool cpureset();
extern std::array<Handler, 65536> handlers;
extern std::array<bool, 65536> legal;
extern std::array<bool, 65536> requires_lc040_probes;
extern std::array<bool,65536> legacy_fpu_opcodes;
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
#define M68K_GETPC m68k_getpc()
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
inline void put_byte(uae_u32 a, uae_u32 v) { put_byte_jit(a,v); }
inline void put_word(uae_u32 a, uae_u32 v) { put_word_jit(a,v); }
inline void put_long(uae_u32 a, uae_u32 v) { put_long_jit(a,v); }
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
void flush_cpu_caches_040(uae_u16);
void flush_cpu_caches_040_2(int,int,uae_u32,bool,bool);
void mmu_flush_cache();
void write_log(const TCHAR *, ...);
int m68k_move2c(int, uae_u32 *);
int m68k_movec2(int, uae_u32 *);
uae_u32 get_disp_ea_020(uae_u32, int);
void m68k_setstopped(int);
void do_cycles_stop(int);
void resume_stopped();
void Exception(int);
void ExceptionL(int, uae_u32);
void Exception_cpu_oldpc(int, uae_u32);
void ExceptionX(int, uae_u32, uae_u32);
void complete_exception_trace(int);
void op_illg(uae_u32);
void exception3_read_prefetch(uae_u32, uae_u32);
void exception3_read_prefetch_only(uae_u32, uae_u32);
void exception3_read_prefetch_68040bug(uae_u32, uae_u32, uae_u16);
void exception3_notinstruction(uae_u32, uae_u32);
void Exception_build_stack_frame(uae_u32, uae_u32, uae_u32, int, int);
void Exception_build_stack_frame_common(uae_u32, uae_u32, uae_u32, int, int);
uae_u32 exception_pc(int);
[[noreturn]] void unsupported_frame(int);
bool interrupt_ready(unsigned level) noexcept;
bool inject_interrupt(int vector, unsigned level);
inline constexpr int sz_byte = 0, sz_word = 1, sz_long = 2;
inline constexpr unsigned cpucycleunit = 1;
extern uae_u32 mmu040_move16[4];
extern uae_u32 last_addr_for_exception_3, last_fault_for_exception_3, last_op_for_exception_3;
extern bool last_writeaccess_for_exception_3, last_notinstruction_for_exception_3;
extern int last_fc_for_exception_3, last_size_for_exception_3, last_di_for_exception_3;
extern uae_u16 last_sr_for_exception3;
inline uae_u32 x_get_word(uae_u32 a) { return get_word_jit(a); }
inline uae_u32 x_get_byte(uae_u32 a) { return get_byte_jit(a); }
inline uae_u32 x_get_long(uae_u32 a) { return get_long_jit(a); }
inline void x_put_byte(uae_u32 a, uae_u32 v) { put_byte_jit(a,v); }
inline void x_put_word(uae_u32 a, uae_u32 v) { put_word_jit(a,v); }
inline void x_put_long(uae_u32 a, uae_u32 v) { put_long_jit(a,v); }
bool cpuboard_fc_check(uae_u32,uae_u32 *,int,bool);
inline void x_do_cycles(unsigned) {} // No CPU-cycle timing profile is implemented.
inline uae_u32 x_cp_next_iword() { return next_diword(); }
inline uae_u32 x_cp_next_ilong() { return next_dilong(); }
inline uae_u32 x_cp_get_disp_ea_020(uae_u32 base,int index) { return get_disp_ea_020(base,index); }
void fpuop_bcc(uae_u32,uaecptr,uae_u32);
void fpuop_save(uae_u32);
void fpuop_restore(uae_u32);
[[noreturn]] void unsupported_fpu_body();
void install_handlers();
void reset();
void poke(uae_u32 address, unsigned size, uae_u32 value);
State architectural_state();
Result run(std::uint64_t instruction_budget);
const char *exit_name(Exit);
}
