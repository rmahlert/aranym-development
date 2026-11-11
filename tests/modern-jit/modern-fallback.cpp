/* SPDX-License-Identifier: GPL-2.0-or-later
 * Derived from cpu-core-upgrade/platform/adapter.cpp (669bb6c0).
 * Modern interpreter fallback for the experimental full native x86 JIT.
 * No MMU/restart/desktop/cycle-performance acceptance.
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
#include "fpu/fpu.h"
#include "../cpu-core-upgrade/runner/c-api.h"
#include "../cpu-core-upgrade/platform/adapter.h"
#include "compiler/compemu.h"
#include <cstdio>
#include <cstdlib>
extern uae_u32 aranym_jit_alu_probe_begin,aranym_jit_alu_probe_end;
extern uae_u32 aranym_upstream_alu_count(unsigned);
extern uae_u32 aranym_jit_ea_probe_begin,aranym_jit_ea_probe_end;
extern uae_u32 aranym_upstream_ea_count(unsigned);
namespace {
bool initialized=false;unsigned depth=0;unsigned long long attempted=0;
bool legacy_fpu=false,service_exception_delivered=false;
bool diagnostics=false;uint64_t last_exception=0;unsigned service_messages=0,guest_messages=0,guest_traps=0;
void checked(int,const char *);
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
void from_old(bool preserve_instruction=false) {
    m040_state state;checked(m040_get_state(&state),"read modern state");MakeSR();
    for(unsigned i=0;i<16;++i)state.regs[i]=regs.regs[i];
    state.pc=m68k_getpc();if(!preserve_instruction)state.instruction_pc=state.pc;state.sr=regs.sr;
    state.usp=regs.usp;state.isp=regs.isp;state.msp=regs.msp;
    if(!regs.s)state.usp=regs.regs[15];else if(regs.m)state.msp=regs.regs[15];else state.isp=regs.regs[15];
    state.stopped=regs.stopped;state.vbr=regs.vbr;state.sfc=regs.sfc;state.dfc=regs.dfc;
    state.cacr=regs.cacr;state.caar=regs.caar;state.itt0=regs.itt0;state.itt1=regs.itt1;state.dtt0=regs.dtt0;state.dtt1=regs.dtt1;
    state.tcr=regs.tc;state.mmusr=regs.mmusr;state.urp=regs.urp;state.srp=regs.srp;
    checked(m040_set_state(&state),"import original architectural state");
}
void to_old() {
    m040_state state;checked(m040_get_state(&state),"export modern architectural state");
    for(unsigned i=0;i<16;++i)regs.regs[i]=state.regs[i];
    regs.usp=state.usp;regs.isp=state.isp;regs.msp=state.msp;
    regs.s=bool(state.sr&0x2000);regs.m=bool(state.sr&0x1000);regs.sr=state.sr;
    MakeFromSR();regs.stopped=state.stopped;regs.vbr=state.vbr;regs.sfc=state.sfc;regs.dfc=state.dfc;
    regs.cacr=state.cacr;regs.caar=state.caar;regs.itt0=state.itt0;regs.itt1=state.itt1;regs.dtt0=state.dtt0;regs.dtt1=state.dtt1;
    regs.tc=state.tcr;regs.mmusr=state.mmusr;regs.urp=state.urp;regs.srp=state.srp;
    m68k_setpc(state.pc);regs.fault_pc=state.instruction_pc;
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
    else if(offset==0x2c)*value=std::getenv("ARANYM_UPSTREAM_JIT_RAM_MOVE")!=nullptr;
    else if(offset>=0x40 && offset<0x60)*value=aranym_upstream_ea_count((offset-0x40)/4);
    else return 0;
    return 1;
}
int memory(void *,const m040_bus_request *request,uint32_t *value) {
    const int ea=ea_fixture(request,value);
    if(ea>=0)return ea;
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
    int ok=1;
    TRY(prb) {
        if(request->kind==M040_WRITE) {
            if(request->size==1)phys_put_byte(request->address,*value);
            else if(request->size==2)phys_put_word(request->address,*value);
            else if(request->size==4)phys_put_long(request->address,*value);
            else ok=0;
        } else {
            if(request->size==1)*value=phys_get_byte(request->address);
            else if(request->size==2)*value=phys_get_word(request->address);
            else if(request->size==4)*value=phys_get_long(request->address);
            else ok=0;
        }
    } CATCH(prb) {ok=0;}
    regs.s=saved_super;return ok;
}
void initialize() {
    if(initialized)return;
#if defined(FULLMMU) || defined(HW_SIGSEGV) || !defined(EXPERIMENTAL_UPSTREAM_MODERN040)
#error This derived adapter requires explicit upstream-JIT/modern040 profile without MMU or signal addressing
#endif
#ifdef MODERN040_DEFAULT_LEGACY_FPU
    legacy_fpu=true;
#else
    legacy_fpu=std::getenv("ARANYM_MODERN040_LEGACY_FPU")!=0;
#endif
    const m040_options options={M040_ABI_VERSION,1,uint32_t(!legacy_fpu),0};
    diagnostics=std::getenv("ARANYM_MODERN040_DIAGNOSTICS")!=0;
    checked(m040_reset(&options),"reset modern core");checked(m040_set_precise_bus(memory,0),"attach precise Atari physical bus");
    checked(m040_fault_recovery(1),"enable restart-aware physical-bus integer bodies");
    checked(m040_set_reset_handler([](void *){AtariReset();return 1;},0),"attach original Atari RESET hardware/NatFeat/input path");
    checked(m040_legacy_fpu_operations(legacy_fpu),"select explicit original FPU backend");
    checked(m040_private_operations(1),"enable ARAnyM extension decoder");initialized=true;
    checked(m040_read_probe_exceptions(1),"enable bounded trace-free pure-read probe fault delivery");
    std::fprintf(stderr,"MODERN040_EXPERIMENTAL source=d82bc254 profile=%s noMMU native-JIT-boundary fault-restart=precise-physical-bus\n",legacy_fpu?"modern-integer-original-ieee-fpu":"integer-lc040-probes");
}
bool acceptable(const m040_result &result){return result.exit==M040_BUDGET||result.exit==M040_STOPPED||result.exit==M040_HOST_RETURN;}
void service(m040_result &result) {
    m040_service call;checked(m040_get_service(&call),"read private instruction metadata");
    const bool trace_service=diagnostics&&call.kind<=M040_NATFEAT_CALL&&(service_messages++<128||call.pc>=0x01000000);
    if(trace_service)std::fprintf(stderr,"MODERN040_SERVICE begin kind=%u pc=%08x stack=%08x d0=%08x sr=%04x\n",call.kind,call.pc,call.stack,regs.regs[0],regs.sr);
    service_exception_delivered=false;
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
            m040_state state;checked(m040_get_state(&state),"read external service exception target");
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
    to_old();
    if(trace_service)std::fprintf(stderr,"MODERN040_SERVICE end kind=%u pc=%08x d0=%08x sr=%04x\n",call.kind,unsigned(m68k_getpc()),regs.regs[0],regs.sr);
}
}
void modern040_reset_requested(){initialized=false;}
bool modern040_restart_pending() {
    if(!initialized)return false;
    int pending=0;checked(m040_restart_pending(&pending),"read restart boundary");return pending;
}
void modern040_external_exception(int vector) {
    if(!initialized)fail("external exception before initialization");
    m040_service pending;const bool service_pending=m040_get_service(&pending)==M040_OK;
    const bool fpu=service_pending&&pending.kind==M040_LEGACY_FPU;
    from_old(fpu);m040_result result;checked(m040_exception(vector,&result),"external exception");to_old();
    if(service_pending)service_exception_delivered=true;
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
        if(diagnostics&&guest_messages<64&&m68k_getpc()>=FastRAMBase&&m68k_getpc()<FastRAMBase+FastRAMSize) {
            // Bounded own-emulator instruction history in mapped guest RAM,
            // never MMIO and no external debugger/process-memory access.
            std::fprintf(stderr,"MODERN040_GUEST pc=%08x op=%04x d0=%08x a7=%08x sr=%04x\n",unsigned(m68k_getpc()),unsigned(phys_get_word(m68k_getpc())),regs.regs[0],regs.regs[15],regs.sr);++guest_messages;
        }
        m040_result result;checked(m040_run(1,&result),"bounded modern dispatch");attempted+=result.instructions;to_old();
        if(result.exit==M040_BUDGET&&result.vector==2)
            std::fprintf(stderr,"MODERN040_PROBE_DELIVERED instruction=%08x fault=%08x handler=%08x\n",result.instruction_pc,result.address,result.current_pc);
        if(result.exit==M040_HOST_SERVICE)service(result);
        diagnostic_exception();
        if(diagnostics&&attempted&&attempted%1000000==0)std::fprintf(stderr,"MODERN040_PROGRESS attempts=%llu pc=%08x sr=%04x a7=%08x\n",attempted,unsigned(m68k_getpc()),regs.sr,regs.regs[15]);
        if(!acceptable(result))fail("modern dispatch unsupported",&result);
        if(result.exit==M040_HOST_RETURN||quit_program>0)return;
        cpu_check_ticks();regs.fault_pc=m68k_getpc();
        if(SPCFLAGS_TEST(SPCFLAG_ALL_BUT_EXEC_RETURN)&&m68k_do_specialties())return;
        from_old();
    }
}

// One genuine modern interpreter operation at the native JIT fallback boundary.
// Architectural state crosses the C ABI only here, not around translated ops.
void REGPARAM2 aranym_upstream_modern_step(uae_u32 encoded_opcode) {
    if(depth)fail("nested JIT fallback guest execution not qualified");
    struct Depth{Depth(){++depth;}~Depth(){--depth;}}guard;
    initialize();from_old();
    const uae_u32 old_cacr=regs.cacr;
    m040_result result;checked(m040_run(1,&result),"modern JIT fallback");
    attempted+=result.instructions;to_old();
    if(result.exit==M040_HOST_SERVICE)service(result);
    if(result.vector||regs.t1||regs.t0)SPCFLAGS_SET(SPCFLAG_JIT_END_COMPILE);
    diagnostic_exception();
    if(!acceptable(result))fail("modern JIT fallback unsupported",&result);
    // Core cache helpers have empty synchronous interpreter caches. Propagate
    // their architectural control to the real native code cache explicitly.
    if(regs.cacr!=old_cacr)set_cache_state(regs.cacr&0x8000);
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
void aranym_upstream_memory_deopt() {
    aranym_upstream_modern_step(GET_OPCODE);
    cpu_check_ticks();
}

// Default-off scalar MOVE pilot: actual imported compiler bodies calculate EA
// and flags; these helpers perform one existing checked bus operation. A failed
// transfer stops explicitly; generic architectural fault/restart is not admitted.
namespace {
uint32_t checked_native_transfer(uint32_t address,unsigned size,bool write,uint32_t value) {
    static const bool guarded=std::getenv("ARANYM_UPSTREAM_JIT_RAM_MOVE")&&!std::getenv("ARANYM_UPSTREAM_JIT_CHECKED_MOVE");
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
