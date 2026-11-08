/* SPDX-License-Identifier: GPL-2.0-or-later
 * Experimental correctness-first interpreter bridge. No JIT/MMU/cycle claim.
 */
#include "sysdeps.h"
#include "main.h"
#include "cpu_emulation.h"
#include "newcpu.h"
#include "../runner/c-api.h"
#include "adapter.h"
#include <cstdio>
namespace {
bool initialized=false;unsigned depth=0;unsigned long long attempted=0;
void fail(const char *where,const m040_result *result=0) {
    std::fprintf(stderr,"MODERN040_UNSUPPORTED where=%s oldpc=%08x attempts=%llu",where,unsigned(m68k_getpc()),attempted);
    if(result)std::fprintf(stderr," exit=%u vector=%u instruction=%08x current=%08x address=%08x size=%u detail=%s",result->exit,result->vector,result->instruction_pc,result->current_pc,result->address,result->size,result->detail);
    std::fprintf(stderr," sr=%04x d0=%08x tc=%08x itt0=%08x itt1=%08x dtt0=%08x dtt1=%08x",regs.sr,regs.regs[0],regs.tc,regs.itt0,regs.itt1,regs.dtt0,regs.dtt1);
    std::fputc('\n',stderr);std::fflush(stderr);QuitEmulator(3);
}
void checked(int status,const char *where){if(status!=M040_OK)fail(where);}
void from_old() {
    m040_state state;checked(m040_get_state(&state),"read modern state");MakeSR();
    for(unsigned i=0;i<16;++i)state.regs[i]=regs.regs[i];
    state.pc=state.instruction_pc=m68k_getpc();state.sr=regs.sr;
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
int memory(void *,const m040_bus_request *request,uint32_t *value) {
    if(request->transfer_type!=0)return 0; // no invented alternate CPU space.
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
#if defined(USE_JIT) || defined(FULLMMU) || defined(HW_SIGSEGV)
#error Experimental modern040 requires JIT-off/MMU-off/normal addressing
#endif
    const m040_options options={M040_ABI_VERSION,1,1,0};
    checked(m040_reset(&options),"reset modern core");checked(m040_set_bus(memory,0),"attach original Atari physical bus");
    checked(m040_set_reset_handler([](void *){AtariReset();return 1;},0),"attach original Atari RESET hardware/NatFeat/input path");
    checked(m040_private_operations(1),"enable ARAnyM extension decoder");initialized=true;
    checked(m040_read_probe_exceptions(1),"enable bounded trace-free TST.B(An) probe fault delivery");
    std::fprintf(stderr,"MODERN040_EXPERIMENTAL source=d82bc254 profile=integer-lc040-probes noMMU noJIT fault-restart=unsupported\n");
}
bool acceptable(const m040_result &result){return result.exit==M040_BUDGET||result.exit==M040_STOPPED||result.exit==M040_HOST_RETURN;}
void service(m040_result &result) {
    m040_service call;checked(m040_get_service(&call),"read private instruction metadata");
    TRY(prb) {
        switch(call.kind) {
        case M040_NATFEAT_ID:m68k_natfeat_id();break;
        case M040_NATFEAT_CALL:m68k_natfeat_call();break;
        case M040_EMULOP:m68k_emulop(call.opcode);break;
        case M040_EMULOP_RETURN:m68k_emulop_return();break;
        default:fail("unknown private instruction kind");
        }
        from_old();checked(m040_complete_service(&result),"complete private source body");
    } CATCH(prb) {
        from_old();checked(m040_exception(int(prb),&result),"NatFeat host exception boundary");
    }
    to_old();
}
}
void modern040_reset_requested(){initialized=false;}
void modern040_external_exception(int vector) {
    if(!initialized)fail("external exception before initialization");
    from_old();m040_result result;checked(m040_exception(vector,&result),"external exception");to_old();
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
        m040_result result;checked(m040_run(1,&result),"bounded modern dispatch");attempted+=result.instructions;to_old();
        if(result.exit==M040_BUDGET&&result.vector==2)
            std::fprintf(stderr,"MODERN040_PROBE_DELIVERED instruction=%08x fault=%08x handler=%08x\n",result.instruction_pc,result.address,result.current_pc);
        if(result.exit==M040_HOST_SERVICE)service(result);
        if(!acceptable(result))fail("modern dispatch unsupported",&result);
        if(result.exit==M040_HOST_RETURN||quit_program>0)return;
        cpu_check_ticks();regs.fault_pc=m68k_getpc();
        if(SPCFLAGS_TEST(SPCFLAG_ALL_BUT_EXEC_RETURN)&&m68k_do_specialties())return;
        from_old();
    }
}
