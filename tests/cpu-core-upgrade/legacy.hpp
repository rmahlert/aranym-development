/* SPDX-License-Identifier: GPL-2.0-or-later
 * Explicit bindings for unmodified c7620462 generated handlers. The helper
 * ordering below follows src/uae_cpu/newcpu.h:207-237. No real host pointers,
 * MMU, physical Atari bus, exception frames or interpreter event loop are used.
 */
#pragma once
#include "adapter.hpp"
#define CPUFUNC(name) legacy_##name
#define cpuop_begin() do {} while (0)
#define cpuop_end() do {} while (0)
// The common snapshot slot is a backend-specific bookkeeping cursor: legacy
// fault_pc, modern instruction_pc. Tests audit exact differences explicitly.
#define fault_pc instruction_pc
struct LegacyFixup { int flag = 0; uae_u32 reg = 0, value = 0; };
inline LegacyFixup fixup;
inline uae_u32 get_byte(uae_u32 address) { return get_byte_jit(address); }
inline uae_u32 get_word(uae_u32 address) { return get_word_jit(address); }
inline uae_u32 get_long(uae_u32 address) { return get_long_jit(address); }
inline uae_u32 phys_get_byte(uae_u32 address) { return get_byte_jit(address); }
inline uae_u32 phys_get_word(uae_u32 address) { return get_word_jit(address); }
inline uae_u32 phys_get_long(uae_u32 address) { return get_long_jit(address); }
inline void put_byte(uae_u32 address, uae_u32 value) { put_byte_jit(address, value); }
inline void put_word(uae_u32 address, uae_u32 value) { put_word_jit(address, value); }
inline void put_long(uae_u32 address, uae_u32 value) { put_long_jit(address, value); }
inline void legacy_setpc(uae_u32 pc) { regs.instruction_pc = regs.pc = pc; }
inline void legacy_rts() {
    legacy_setpc(get_long(m68k_areg(regs, 7)));
    m68k_areg(regs, 7) += 4;
}
inline void legacy_jsr(uae_u32 return_pc, uae_u32 destination) {
    put_long(m68k_areg(regs, 7) - 4, return_pc);
    m68k_areg(regs, 7) -= 4;
    legacy_setpc(destination);
}
#define m68k_setpc legacy_setpc
#define m68k_do_rts legacy_rts
#define m68k_do_jsr legacy_jsr
