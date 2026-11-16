/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef ARANYM_ARM64_ADAPTER_H
#define ARANYM_ARM64_ADAPTER_H
#include "sysdeps.h"
#include "cpu_emulation.h"
#include "main.h"
#include "memory-uae.h"
#include "newcpu.h"
#include "parameters.h"
#include "readcpu.h"
#include "vm_alloc.h"
#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <sys/mman.h>
#if defined(__APPLE__)
#include <pthread.h>
#include <libkern/OSCacheControl.h>
#endif
#define ARANYM_ARM64 1
#define CPU_AARCH64 1
#define AMIBERRY 1 // Retain upstream ARM64 synchronous-exception code paths.
#ifndef STATIC_INLINE
#define STATIC_INLINE static inline
#endif
#define NATMEM_OFFSET MEMBaseDiff
#define S_READ 1
#define S_WRITE 2
#define S_N_ADDR 4
inline int special_mem = S_READ | S_WRITE | S_N_ADDR;
inline constexpr int jit_n_addr_unsafe=0, jit_n_addr_bank_unsafe=0;
struct aranym_arm64_preferences {
    int cpu_model=68040, cachesize=0;
    bool comp_constjump=false, comp_hardflush=true, compfpu=false, compnf=true;
    int comptrustbyte=1,comptrustword=1,comptrustlong=1,comptrustnaddr=1;
    bool address_space_24=false,cpu_compatible=false,cputester=false;
    int uaeboard=0;
};
inline aranym_arm64_preferences currprefs, changed_prefs;
inline int aranym_arm64_dispatch_budget=0;
#define countdown aranym_arm64_dispatch_budget
// The native dispatcher already accounts for the interpreted trace. Refresh
// ARAnyM's real host timer after compilation, as its existing JIT does.
#define do_extra_cycles(cycles) cpu_do_check_ticks()
void aranym_arm64_load_preferences();
void aranym_arm64_log(const char *format,...);
void aranym_arm64_interpret(uae_u32 opcode);
void aranym_arm64_emit_instruction_pc(uae_u32 pc);
void aranym_arm64_emit_execution_witness(uae_u32 pc);
void aranym_arm64_report_execution();
#define write_log aranym_arm64_log
#define _T(text) text
static inline uae_u32 uae_bswap_32(uae_u32 value) { return SDL_Swap32(value); }
static inline uae_u16 uae_bswap_16(uae_u16 value) { return SDL_Swap16(value); }
struct aranym_arm64_bank {
    uae_u32 (*lget)(uae_u32), (*wget)(uae_u32), (*bget)(uae_u32);
    void (*lput)(uae_u32,uae_u32), (*wput)(uae_u32,uae_u32), (*bput)(uae_u32,uae_u32);
    uae_u8 *(*xlateaddr)(uae_u32);
};
extern aranym_arm64_bank *mem_banks[65536];
void *aranym_arm64_code_acquire(size_t size);
static inline void *vm_acquire_code(uae_u32 size,int=VM_MAP_DEFAULT) { return aranym_arm64_code_acquire(size); }
static inline void uae_vm_jit_write_protect(bool enabled) {
#if defined(__APPLE__)
    pthread_jit_write_protect_np(enabled);
#else
    (void)enabled; // POSIX mapping is executable and writable.
#endif
}
static inline size_t uae_vm_page_size() { return getpagesize(); }
static inline void *aranym_arm64_meta_acquire(size_t size) {
    return mmap(nullptr,size,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);
}
#define vm_acquire(size,options) aranym_arm64_meta_acquire(size)
static inline void set_special(uae_u32 bits) { SPCFLAGS_SET(bits|SPCFLAG_JIT_END_COMPILE); }
#endif
