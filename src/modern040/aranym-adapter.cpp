/* SPDX-License-Identifier: GPL-2.0-or-later
 * Derived from cpu-core-upgrade/platform/adapter.cpp (669bb6c0).
 * Modern interpreter fallback for the experimental full native x86 JIT.
 * Physical-bus restart uses the qualified genuine 040 bodies. Paged MMU and
 * the pinned SoftFloat FPU are owned by the modern core; native JIT admission
 * deoptimizes while paging is active.
 */
#include "sysdeps.h"
// Fixed mapping is retained for native JIT PC pointers. This C-ABI bus TU uses
// the existing explicit ARAnyM checked accessors, including real HW dispatch;
// it must never delegate a modern bus failure to a native host-signal frame.
// Accessors are static inline, so no different external definition is created.
#undef HW_SIGSEGV
#undef NOCHECKBOUNDARY
#undef ARAM_PAGE_CHECK
#include "main.h"
#include "cpu_emulation.h"
#include "newcpu.h"
#include "hardware.h"
#include "modern040-frame.h"
#include "fpu/fpu.h"
#include "runtime/c-api.h"
#include "adapter.h"
#include "compiler/compemu.h"
#include "native-ea-plan.hpp"
#include <algorithm>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include "modern040_performance.h"
#include "modern040_qualified_jit.h"
#include "modern040_execution_mode.h"
#ifdef MODERN040_FPU_JIT
extern bool aranym_modern_fpu_mode();
#endif
extern uae_u32 aranym_jit_alu_probe_begin,aranym_jit_alu_probe_end;
extern uae_u32 aranym_upstream_alu_count(unsigned);
extern uae_u32 aranym_jit_ea_probe_begin,aranym_jit_ea_probe_end;
extern uae_u32 aranym_upstream_ea_count(unsigned);
extern uae_u32 aranym_jit_native_probe_begin,aranym_jit_native_probe_end;
extern uae_u32 aranym_upstream_native_pc_count(uae_u32);
extern uae_u32 aranym_upstream_native_pc_noflags_count(uae_u32);
namespace {
bool initialized=false;unsigned depth=0;unsigned long long attempted=0;
bool modern_guest_started=false;
#ifdef MODERN040_FPU_JIT
bool native_fpu_active=false;
#endif
// ABI1 imports overwrite every field except BUSCR/PCR and, for legacy FPU,
// instruction_pc. Reuse the last exact export; no guest access is skipped.
static_assert(sizeof(m040_state)==152,"audit cached import when state ABI changes");
m040_state import_snapshot{};bool import_snapshot_valid=false;
bool cached_import_enabled(){static const bool enabled=aranym_jit_opt_in("ARANYM_MODERN040_CACHED_IMPORT");return enabled;}
bool verify_import_enabled(){static const bool enabled=std::getenv("ARANYM_MODERN040_VERIFY_IMPORT")!=nullptr;return enabled;}

bool legacy_fpu=false,service_exception_delivered=false;
bool frame_irq_profile=false,frame_irq_skip_native_notify=false;
bool diagnostics=false;uint64_t last_exception=0;unsigned service_messages=0,guest_messages=0,guest_traps=0;
void checked(int,const char *);
int profiled_get_state(m040_state *state){if(aranym_modern_perf_enabled())++aranym_modern_perf().get_state;return m040_get_state(state);}
int profiled_set_state(const m040_state *state){if(aranym_modern_perf_enabled())++aranym_modern_perf().set_state;return m040_set_state(state);}
int profiled_run(uint64_t budget,m040_result *result){AranymModernPerfScope sample(1);return m040_run(budget,result);}

void diagnostic_exception() {
    if(!diagnostics)return;
    m040_exception_event event;checked(m040_get_exception_event(&event),"read exception event");
    const bool admitted=event.vector<=14||event.vector==61||(event.count!=last_exception&&event.stacked_pc>=0x01000000&&guest_traps++<128);
    if(event.count!=last_exception&&admitted)
        std::fprintf(stderr,"MODERN040_EXCEPTION count=%llu vector=%u format=%x savedpc=%08x handler=%08x frame=%08x sr=%04x d0=%08x a7=%08x\n",(unsigned long long)event.count,event.vector,event.format,event.stacked_pc,event.handler,event.stack,event.saved_sr,regs.regs[0],regs.regs[15]);
    last_exception=event.count;
}
void fail(const char *where,const m040_result *result=0) {
    std::fprintf(stderr,"MODERN040_UNSUPPORTED where=%s oldpc=%08x attempts=%llu",where,unsigned(m68k_getpc()),attempted);
    if(result)std::fprintf(stderr," exit=%u vector=%u instruction=%08x current=%08x address=%08x size=%u detail=%s",result->exit,result->vector,result->instruction_pc,result->current_pc,result->address,result->size,result->detail);
    if(result&&result->instruction_pc>=FastRAMBase&&uint64_t(result->instruction_pc)+8<=uint64_t(FastRAMBase)+FastRAMSize)
        std::fprintf(stderr," words=%04x,%04x,%04x,%04x",unsigned(phys_get_word(result->instruction_pc)),unsigned(phys_get_word(result->instruction_pc+2)),unsigned(phys_get_word(result->instruction_pc+4)),unsigned(phys_get_word(result->instruction_pc+6)));
    std::fprintf(stderr," sr=%04x d0=%08x tc=%08x itt0=%08x itt1=%08x dtt0=%08x dtt1=%08x",regs.sr,regs.regs[0],regs.tc,regs.itt0,regs.itt1,regs.dtt0,regs.dtt1);
    std::fputc('\n',stderr);std::fflush(stderr);QuitEmulator(3);
}
void checked(int status,const char *where){if(status!=M040_OK)fail(where);}
#ifdef MODERN040_FPU_JIT
static_assert(sizeof(fpu_register)>=10,"native FPU bridge requires extended precision");
void import_native_fpu() {
    m040_fpu_state state{};state.abi_version=M040_FPU_STATE_VERSION;
    for(unsigned i=0;i<8;++i) {
        uint32_t bits[4]={};std::memcpy(bits,fpu_register_address(i),10);
        state.fp[i][0]=(bits[2]&0xffff)<<16;
        state.fp[i][1]=bits[1];state.fp[i][2]=bits[0];
    }
    state.fpcr=fpu_get_fpcr();
    state.fpsr=aranym_modern_fpu_mode()?
        ((fpu.fpsr.condition_codes&0x0f000000u)|fpu.fpsr.quotient|fpu.fpsr.exception_status|fpu.fpsr.accrued_exception):
        fpu_get_fpsr();
    state.fpiar=fpu.instruction_address;
    checked(m040_set_fpu_state(&state),"import exact native FPU state");
    checked(m040_set_fpu_activity(aranym_modern_fpu_mode()?native_fpu_active:true),"import FPU null/idle state");
}
void export_native_fpu() {
    m040_fpu_state state{};checked(m040_get_fpu_state(&state),"export exact modern FPU state");
    for(unsigned i=0;i<8;++i) {
        uint32_t bits[4]={state.fp[i][2],state.fp[i][1],state.fp[i][0]>>16,0};
        std::memcpy(fpu_register_address(i),bits,10);
    }
    fpu_set_fpcr(state.fpcr);fpu_set_fpsr(state.fpsr);
    fpu.fpsr.condition_codes=state.fpsr&0x0f000000u;
    fpu.instruction_address=state.fpiar;
    uint32_t active=0;checked(m040_get_fpu_activity(&active),"export FPU null/idle state");
    native_fpu_active=active!=0;
}
#endif
void from_old(bool preserve_instruction=false) {
    AranymModernPerfScope sample(0);
    m040_state state;
    if(cached_import_enabled()&&import_snapshot_valid) {
        state.abi_version=import_snapshot.abi_version;state.buscr=import_snapshot.buscr;
        state.pcr=import_snapshot.pcr;state.instruction_pc=import_snapshot.instruction_pc;aranym_modern_perf_event(10);
        if(verify_import_enabled()) {
            m040_state actual;checked(profiled_get_state(&actual),"verify cached import source");
            if(state.abi_version!=actual.abi_version||state.buscr!=actual.buscr||state.pcr!=actual.pcr||
               (preserve_instruction&&state.instruction_pc!=actual.instruction_pc))
                fail("cached import preserved fields differ from genuine state export");
            aranym_modern_perf_event(12);
        }
    } else {checked(profiled_get_state(&state),"read modern state");aranym_modern_perf_event(11);}
    MakeSR();
    for(unsigned i=0;i<16;++i)state.regs[i]=regs.regs[i];
    state.pc=m68k_getpc();if(!preserve_instruction)state.instruction_pc=state.pc;state.sr=regs.sr;
    state.usp=regs.usp;state.isp=regs.isp;state.msp=regs.msp;
    if(!regs.s)state.usp=regs.regs[15];else if(regs.m)state.msp=regs.regs[15];else state.isp=regs.regs[15];
    state.stopped=regs.stopped;state.vbr=regs.vbr;state.sfc=regs.sfc;state.dfc=regs.dfc;
    state.cacr=regs.cacr;state.caar=regs.caar;state.itt0=regs.itt0;state.itt1=regs.itt1;state.dtt0=regs.dtt0;state.dtt1=regs.dtt1;
    state.tcr=regs.tc;state.mmusr=regs.mmusr;state.urp=regs.urp;state.srp=regs.srp;
    checked(profiled_set_state(&state),"import original architectural state");
#ifdef MODERN040_FPU_JIT
    import_native_fpu();
#endif
    if(cached_import_enabled()){import_snapshot.abi_version=state.abi_version;import_snapshot.buscr=state.buscr;
        import_snapshot.pcr=state.pcr;import_snapshot.instruction_pc=state.instruction_pc;import_snapshot_valid=true;}
}
void to_old() {
    AranymModernPerfScope sample(2);
    m040_state state;checked(profiled_get_state(&state),"export modern architectural state");
    if(cached_import_enabled()){import_snapshot.abi_version=state.abi_version;import_snapshot.buscr=state.buscr;
        import_snapshot.pcr=state.pcr;import_snapshot.instruction_pc=state.instruction_pc;import_snapshot_valid=true;}
    for(unsigned i=0;i<16;++i)regs.regs[i]=state.regs[i];
    regs.usp=state.usp;regs.isp=state.isp;regs.msp=state.msp;
    regs.s=bool(state.sr&0x2000);regs.m=bool(state.sr&0x1000);regs.sr=state.sr;
    MakeFromSR();regs.stopped=state.stopped;regs.vbr=state.vbr;regs.sfc=state.sfc;regs.dfc=state.dfc;
    regs.cacr=state.cacr;regs.caar=state.caar;regs.itt0=state.itt0;regs.itt1=state.itt1;regs.dtt0=state.dtt0;regs.dtt1=state.dtt1;
    regs.tc=state.tcr;regs.mmusr=state.mmusr;regs.urp=state.urp;regs.srp=state.srp;
    m68k_setpc(state.pc);regs.fault_pc=state.instruction_pc;
#ifdef MODERN040_FPU_JIT
    export_native_fpu();
#endif
    SPCFLAGS_CLEAR(SPCFLAG_TRACE|SPCFLAG_DOTRACE);
    if(state.stopped)SPCFLAGS_SET(SPCFLAG_STOP);else SPCFLAGS_CLEAR(SPCFLAG_STOP);
}
// Synthetic read-sensitive bus fixture, enabled only by an explicit harness
// environment. Both genuine interpreter and emitted helper arms see this same
// bus operation; no real Atari device or owner image is substituted.
struct SideEffectBus { uint32_t reads=0,writes=0,last=0,faults=0; } side_bus;
int side_effect_fixture(const m040_bus_request *request,uint32_t *value) {
    static const bool enabled=std::getenv("ARANYM_UPSTREAM_JIT_SIDE_EFFECT_FIXTURE")!=nullptr;
    if(!enabled||request->address<0x00fef000||request->address>0x00fef014)return -1;
    static bool reported=false;
    if(!reported) {
        std::atexit([]{std::fprintf(stderr,"SIDE-EFFECT-BUS reads=%u writes=%u last=%08x faults=%u\n",side_bus.reads,side_bus.writes,side_bus.last,side_bus.faults);});
        reported=true;
    }
    if(request->kind==M040_FETCH||(request->size!=1&&request->size!=2&&request->size!=4))return 0;
    const uint32_t offset=request->address-0x00fef000;
    if(offset==20){++side_bus.faults;return 0;}
    if(request->kind==M040_WRITE) {
        if(offset==4){side_bus={};return 1;}
        if(offset!=0)return 0;
        ++side_bus.writes;side_bus.last=request->size==1?(*value&255):request->size==2?(*value&65535):*value;
        return 1;
    }
    switch(offset) {
    case 0:*value=0x12340000u|++side_bus.reads;break;
    case 4:*value=side_bus.reads;break;
    case 8:*value=side_bus.writes;break;
    case 12:*value=side_bus.last;break;
    case 16:*value=side_bus.faults;break;
    default:return 0;
    }
    if(request->size==1)*value&=255;else if(request->size==2)*value&=65535;
    return 1;
}
int arithmetic_fixture(const m040_bus_request *request,uint32_t *value) {
    static const bool enabled=std::getenv("ARANYM_UPSTREAM_JIT_ALU_FIXTURE")!=nullptr;
    if(!enabled)return -1;
    if(request->address>=0x00fef040 && request->address<0x00fef094) {
        if(request->kind!=M040_READ || request->size!=4 || (request->address&3) || request->transfer_type!=0)return 0;
        *value=aranym_upstream_alu_count((request->address-0x00fef040)/4);return 1;
    }
    if(request->address<0x00fef020 || request->address>0x00fef024)return -1;
    if(request->kind!=M040_WRITE || request->size!=4 || request->transfer_type!=0)return 0;
    if(request->address==0x00fef020)aranym_jit_alu_probe_begin=*value;
    else if(request->address==0x00fef024)aranym_jit_alu_probe_end=*value;
    else return 0;
    return 1;
}
// Separate deterministic EA fixture: configured data, exact transfer counts,
// and explicitly registered guest-only native execution counters.
int ea_fixture(const m040_bus_request *request,uint32_t *value) {
    static const bool enabled=std::getenv("ARANYM_UPSTREAM_JIT_EA_FIXTURE")!=nullptr;
    if(!enabled || request->address<0x00fed000 || request->address>=0x00fed080)return -1;
    static uint32_t data=0,reads=0,writes=0,last=0,faults=0;
    if(request->transfer_type || (request->kind!=M040_READ&&request->kind!=M040_WRITE) || (request->size!=1&&request->size!=2&&request->size!=4))return 0;
    const uint32_t offset=request->address-0x00fed000;
    if(offset==0x14){++faults;std::fprintf(stderr,"EA-FAULT reads=%u writes=%u faults=%u\n",reads,writes,faults);return 0;}
    if(offset==0) {
        if(request->kind==M040_WRITE) {++writes;last=*value;}
        else {++reads;*value=data;}
        if(request->size==1){if(request->kind==M040_WRITE)last&=255;else *value&=255;}
        if(request->size==2){if(request->kind==M040_WRITE)last&=65535;else *value&=65535;}
        return 1;
    }
    if(request->size!=4 || (offset&3))return 0;
    if(request->kind==M040_WRITE) {
        if(offset==4){data=*value;reads=writes=last=faults=0;}
        else if(offset==0x20)aranym_jit_ea_probe_begin=*value;
        else if(offset==0x24)aranym_jit_ea_probe_end=*value;
        else return 0;
    } else if(offset==4)*value=reads;
    else if(offset==8)*value=writes;
    else if(offset==12)*value=last;
    else if(offset==16)*value=faults;
    else if(offset==0x28)*value=std::getenv("ARANYM_UPSTREAM_JIT_CHECKED_MOVE")!=nullptr;
    else if(offset==0x2c)*value=!std::getenv("ARANYM_UPSTREAM_JIT_RAM_MOVE_OFF")&&!std::getenv("ARANYM_UPSTREAM_JIT_CHECKED_MOVE");
    else if(offset>=0x40 && offset<0x60)*value=aranym_upstream_ea_count((offset-0x40)/4);
    else return 0;
    return 1;
}
int native_pc_fixture(const m040_bus_request *request,uint32_t *value) {
    static const bool enabled=std::getenv("ARANYM_UPSTREAM_JIT_NATIVE_FIXTURE")!=nullptr;
    if(!enabled||request->address<0x00fec000||request->address>=0x00fec010)return -1;
    static uae_u32 selected_pc=0;
    if(request->size!=4||request->transfer_type||request->has_alternate_function_code)return 0;
    const auto offset=request->address-0x00fec000;
    if(request->kind==M040_WRITE) {
        if(offset==0)aranym_jit_native_probe_begin=*value;
        else if(offset==4)aranym_jit_native_probe_end=*value;
        else if(offset==8)selected_pc=*value;
        else return 0;
    } else if(request->kind==M040_READ&&offset==8) {
        *value=aranym_upstream_native_pc_count(selected_pc);
    } else if(request->kind==M040_READ&&offset==12) {
        *value=aranym_upstream_native_pc_noflags_count(selected_pc);
    } else return 0;
    return 1;
}
// Owned fault fixture: reads at +0x80 complete, writes at +0x80 fail before
// any store; reads at +0x90 fail before returning data. This explicit contract
// must never be inferred for a mapped production device that throws midway.
int rmw_fault_fixture(const m040_bus_request *request,uint32_t *value) {
    static const bool enabled=std::getenv("ARANYM_UPSTREAM_JIT_RMW_FAULT_FIXTURE")!=nullptr;
    if(!enabled||request->address<0x00fed080||request->address>=0x00fed098)return -1;
    if(request->transfer_type||(request->kind!=M040_READ&&request->kind!=M040_WRITE))return 0;
    static uint32_t data=0,reads=0,write_attempts=0,last=0,read_faults=0;
    const auto offset=request->address-0x00fed080;
    if(offset==0||offset==0x10) {
        if(request->size!=1&&request->size!=2&&request->size!=4)return 0;
        const auto mask=request->size==1?255u:request->size==2?65535u:0xffffffffu;
        if(request->kind==M040_WRITE){++write_attempts;last=*value&mask;return M040_BUS_FAULT_BEFORE_TRANSFER;}
        if(offset==0x10){++read_faults;return M040_BUS_FAULT_BEFORE_TRANSFER;}
        ++reads;*value=data&mask;return M040_BUS_COMPLETED;
    }
    if(request->size!=4)return 0;
    if(request->kind==M040_WRITE) {
        if(offset!=4)return 0;
        data=*value;reads=write_attempts=last=read_faults=0;return M040_BUS_COMPLETED;
    }
    if(offset==4)*value=reads;
    else if(offset==8)*value=write_attempts;
    else if(offset==12)*value=last;
    else if(offset==20)*value=read_faults;
    else return 0;
    return M040_BUS_COMPLETED;
}
// Closed qualification fixture only. Operand data and a read-sensitive pointer
// have separate addresses from the inspection controls. Each attempted operand
// request is recorded before completion; an injected failure is explicitly
// BEFORE_TRANSFER and is armed once. Inspection never consumes operand reads.
int movem_bus_fixture(const m040_bus_request *request,uint32_t *value) {
    static const bool enabled=std::getenv("ARANYM_UPSTREAM_JIT_MOVEM_BUS_FIXTURE")!=nullptr;
    if(!enabled)return -1;
    static const bool suppress_fetch_fault=std::getenv("ARANYM_UPSTREAM_JIT_MOVEM_FETCH_DISABLE")!=nullptr;
    static uint32_t fetch_address=0,fetch_armed=0,fetch_attempts=0,fetch_faults=0,fetch_size=0,fetch_fc=0;
    // A separate one-shot instruction-fetch denial lets the guest exercise
    // CM recovery without pretending the device data window contains code.
    // Successful watched fetches fall through to the real checked RAM bus.
    if(fetch_address&&request->kind==M040_FETCH&&request->address==fetch_address) {
        ++fetch_attempts;fetch_size=request->size;fetch_fc=request->function_code;
        if(fetch_armed&&!suppress_fetch_fault){fetch_armed=0;++fetch_faults;return M040_BUS_FAULT_BEFORE_TRANSFER;}
    }
    const bool signed_alias=request->address>=0xfffffa80&&request->address<0xfffffac0;
    if(!signed_alias&&(request->address<0x00fed200||request->address>=0x00fed340))return -1;
    struct Event {uint32_t address=0,info=0,value=0;};
    static std::array<uint8_t,64> data{};
    static std::array<Event,64> events{};
    static uint32_t count=0,selected=0,reads=0,writes=0,faults=0,fail_at=~0u,pointer=0;
    const auto offset=request->address-(signed_alias?0xfffffa80:0x00fed200);
    const bool write=request->kind==M040_WRITE,read=request->kind==M040_READ;
    if(request->transfer_type||request->has_alternate_function_code||(!read&&!write))return M040_BUS_FAULT_BEFORE_TRANSFER;
    if(offset<64||offset==0x40) {
        if((request->size!=1&&request->size!=2&&request->size!=4)||(offset<64&&offset+request->size>64)||(offset==0x40&&(write||request->size!=4))||count==events.size())return M040_BUS_FAULT_BEFORE_TRANSFER;
        auto &event=events[count++];event={request->address,request->size|(write?0x100u:0u),write?(*value&(request->size==1?0xffu:request->size==2?0xffffu:~0u)):0};
        if(count-1==fail_at) {
            fail_at=~0u;++faults;event.info|=0x10000;return M040_BUS_FAULT_BEFORE_TRANSFER;
        }
        if(write) {
            for(unsigned i=0;i<request->size;++i)data[offset+i]=uint8_t(*value>>(8*(request->size-1-i)));
            ++writes;
        } else {
            *value=0;
            if(offset==0x40)*value=pointer;
            else for(unsigned i=0;i<request->size;++i)*value=(*value<<8)|data[offset+i];
            event.value=*value;++reads;
        }
        return M040_BUS_COMPLETED;
    }
    if(request->size!=4||(offset&3))return M040_BUS_FAULT_BEFORE_TRANSFER;
    if(offset>=0x100) {
        if(!read)return M040_BUS_FAULT_BEFORE_TRANSFER;
        *value=0;for(unsigned i=0;i<4;++i)*value=(*value<<8)|data[offset-0x100+i];
        return M040_BUS_COMPLETED;
    }
    if(write) {
        if(offset==0x80) {
            for(unsigned i=0;i<data.size();++i)data[i]=uint8_t(i*19+0x80);
            events={};count=selected=reads=writes=faults=0;fail_at=~0u;
            fetch_address=fetch_armed=fetch_attempts=fetch_faults=fetch_size=fetch_fc=0;
        } else if(offset==0x88)selected=*value;
        else if(offset==0xa4)fail_at=*value;
        else if(offset==0xac)pointer=*value;
        else if(offset==0xb0)fetch_address=*value;
        else if(offset==0xb4)fetch_armed=*value;
        else if(offset==0xc8)for(unsigned i=0;i<4;++i)data[i]=uint8_t(*value>>(8*(3-i)));
        else return M040_BUS_FAULT_BEFORE_TRANSFER;
    } else {
        if(offset==0x84)*value=count;
        else if(offset>=0x8c&&offset<=0x94) {
            if(selected>=count)return M040_BUS_FAULT_BEFORE_TRANSFER;
            const auto &event=events[selected];
            *value=offset==0x8c?event.address:offset==0x90?event.info:event.value;
        } else if(offset==0x98)*value=reads;
        else if(offset==0x9c)*value=writes;
        else if(offset==0xa0)*value=faults;
        else if(offset==0xb8)*value=fetch_attempts;
        else if(offset==0xbc)*value=fetch_faults;
        else if(offset==0xc0)*value=fetch_size;
        else if(offset==0xc4)*value=fetch_fc;
        else return M040_BUS_FAULT_BEFORE_TRANSFER;
    }
    return M040_BUS_COMPLETED;
}
int frame_storage_identity(void*,const m040_bus_request*request,uint64_t*key) {
    // This is this backend's actual NoMMU RAM-storage alias, not a CPU
    // physical address supplied to format7 or a target memory access.
    if(request->transfer_type||!request->size||request->size>0x7fffffffU)return M040_FRAME_UNKNOWN;
    const uae_u32 address=request->address<0xff000000?request->address:request->address&0x00ffffff;
    const uint64_t end=uint64_t(address)+request->size;
    if(end>0x100000000ULL)return M040_FRAME_UNKNOWN;
    if((address&0xfff00000)==0x00f00000&&end<=0x01000000)return M040_FRAME_DISJOINT;
    if(test_ram_boundary(address,int(request->size),true,true)) {*key=address;return M040_FRAME_RAM;}
    return M040_FRAME_UNKNOWN;
}
int physical_transfer(const m040_bus_request *request,uint32_t *value) {
    const bool write=request->kind==M040_WRITE;
    switch(request->size) {
    case 1:
        if(write)phys_put_byte(request->address,*value);
        else *value=phys_get_byte(request->address);
        break;
    case 2:
        if(write)phys_put_word(request->address,*value);
        else *value=phys_get_word(request->address);
        break;
    case 4:
        if(write)phys_put_long(request->address,*value);
        else *value=phys_get_long(request->address);
        break;
    default:return M040_BUS_UNKNOWN_FAULT;
    }
    return M040_BUS_COMPLETED;
}
int memory(void *,const m040_bus_request *request,uint32_t *value) {
    AranymModernPerfScope sample(3);
    if(aranym_modern_perf_enabled()&&request->kind<3)++aranym_modern_perf().memory[request->kind];
    const int native=native_pc_fixture(request,value);
    if(native>=0)return native;
    const int ea=ea_fixture(request,value);
    if(ea>=0)return ea;
    const int rmw=rmw_fault_fixture(request,value);
    if(rmw>=0)return rmw;
    const int movem=movem_bus_fixture(request,value);
    if(movem>=0)return movem;
    const int arithmetic=arithmetic_fixture(request,value);
    if(arithmetic>=0)return arithmetic;
    if(request->transfer_type!=0)return 0; // no invented alternate CPU space.
    const int fixture=side_effect_fixture(request,value);
    if(fixture>=0)return fixture;
    const uae_u32 physical=request->address<0xff000000?request->address:request->address&0x00ffffff;
    const bool super=bool(request->function_code&4);
    if((physical&0xfff00000)==0x00f00000) {
        if(HWaccess_unmapped(physical))return M040_BUS_FAULT_BEFORE_TRANSFER;
    } else if(!test_ram_boundary(physical,request->size,super,request->kind==M040_WRITE)) {
        return M040_BUS_FAULT_BEFORE_TRANSFER;
    }
    const auto saved_super=regs.s;regs.s=bool(request->function_code&4);
#ifdef ARAM_PAGE_CHECK
    // Original page shortcuts cache privilege/permission decisions. Each
    // explicit bus operation must recheck its own effective FC and boundary.
    read_page=write_page=request->address^(uae_u32(ARAM_PAGE_MASK)+1);
#endif
    const uint64_t end=uint64_t(physical)+request->size;
    if((physical>=0x800 && end<=RAMSize) ||
       (physical>=FastRAMBase && end<=uint64_t(FastRAMBase)+FastRAMSize)) {
        const int result=physical_transfer(request,value);
        regs.s=saved_super;
        return result;
    }
    VOLATILE int ok=1;
    /* TRY uses ARAnyM's process-global excep_env in longjmp builds.  A bus
     * callback is nested inside the outer CPU/JIT exception frame, so retain
     * that owner before installing this short-lived probe frame. */
    SAVE_EXCEPTION;
    TRY(prb) {
        ok=physical_transfer(request,value);
    } CATCH(prb) {ok=0;}
    RESTORE_EXCEPTION;
    regs.s=saved_super;return ok;
}
void initialize() {
    if(initialized)return;
#if defined(FULLMMU) || defined(HW_SIGSEGV) || !defined(EXPERIMENTAL_UPSTREAM_MODERN040)
#error This derived adapter requires explicit upstream-JIT/modern040 profile without MMU or signal addressing
#endif
    legacy_fpu=std::getenv("ARANYM_MODERN040_LEGACY_FPU")!=0;
    const m040_options options={M040_ABI_VERSION,1,0,0};
    diagnostics=std::getenv("ARANYM_MODERN040_DIAGNOSTICS")!=0;
    import_snapshot_valid=false;
    if(verify_import_enabled()&&!cached_import_enabled())fail("import verification requires cached import opt-in");
    if(cached_import_enabled())std::fprintf(stderr,"MODERN040_IMPORT_CACHE enabled verify=%u\n",unsigned(verify_import_enabled()));
    checked(m040_reset(&options),"reset modern core");checked(m040_set_precise_bus(memory,0),"attach precise Atari physical bus");
    checked(m040_fault_recovery(1),"enable restart-aware physical-bus integer bodies");
    frame_irq_profile=std::getenv("ARANYM_MODERN040_FRAME_IRQ")!=nullptr;
    frame_irq_skip_native_notify=frame_irq_profile&&std::getenv("ARANYM_MODERN040_FRAME_IRQ_NO_NATIVE_NOTIFY")!=nullptr;
    if(frame_irq_skip_native_notify&&(!std::getenv("ARANYM_UPSTREAM_JIT_NATIVE_FIXTURE")||!std::getenv("ARANYM_UPSTREAM_JIT_MOVEM_BUS_FIXTURE")))
        fail("missing native-notify control requires both owned fixture backends");
    if(frame_irq_profile) {
        checked(m040_set_frame_storage(frame_storage_identity,nullptr),"attach pure Atari RAM-storage identity");
        checked(m040_completed_frame_irq(1),"enable bounded exact-frame IRQ completion");
        std::fprintf(stderr,"MODERN040_FRAME_IRQ exact-storage default-off native-notify=%s\n",frame_irq_skip_native_notify?"negative-fixture-disabled":"on");
    }
    checked(m040_exception_history_limit(0),"retain bounded exception diagnostics without accumulating every guest trap");
    checked(m040_set_reset_handler([](void *){AtariReset();return 1;},0),"attach original Atari RESET hardware/NatFeat/input path");
    checked(m040_legacy_fpu_operations(legacy_fpu),"select explicit original FPU backend");
    checked(m040_modern_fpu_operations(!legacy_fpu),"select pinned modern FPU frontend");
    checked(m040_private_operations(1),"enable ARAnyM extension decoder");initialized=true;
    std::fprintf(stderr,"MODERN040_EXPERIMENTAL source=d82bc254 profile=%s paged-MMU native-JIT-boundary fault-restart=precise-physical-bus\n",legacy_fpu?"modern-integer-original-ieee-fpu":"modern-integer-pinned-softfloat-fpu");
#ifdef MODERN040_FPU_JIT
    std::fprintf(stderr,"MODERN040_FPU_JIT available=yes enabled=%s frontend=%s\n",
        bx_options.jit.jitfpu?"yes":"no",aranym_modern_fpu_mode()?"pinned-softfloat":"classic-compatible");
#endif
    std::fprintf(stderr,"MODERN040_FALLBACK physical=classic natfeat=noMMU-classic emulop=supervisor-classic/user-modern fpu=noMMU-classic modern=MMU/translation/trace/restart\n");
}
bool acceptable(const m040_result &result){return result.exit==M040_BUDGET||result.exit==M040_STOPPED||result.exit==M040_HOST_RETURN;}
void service(m040_result &result) {
    AranymModernPerfScope sample(4);
    m040_service call;checked(m040_get_service(&call),"read private instruction metadata");
    if(aranym_modern_perf_enabled()&&call.kind<6)++aranym_modern_perf().services[call.kind];
    VOLATILE bool trace_service=diagnostics&&call.kind<=M040_NATFEAT_CALL&&(service_messages++<128||call.pc>=0x01000000);
    if(trace_service)std::fprintf(stderr,"MODERN040_SERVICE begin kind=%u pc=%08x stack=%08x d0=%08x sr=%04x\n",call.kind,call.pc,call.stack,regs.regs[0],regs.sr);
    service_exception_delivered=false;
    /* NatFeat, EmulOp and legacy-FPU services can themselves use TRY. Do not
     * leave their temporary longjmp target installed after returning to the
     * native JIT. */
    SAVE_EXCEPTION;
    TRY(prb) {
        switch(call.kind) {
        case M040_NATFEAT_ID:m68k_natfeat_id();break;
        case M040_NATFEAT_CALL:m68k_natfeat_call();break;
        case M040_EMULOP:m68k_emulop(call.opcode);break;
        case M040_EMULOP_RETURN:m68k_emulop_return();break;
        case M040_LEGACY_FPU: {
            if(!legacy_fpu||CPUType!=4)fail("legacy FPU requires explicit native 68040 profile");
            if(fpu_get_fpcr()&0xff00)fail("enabled legacy FPU arithmetic exception mode not qualified");
            // The original native table and generated entry argument use host
            // fetch encoding; the modern boundary exports numeric BE opcode.
            uae_u32 encoded=call.opcode;
#if defined(HAVE_GET_WORD_UNSWAPPED) && !defined(FULLMMU)
            encoded=do_byteswap_16(encoded);
#endif
            cpufunctbl[encoded](encoded); // real original body, exactly once.
            break;
        }
        default:fail("unknown private instruction kind");
        }
        if(service_exception_delivered) {
            m040_state state;checked(profiled_get_state(&state),"read external service exception target");
            if(state.pc!=m68k_getpc())fail("external body changed delivered exception target");
            result={};result.abi_version=M040_ABI_VERSION;result.exit=M040_BUDGET;
            result.current_pc=state.pc;result.instruction_pc=state.instruction_pc;
        } else {
            from_old(call.kind==M040_LEGACY_FPU);
            checked(m040_complete_service(&result),"complete external source body");
        }
    } CATCH(prb) {
        if(call.kind==M040_LEGACY_FPU)fail("legacy FPU operand fault/restart not integrated");
        from_old();checked(m040_exception(int(prb),&result),"NatFeat host exception boundary");
    }
    RESTORE_EXCEPTION;
    to_old();
    if(trace_service)std::fprintf(stderr,"MODERN040_SERVICE end kind=%u pc=%08x d0=%08x sr=%04x\n",call.kind,unsigned(m68k_getpc()),regs.regs[0],regs.sr);
}
}
void modern040_prepare(){initialize();}
void modern040_memory_barrier() {
    aranym_modern_perf_event(9);
    // Hardware copies can run inside an existing CPU bus callback. The core
    // barrier touches only same-thread provenance, with no resolver/reentry.
    if(initialized&&frame_irq_profile)checked(m040_frame_barrier(),"invalidate direct hardware RAM write provenance");
}
void modern040_reset_requested(){initialized=false;import_snapshot_valid=false;modern_guest_started=false;
#ifdef MODERN040_FPU_JIT
    native_fpu_active=false;
#endif
}
#ifdef MODERN040_FPU_JIT
void aranym_modern_fpu_native_completed(){native_fpu_active=true;}
#endif
bool modern040_restart_pending() {
    if(!initialized)return false;
    int pending=0;checked(m040_native_restart_pending(&pending),"read native restart boundary");return pending;
}
bool modern040_owns_exception() {
    return depth||modern040_restart_pending();
}
bool modern040_owns_interrupt() {
    return modern040_owns_exception()||(frame_irq_profile&&modern_guest_started);
}
bool modern040_irq_restart_pending() {
    if(!initialized)return false;
    int pending=0;checked(m040_restart_pending(&pending),"read restart boundary");return pending;
}
bool modern040_interrupt_ready(unsigned level) {
    if(!initialized)return false;
    int ready=0;checked(m040_interrupt_ready(level,&ready),"query pure IRQ acceptance before acknowledgement");return ready;
}
void modern040_external_exception(int vector) {
    if(!initialized)fail("external exception before initialization");
    m040_service pending;const bool service_pending=m040_get_service(&pending)==M040_OK;
    const bool fpu=service_pending&&pending.kind==M040_LEGACY_FPU;
    from_old(fpu);m040_result result;checked(m040_exception(vector,&result),"external exception");to_old();
    if(service_pending){service_exception_delivered=true;aranym_modern_perf_event(7);}
    if(!acceptable(result))fail("external exception unsupported",&result);
}
void modern040_external_interrupt(int vector,unsigned level) {
    if(!initialized)fail("IRQ before initialization");
    from_old();m040_result result;int accepted;checked(m040_interrupt(vector,level,&accepted,&result),"explicit Atari IRQ");to_old();
    if(!acceptable(result))fail("Atari IRQ unsupported",&result);
}
void modern040_execute() {
    if(depth)fail("nested guest execution not yet integrated");
    struct Depth{Depth(){++depth;}~Depth(){--depth;}}guard;
    initialize();from_old();
    for(;;) {
        AranymModernPerfStep profile_step(false,0,m68k_getpc());
        if(diagnostics&&guest_messages<64&&m68k_getpc()>=FastRAMBase&&m68k_getpc()<FastRAMBase+FastRAMSize) {
            // Bounded own-emulator instruction history in mapped guest RAM,
            // never MMIO and no external debugger/process-memory access.
            std::fprintf(stderr,"MODERN040_GUEST pc=%08x op=%04x d0=%08x a7=%08x sr=%04x\n",unsigned(m68k_getpc()),unsigned(phys_get_word(m68k_getpc())),regs.regs[0],regs.regs[15],regs.sr);++guest_messages;
        }
        m040_result result;checked(profiled_run(1,&result),"bounded modern dispatch");attempted+=result.instructions;to_old();
        if(result.exit==M040_BUDGET&&result.vector==2)
            std::fprintf(stderr,"MODERN040_ACCESS_ERROR_DELIVERED instruction=%08x fault=%08x handler=%08x\n",result.instruction_pc,result.address,result.current_pc);
        if(result.exit==M040_HOST_SERVICE)service(result);
        diagnostic_exception();
        if(diagnostics&&attempted&&attempted%1000000==0)std::fprintf(stderr,"MODERN040_PROGRESS attempts=%llu pc=%08x sr=%04x a7=%08x\n",attempted,unsigned(m68k_getpc()),regs.sr,regs.regs[15]);
        if(!acceptable(result))fail("modern dispatch unsupported",&result);
        if(result.exit==M040_HOST_RETURN||quit_program>0)return;
        {AranymModernPerfScope sample(5);cpu_check_ticks();regs.fault_pc=m68k_getpc();
        if(SPCFLAGS_TEST(SPCFLAG_ALL_BUT_EXEC_RETURN)&&m68k_do_specialties())return;}
        from_old();
    }
}

