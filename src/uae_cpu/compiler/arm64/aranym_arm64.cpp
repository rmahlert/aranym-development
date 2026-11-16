/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "aranym_arm64.h"
#include "compemu_arm.h"
void (*flush_icache)(int)=nullptr;
static inline void create_popalls();

static uae_u32 bank_lget(uae_u32 address) { return get_long(address); }
static uae_u32 bank_wget(uae_u32 address) { return get_word(address); }
static uae_u32 bank_bget(uae_u32 address) { return get_byte(address); }
static void bank_lput(uae_u32 address,uae_u32 value) { put_long(address,value); }
static void bank_wput(uae_u32 address,uae_u32 value) { put_word(address,value); }
static void bank_bput(uae_u32 address,uae_u32 value) { put_byte(address,value); }
static uae_u8 *bank_xlate(uae_u32 address) { return get_real_address(address,0,sz_word); }
static aranym_arm64_bank checked_bank={bank_lget,bank_wget,bank_bget,bank_lput,bank_wput,bank_bput,bank_xlate};
aranym_arm64_bank *mem_banks[65536];

void aranym_arm64_log(const char *format,...) {
    va_list args;va_start(args,format);vfprintf(stderr,format,args);va_end(args);
}
void jit_abort(const char *format,...) {
    va_list args;va_start(args,format);vfprintf(stderr,format,args);va_end(args);
    fputc('\n',stderr);abort();
}
void aranym_arm64_load_preferences() {
    currprefs.cachesize=bx_options.jit.jitcachesize;
    currprefs.comp_constjump=bx_options.jit.jitinline;
    currprefs.comp_hardflush=bx_options.jit.jitlazyflush==0;
    changed_prefs=currprefs;
    for(auto &bank:mem_banks) bank=&checked_bank;
}
void *aranym_arm64_code_acquire(size_t size) {
    int flags=MAP_PRIVATE|MAP_ANONYMOUS;
#if defined(__APPLE__)
    flags|=MAP_JIT;
#endif
    return mmap(nullptr,size,PROT_READ|PROT_WRITE|PROT_EXEC,flags,-1,0);
}

// Compile the complete pinned engine against the native state and bank ABI.
#include "compemu_support_arm.cpp"

static uae_u64 executed_native_ops=0,executed_guest_ops=0;
static uae_u32 last_native_guest_pc=0;
static bool execution_diagnostics() {
    static const bool enabled=[](){const char *p=getenv("ARANYM_ARM64_JIT_DIAGNOSTICS");return p&&strcmp(p,"1")==0;}();
    return enabled;
}
void aranym_arm64_emit_instruction_pc(uae_u32 pc) {
    compemu_raw_mov_l_mi((uintptr)&regs.fault_pc,pc);
}
void aranym_arm64_emit_execution_witness(uae_u32 pc) {
    if(!execution_diagnostics()) return;
    auto increment=[](uae_u64 *counter) {
        LOAD_U64(REG_WORK1,(uintptr)counter);
        LDR_xXi(REG_WORK2,REG_WORK1,0);
        ADD_xxi(REG_WORK2,REG_WORK2,1);
        STR_xXi(REG_WORK2,REG_WORK1,0);
    };
    increment(&executed_native_ops);
    if(pc>=FastRAM_BEGIN&&pc<FastRAM_BEGIN+FastRAM_SIZE) {
        increment(&executed_guest_ops);
        compemu_raw_mov_l_mi((uintptr)&last_native_guest_pc,pc);
    }
}
void aranym_arm64_report_execution() {
    if(execution_diagnostics())
        fprintf(stderr,"ARM64-JIT executed-native=%llu executed-guest=%llu last-guest-pc=%08x\n",
            (unsigned long long)executed_native_ops,(unsigned long long)executed_guest_ops,last_native_guest_pc);
}
void aranym_arm64_interpret(uae_u32 opcode) {
    regs.fault_pc=m68k_getpc();
    cpufunctbl[opcode](opcode);
}

void do_nothing() { cpu_do_check_ticks(); }
void execute_exception(uae_u32) {
    const uae_u32 exception=regs.jit_exception;
    regs.jit_exception=0;
    if(exception) Exception(exception,regs.instruction_pc);
}
void exec_nostats() {
    for(;;) {
        const uae_u32 opcode=GET_OPCODE;
        regs.fault_pc=m68k_getpc();
        cpufunctbl[opcode](opcode);
        cpu_check_ticks();
        if(end_block(opcode)||SPCFLAGS_TEST(SPCFLAG_ALL)) return;
    }
}
void execute_normal() {
    if(check_for_cache_miss()) return;
    cpu_history history[MAXRUN];int count=0;
    start_pc_p=regs.pc_oldp;start_pc=regs.pc;
    for(;;) {
        history[count].location=reinterpret_cast<uae_u16 *>(regs.pc_p);
        history[count++].specmem=S_READ|S_WRITE|S_N_ADDR;
        const uae_u32 opcode=GET_OPCODE;
        regs.fault_pc=m68k_getpc();
        cpufunctbl[opcode](opcode);
        cpu_check_ticks();
        if(end_block(opcode)||SPCFLAGS_TEST(SPCFLAG_ALL)||count>=MAXRUN) {
            compile_block(history,count,count);
            return;
        }
    }
}
void m68k_do_compile_execute() {
    for(;;) {
        if(regs.t1||regs.t0) {
            regs.fault_pc=m68k_getpc();
            const uae_u32 opcode=GET_OPCODE;cpufunctbl[opcode](opcode);cpu_check_ticks();
        } else {
            aranym_arm64_dispatch_budget=4096;
            reinterpret_cast<void (*)()>(pushall_call_handler)();
        }
        if(SPCFLAGS_TEST(SPCFLAG_ALL)&&m68k_do_specialties()) return;
    }
}
void m68k_compile_execute() {
retry:
    TRY(exception) {
        for(;;) {
            if(quit_program>0) {
                if(quit_program==1) break;
                quit_program=0;m68k_reset();
            }
            m68k_do_compile_execute();
        }
    } CATCH(exception) {
        flush_icache(0);Exception(exception,0);goto retry;
    }
}
