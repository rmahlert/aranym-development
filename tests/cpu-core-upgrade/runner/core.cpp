/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "core.hpp"
#include <limits>
namespace modern040 {
State regs;
Host host;
bool ismoves_nommu=false;
std::array<Handler, 65536> handlers{};
std::array<bool, 65536> legal{};
std::array<bool, 65536> requires_lc040_probes{};
int movem_index1[256], movem_index2[256], movem_next[256];
uae_u32 mmu040_move16[4]{};
uae_u32 last_addr_for_exception_3 = 0, last_fault_for_exception_3 = 0, last_op_for_exception_3 = 0;
bool last_writeaccess_for_exception_3 = false, last_notinstruction_for_exception_3 = false;
int last_fc_for_exception_3 = 0, last_size_for_exception_3 = 0, last_di_for_exception_3 = 0;
uae_u16 last_sr_for_exception3 = 0;
namespace {
[[noreturn]] void stop(Exit exit, int vector, uae_u32 address, unsigned size, const char *detail) {
    throw Stop{{exit, 0, vector, regs.instruction_pc, regs.pc, address, size, detail}};
}
bool valid(AccessKind kind, uae_u32 address, unsigned size) {
    if (!host.bus&&(address >= host.ram.size() || size > host.ram.size() - address)) return false;
    if (host.denied && kind == host.denied->kind) {
        const auto first = host.denied->address;
        const auto end = std::uint64_t(first) + host.denied->size;
        if (address < end && std::uint64_t(address) + size > first) return false;
    }
    return true;
}
BusRequest request(AccessKind kind,uae_u32 address,unsigned size) {
    if(!ismoves_nommu)return {kind,address,size,unsigned((regs.s?4:0)|(kind==AccessKind::Fetch?2:1))};
    const auto selected=unsigned(kind==AccessKind::Write?regs.dfc:regs.sfc)&7;
    // MC68040UM Table3-2 (PDF71): MOVES instruction-space references
    // become data references; other alternate spaces use TT=10.
    const auto effective=(selected==2||selected==6)?selected-1:selected;
    const unsigned tt=(selected==0||selected==3||selected==4||selected==7)?2:0;
    return {kind,address,size,effective,selected,tt};
}
bool translation_enabled() { return (regs.tcr | regs.itt0 | regs.itt1 | regs.dtt0 | regs.dtt1) & 0x8000; }
void enter_exception(int nr, bool interrupt, unsigned level) {
    if (!host.deliver_exceptions) {
        stop(Exit::GuestException,nr,nr==3?last_fault_for_exception_3:regs.pc,nr==3?2:0,"terminal guest exception; frame delivery disabled");
    }
    if (nr==2) stop(Exit::Unsupported,2,regs.mmu_fault_addr,0,"format7 access-fault continuation/writeback not integrated");
    if (!interrupt&&nr>=48&&nr<=55) stop(Exit::Unsupported,nr,regs.pc,0,"floating-point arithmetic exception profile not integrated");
    if (host.exception_depth>=2) stop(Exit::DoubleFault,nr,regs.pc,0,"nested exception delivery limit");
    struct Depth {
        Depth() { ++host.exception_depth; }
        ~Depth() { --host.exception_depth; }
    } depth;
    try {
        resume_stopped();
        MakeSR();
        const auto saved_sr=regs.sr;
        const auto current_pc=regs.pc;
        const auto oldpc=regs.instruction_pc;
        if (!regs.s) {
            regs.usp=regs.regs[15];
            regs.regs[15]=regs.m?regs.msp:regs.isp;
            regs.s=true;
        }
        unsigned format=0;
        const bool absent_fpu=nr==11&&regs.fp_unimp_ins;
        if(absent_fpu&&!host.lc040_probes) stop(Exit::Unsupported,nr,regs.pc,0,"LC040 recognized-FPU profile disabled");
        uae_u32 stacked_pc=absent_fpu?current_pc:exception_pc(nr);
        if (interrupt && regs.m) {
            // Master entry: original context on MSP; throwaway context on ISP.
            // The throwaway SR retains M=1 so upstream RTE switches back to MSP.
            Exception_build_stack_frame(oldpc,current_pc,0,nr,0);
            regs.msp=regs.regs[15];
            regs.sr|=0x2000;
            regs.m=false;
            regs.regs[15]=regs.isp;
            Exception_build_stack_frame(oldpc,current_pc,0,nr,1);
            format=1; stacked_pc=current_pc;
        } else if (interrupt) {
            Exception_build_stack_frame(oldpc,current_pc,0,nr,0);
            stacked_pc=current_pc;
        } else if (nr==3) {
            Exception_build_stack_frame(last_fault_for_exception_3,current_pc,0,nr,2);
            format=2; stacked_pc=current_pc;
            if (last_op_for_exception_3 & 0x10000) x_put_word(regs.regs[15],last_sr_for_exception3);
        } else {
            Exception_build_stack_frame_common(oldpc,stacked_pc,regs.mmu_ssw,nr,nr);
            format=absent_fpu?4:(nr==5||nr==6||nr==7||nr==9)?2:0;
        }
        const auto handler=read(AccessKind::Read,regs.vbr+4u*uae_u32(nr),4);
        const auto frame_sr=uae_u16(format==1?(saved_sr|0x2000):nr==3&&(last_op_for_exception_3&0x10000)?last_sr_for_exception3:saved_sr);
        host.frames.push_back({nr,format,regs.regs[15],handler,stacked_pc,frame_sr,interrupt});
        if (handler&1) {
            if (nr==3) stop(Exit::DoubleFault,nr,handler,2,"odd address-error vector");
            complete_exception_trace(nr);
            exception3_notinstruction(regs.opcode,handler);
            return;
        }
        if (interrupt) regs.intmask=int(level);
        m68k_setpc(handler);
        complete_exception_trace(nr);
    } catch (const Stop &event) {
        if (event.result.exit==Exit::MemoryFault)
            stop(Exit::DoubleFault,event.result.vector,event.result.address,event.result.size,"fault while writing frame or fetching vector");
        throw;
    }
}
}
uae_u32 read(AccessKind kind, uae_u32 address, unsigned size) {
    uae_u32 value = 0;
    bool ok=valid(kind,address,size);
    if(ok&&host.bus)ok=host.bus(host.bus_opaque,request(kind,address,size),value);
    if (!ok) {
        if (host.log_accesses) host.accesses.push_back({kind, address, size, 0, false});
        stop(Exit::MemoryFault, 2, address, size, "bus read/fetch fault; access-error continuation not integrated");
    }
    if(!host.bus)for (unsigned i = 0; i < size; ++i) value = (value << 8) | host.ram[address + i];
    if(size<4)value&=(uae_u32{1}<<(size*8))-1;
    if (host.log_accesses) host.accesses.push_back({kind, address, size, value, true});
    // Capability check at the real generated RTE's format-word read. No added
    // read, no modified instruction body and no hidden frame normalization.
    if (host.in_instruction && regs.opcode==0x4e73 && kind==AccessKind::Read && size==2 && address==regs.regs[15]+6) {
        const auto format=value>>12;
        if (format==3||(format==4&&!host.lc040_probes)||format==7)
            stop(Exit::Unsupported,0,address,2,"RTE FPU/access-error continuation profile not integrated");
    }
    return value;
}
void poke(uae_u32 address, unsigned size, uae_u32 value) {
    if (address >= host.ram.size() || size > host.ram.size() - address) throw std::out_of_range("runner RAM initialization");
    for (unsigned i = size; i > 0; --i) { host.ram[address + i - 1] = value & 255; value >>= 8; }
}
void write(uae_u32 address, unsigned size, uae_u32 value) {
    if (size < 4) value &= (uae_u32{1} << (size * 8)) - 1;
    bool ok = valid(AccessKind::Write, address, size);
    if(ok&&host.bus)ok=host.bus(host.bus_opaque,request(AccessKind::Write,address,size),value);
    if (host.log_accesses) host.accesses.push_back({AccessKind::Write, address, size, value, ok});
    if (!ok) stop(Exit::MemoryFault, 2, address, size, "bus write fault; access-error continuation not integrated");
    if(!host.bus)poke(address, size, value);
}
void reset() {
    regs = State{}; host = Host{};
    ismoves_nommu=false;
    last_addr_for_exception_3=last_fault_for_exception_3=last_op_for_exception_3=0;
    last_writeaccess_for_exception_3=last_notinstruction_for_exception_3=false;
    last_fc_for_exception_3=last_size_for_exception_3=last_di_for_exception_3=0;
    last_sr_for_exception3=0;
    for(auto &word:mmu040_move16) word=0;
    for (int i = 0; i < 256; ++i) {
        int bit = 0;
        while (bit < 8 && !(i & (1 << bit))) ++bit;
        movem_index1[i] = bit; movem_index2[i] = 7 - bit; movem_next[i] = i & ~(1 << bit);
    }
}
bool cpuboard_fc_check(uae_u32 address,uae_u32 *,int size,bool writing) {
    // This is the platform seam in the real imported MOVES helpers. There is
    // no Amiga CPU board. An attached bus receives every FC explicitly;
    // flat RAM supports ordinary user/supervisor data/program spaces only.
    const auto fc=writing?regs.dfc:regs.sfc;
    if(!host.bus&&fc!=1&&fc!=2&&fc!=5&&fc!=6)
        stop(Exit::Unsupported,0,address,1u<<size,"flat RAM has no mapping for this alternate function-code space");
    return false;
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
void ExceptionX(int nr, uae_u32, uae_u32 oldpc) {
    if (oldpc!=0xffffffff) regs.instruction_pc=oldpc;
    enter_exception(nr,false,0);
}
void op_illg(uae_u32 opcode) { Exception((opcode & 0xf000)==0xa000?10:(opcode & 0xf000)==0xf000?11:4); }
[[noreturn]] void unsupported_frame(int format) { stop(Exit::Unsupported,14,regs.pc,unsigned(format),"non-040 exception frame format excluded"); }
[[noreturn]] void unsupported_fpu_body() { stop(Exit::Unsupported,0,regs.pc,0,"active-FPU/arithmetic tail outside declared absent-LC040 probe profile"); }
bool inject_interrupt(int vector, unsigned level) {
    if (vector<0||vector>255||level<1||level>7) throw std::invalid_argument("invalid architectural interrupt request");
    if (level<=unsigned(regs.intmask)&&level!=7) return false;
    enter_exception(vector,true,level);
    return true;
}
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
        if (host.halted) return {Exit::DoubleFault,0,0,regs.instruction_pc,regs.pc,0,0,"halted until reset"};
        if (host.capability_latched) return {Exit::Unsupported,0,0,regs.instruction_pc,regs.pc,0,0,"unsupported capability requires reset before continuation"};
        if (host.access_fault_latched) return {Exit::Unsupported,0,2,regs.instruction_pc,regs.pc,0,0,"access-fault restart requires reset; continuation not integrated"};
        if (regs.stopped) return {Exit::Stopped, 0};
        if (translation_enabled()) stop(Exit::UnsupportedMmu, 0, regs.pc, 0, "cannot resume while unsupported MMU/translation is enabled");
        while (instructions < budget) {
            regs.instruction_pc = regs.pc;
            regs.trace_pc = regs.pc;
            if (regs.pc & 1) {
                exception3_read_prefetch(0, regs.pc);
                continue;
            }
            regs.opcode = read(AccessKind::Fetch, regs.pc, 2);
            ++instructions;
            if(requires_lc040_probes[regs.opcode]&&!host.lc040_probes)
                stop(Exit::Unsupported,0,regs.pc,2,"explicit LC040 probe profile required for recognized FPU opcodes");
            const auto handler = handlers[regs.opcode];
            if (!handler) {
                if (!legal[regs.opcode]) { op_illg(regs.opcode); continue; }
                stop(Exit::Unsupported, 0, regs.pc, 2, "legal instruction excluded by current helper/profile inventory");
            }
            const auto trace_before = regs.t1;
            const auto frames_before=host.frames.size();
            struct Instruction {
                Instruction() { host.in_instruction=true; }
                ~Instruction() { host.in_instruction=false; }
            } instruction;
            handler(regs.opcode);
            if (translation_enabled())
                stop(Exit::UnsupportedMmu, 0, regs.pc, 0, "MMU/transparent translation enabled by control write");
            if (regs.spcflags & SPCFLAG_DOTRACE) Exception(9);
            else if (trace_before && host.frames.size()==frames_before) { regs.spcflags |= SPCFLAG_DOTRACE; Exception(9); }
            if (regs.stopped) return {Exit::Stopped, instructions, 0, regs.instruction_pc, regs.pc};
        }
        return {Exit::Budget, instructions, 0, regs.instruction_pc, regs.pc};
    } catch (const Stop &event) {
        // Real MOVES helpers clear their transient flag only on normal return.
        // A host unwind must never leak alternate FC into the next boundary.
        ismoves_nommu=false;
        if (event.result.exit==Exit::MemoryFault) host.access_fault_latched=true;
        if (event.result.exit==Exit::DoubleFault) host.halted=true;
        if (event.result.exit==Exit::Unsupported) host.capability_latched=true;
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