#include "aranym-fpu-control.h"

// One genuine modern interpreter operation at the native JIT fallback boundary.
// Architectural state crosses the C ABI only here, not around translated ops.
void REGPARAM2 aranym_upstream_modern_step(uae_u32 encoded_opcode) {
    if(depth)fail("nested JIT fallback guest execution not qualified");
    struct Depth{Depth(){++depth;}~Depth(){--depth;}}guard;
    initialize();
    const uae_u32 entry_pc=m68k_getpc();
    if(entry_pc>=FastRAMBase&&uint64_t(entry_pc)<uint64_t(FastRAMBase)+FastRAMSize)
        modern_guest_started=true;
    unsigned profile_opcode=encoded_opcode;
#if defined(HAVE_GET_WORD_UNSWAPPED) && !defined(FULLMMU)
    profile_opcode=do_byteswap_16(profile_opcode);
#endif
    AranymModernPerfStep profile_step(true,profile_opcode,m68k_getpc());
#if defined(CPU_x86_64) && defined(MODERN040_FPU_JIT)
    if(modern040_fpu_control_step(profile_opcode,entry_pc)) {++attempted;return;}
#endif
    from_old();
    const uae_u32 old_cacr=regs.cacr;
    const bool old_translation=aranym_modern_translation_active(regs.tc,
        regs.itt0,regs.itt1,regs.dtt0,regs.dtt1);
    m040_result result;checked(profiled_run(1,&result),"modern JIT fallback");
    attempted+=result.instructions;to_old();
    if(result.exit==M040_HOST_SERVICE)service(result);
    // Compiled fallback blocks may link directly to their successor instead
    // of re-entering m68k_do_compile_execute's trace/restart dispatch check.
    // A format7 RTE must leave that chain before any cached target runs, so
    // the genuine restarted body consumes MMURESTART and retains IRQ order.
    int completed_return=0;
    if(frame_irq_profile)checked(m040_completed_rte_event(&completed_return),"read actual completed-RTE native boundary");
    if(result.vector||regs.t1||regs.t0||modern040_restart_pending()||completed_return){aranym_modern_perf_event(8);SPCFLAGS_SET(SPCFLAG_JIT_END_COMPILE);}
    diagnostic_exception();
    if(!acceptable(result))fail("modern JIT fallback unsupported",&result);
    // Core cache helpers have empty synchronous interpreter caches. Propagate
    // their architectural control to the real native code cache explicitly.
    if(regs.cacr!=old_cacr)set_cache_state(regs.cacr&0x8000);
    if(old_translation!=aranym_modern_translation_active(regs.tc,
       regs.itt0,regs.itt1,regs.dtt0,regs.dtt1))
        SPCFLAGS_SET(SPCFLAG_JIT_END_COMPILE);
    #if defined(HAVE_GET_WORD_UNSWAPPED) && !defined(FULLMMU)
    const uae_u32 numeric_opcode=do_byteswap_16(encoded_opcode);
#else
    const uae_u32 numeric_opcode=encoded_opcode;
#endif
    if((numeric_opcode&0xff00)==0xf400)flush_icache(0);
    if(result.exit==M040_HOST_RETURN)SPCFLAGS_SET(SPCFLAG_JIT_EXEC_RETURN);
}
// Pure range/permission guard: no guest memory access, device callback, state
// update or guessed value. Unsafe targets leave the block BEFORE the op runs.
uae_u32 REGPARAM2 aranym_upstream_ram_move_guard(uae_u32 opcode,uae_u32 extensions) {
    const uae_u32 extension1=extensions&65535,extension2=extensions>>16;
    const unsigned top=opcode>>12;
    if(top<1||top>3)return 0;
    const unsigned size=top==1?1:top==2?4:2;
    const unsigned source=(opcode>>3)&7,destination=(opcode>>6)&7;
    const unsigned src_reg=opcode&7,dst_reg=(opcode>>9)&7;
    const bool reading=source>=2&&source<=5,writing=destination>=2&&destination<=5;
    if((!reading&&source>1)||(!writing&&destination>1)||(!reading&&!writing))return 0;
    const auto permitted=[&](uae_u32 address,bool write) {
        address=address<0xff000000?address:address&0x00ffffff;
        return (address&0xfff00000)!=0x00f00000&&test_ram_boundary(address,size,regs.s,write);
    };
    uae_u32 destination_base=regs.regs[8+dst_reg];
    if(reading) {
        uae_u32 address=regs.regs[8+src_reg];
        const unsigned increment=size==1&&src_reg==7?2:size;
        if(source==4)address-=increment;
        if(source==5)address+=uae_s16(extension1);
        if(!permitted(address,false))return 0;
        // Source auto-update precedes destination EA calculation, including
        // MOVE (An)+,(An) and MOVE -(An),-(An) register aliasing.
        if(src_reg==dst_reg&&(source==3||source==4))
            destination_base=source==3?destination_base+increment:destination_base-increment;
    }
    if(writing) {
        const unsigned increment=size==1&&dst_reg==7?2:size;
        if(destination==4)destination_base-=increment;
        if(destination==5)destination_base+=uae_s16(source==5?extension2:extension1);
        if(!permitted(destination_base,true))return 0;
    }
    return 1;
}
namespace {
template<bool Multiple> uae_u32 native_ram_plan(const void *encoded) {
    modern040::native_ea::Image image;
    std::memcpy(&image,encoded,sizeof image);
    std::array<std::uint32_t,16> state;
    std::copy(std::begin(regs.regs),std::end(regs.regs),state.begin());
    struct Memory {
        bool code_allowed(std::uint32_t address,unsigned size) {
            static const bool rom=aranym_qualified_jit_gate("ARANYM_UPSTREAM_JIT_ROM_CODE");
            const auto end=std::uint64_t(address)+size;
            if(!(regs.tc&0x8000)&&rom&&address>=ROMBase&&end<=std::uint64_t(ROMBase)+ROMSize)return true;
            return allowed(address,size,false);
        }
        bool allowed(std::uint32_t address,unsigned size,bool write) {
            const auto physical=address<0xff000000?address:address&0x00ffffff;
            return !(regs.tc&0x8000)&&(physical&0xfff00000)!=0x00f00000&&
                test_ram_boundary(physical,size,regs.s,write);
        }
        bool peek32(std::uint32_t address,std::uint32_t &value) {
            // The planner already checked this pointer transfer as ordinary
            // RAM. No device callback or guest register change is permitted.
            value=phys_get_long(address);return true;
        }
    } memory;
    if constexpr(Multiple)return modern040::native_ea::movem_allowed(image,state,memory)?1:0;
    else return modern040::native_ea::allowed(image,state,memory)?1:0;
}
}
// Only existing native control/stack bodies are selected. This guard never
// calls a device. Stack/indirect peeks occur only after proving plain RAM.
uae_u32 REGPARAM2 aranym_upstream_control_guard(const void *encoded) {
    if(regs.tc&0x8000)return 0;
    modern040::native_ea::Image image;
    std::memcpy(&image,encoded,sizeof image);
    if(!image.count||image.count>image.words.size())return 0;
    const uae_u32 opcode=image.words[0];uae_u32 target=0;
    const auto stack_ram=[](uae_u32 address,bool write) {
        const uint64_t end=uint64_t(address)+4;
        const bool backing=end<=STRAM_END||(address>=FastRAMBase&&end<=uint64_t(FastRAMBase)+FastRAMSize);
        return backing&&address<0xff000000&&test_ram_boundary(address,4,regs.s,write);
    };
    if(opcode==0x4e75) {
        if(!stack_ram(regs.regs[15],false))return 0;
        target=phys_get_long(regs.regs[15]);
    } else if((opcode&0xfff8)==0x4e50) {
        return image.count>=2&&stack_ram(regs.regs[15]-4,true);
    } else if((opcode&0xfff8)==0x4e58) {
        return stack_ram(regs.regs[8+(opcode&7)],false);
    } else {
        std::array<std::uint32_t,16> state;
        std::copy(std::begin(regs.regs),std::end(regs.regs),state.begin());
        struct Memory {
            bool code_allowed(std::uint32_t address,unsigned size) {
                static const bool rom=aranym_qualified_jit_gate("ARANYM_UPSTREAM_JIT_ROM_CODE");
                const auto end=std::uint64_t(address)+size;
                if(rom&&address>=ROMBase&&end<=std::uint64_t(ROMBase)+ROMSize)return true;
                return allowed(address,size,false);
            }
            bool allowed(std::uint32_t address,unsigned size,bool write) {
                const auto physical=address<0xff000000?address:address&0x00ffffff;
                return (physical&0xfff00000)!=0x00f00000&&test_ram_boundary(physical,size,regs.s,write);
            }
            bool peek32(std::uint32_t address,std::uint32_t &value) {
                value=phys_get_long(address);return true;
            }
        } memory;
        if(!modern040::native_ea::control_address(image,state,memory,target))return 0;
        if((opcode&0xffc0)==0x4840)
            return stack_ram(regs.regs[15]-4,true); // PEA
        if((opcode&0xffc0)==0x4e80&&!stack_ram(regs.regs[15]-4,true))return 0; // JSR
    }
    // Avoid an odd/unsafe native target or a wrap/mirrored PC. The genuine
    // interpreter preserves the exact fault/stack ordering in those cases.
    const uint64_t target_end=uint64_t(target)+2;
    const bool target_backed=target_end<=STRAM_END||
        (target>=FastRAMBase&&target_end<=uint64_t(FastRAMBase)+FastRAMSize)||
        (target>=ROMBase&&target_end<=uint64_t(ROMBase)+ROMSize);
    if(!target_backed||(target&1)||target>=0xff000000||(target&0xfff00000)==0x00f00000||
       !test_ram_boundary(target,2,regs.s,false))return 0;
    return 1;
}
uae_u32 REGPARAM2 aranym_upstream_ram_complex_guard(const void *encoded) {
    return native_ram_plan<false>(encoded);
}
uae_u32 REGPARAM2 aranym_upstream_ram_movem_guard(const void *encoded) {
    return native_ram_plan<true>(encoded);
}
void aranym_upstream_memory_deopt() {
    aranym_upstream_modern_step(GET_OPCODE);
    cpu_check_ticks();
}

// Default-off scalar MOVE pilot: actual imported compiler bodies calculate EA
// and flags; these helpers perform one existing checked bus operation. A failed
// transfer stops explicitly; generic architectural fault/restart is not admitted.
namespace {
uint32_t checked_native_transfer(uint32_t address,unsigned size,bool write,uint32_t value) {
    if(write&&frame_irq_profile&&!frame_irq_skip_native_notify) {
        const m040_bus_request request{M040_ABI_VERSION,M040_WRITE,address,size,regs.s?5u:1u,0,0,0};
        checked(m040_frame_write_notify(&request),"invalidate native RAM-store frame identity before transfer");
    }
    static const bool guarded=!std::getenv("ARANYM_UPSTREAM_JIT_RAM_MOVE_OFF")&&!std::getenv("ARANYM_UPSTREAM_JIT_CHECKED_MOVE");
    if(guarded) {
        const uae_u32 physical=address<0xff000000?address:address&0x00ffffff;
        if((physical&0xfff00000)==0x00f00000||!test_ram_boundary(physical,size,regs.s,write))
            fail("guarded RAM helper received an address not admitted by its instruction guard");
        // Same existing endian-aware RAM accessors, without a per-transfer
        // setjmp/device dispatch around an already validated RAM operation.
        if(write) {
            if(size==1)phys_put_byte(address,value);
            else if(size==2)phys_put_word(address,value);
            else phys_put_long(address,value);
        } else {
            if(size==1)value=phys_get_byte(address);
            else if(size==2)value=phys_get_word(address);
            else value=phys_get_long(address);
        }
        return value;
    }
    const m040_bus_request request={M040_ABI_VERSION,uint32_t(write?M040_WRITE:M040_READ),
        address,size,regs.s?5u:1u,0,0,0};
    if(memory(nullptr,&request,&value)!=M040_BUS_COMPLETED) {
        m040_result result={};result.abi_version=M040_ABI_VERSION;
        result.exit=M040_MEMORY_FAULT;result.instruction_pc=regs.fault_pc;
        result.current_pc=m68k_getpc();result.address=address;result.size=size;
        fail("checked MOVE pilot transfer not completed; no replay/restart",&result);
    }
    return value;
}
}
uae_u32 REGPARAM2 aranym_upstream_checked_read_b(uae_u32 address){return checked_native_transfer(address,1,false,0);}
uae_u32 REGPARAM2 aranym_upstream_checked_read_w(uae_u32 address){return checked_native_transfer(address,2,false,0);}
uae_u32 REGPARAM2 aranym_upstream_checked_read_l(uae_u32 address){return checked_native_transfer(address,4,false,0);}
void REGPARAM2 aranym_upstream_checked_write_b(uae_u32 address,uae_u32 value){checked_native_transfer(address,1,true,value);}
void REGPARAM2 aranym_upstream_checked_write_w(uae_u32 address,uae_u32 value){checked_native_transfer(address,2,true,value);}
void REGPARAM2 aranym_upstream_checked_write_l(uae_u32 address,uae_u32 value){checked_native_transfer(address,4,true,value);}
