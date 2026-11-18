/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include "core.hpp"
namespace modern040 {
constexpr unsigned CYCLE_UNIT=512;
constexpr unsigned MMU_SSW_CT=0x2000,MMU_SSW_CM=0x1000;
constexpr unsigned SPCFLAG_MMURESTART=32;
struct MmuFixup { int reg=-1;uae_u32 value=0; };
extern MmuFixup mmufixup[2];
extern bool mmu_restart;
extern int mmu040_movem;
extern uae_u32 mmu040_movem_ea;
extern std::array<Handler,65536> restart_handlers;
uae_u32 get_bitfield(uae_u32,uae_u32 *,uae_s32,int);
void put_bitfield(uae_u32,uae_u32 *,uae_u32,uae_s32,int);
void divbyzero_special(bool,uae_s32);
void setdivuflags(uae_u32,uae_u16);
void setdivsflags(uae_s32,uae_s16);
void setchkundefinedflags(uae_s32,uae_s32,int);
void setchk2undefinedflags(uae_s32,uae_s32,uae_s32,int);
void op_unimpl(uae_u32);
int m68k_divl(uae_u32,uae_u32,uae_u16,uaecptr);
int m68k_mull(uae_u32,uae_u32,uae_u16);
uae_u32 sfc_nommu_get_byte(uaecptr);
uae_u32 sfc_nommu_get_word(uaecptr);
uae_u32 sfc_nommu_get_long(uaecptr);
void dfc_nommu_put_byte(uaecptr,uae_u32);
void dfc_nommu_put_word(uaecptr,uae_u32);
void dfc_nommu_put_long(uaecptr,uae_u32);
void install_restart_handlers();
void cpu_restore_fixup();
void m68k_do_rte_mmu040(uaecptr);
void record_rte_trace(uaecptr) noexcept;
void m68k_do_rts_mmu040();
void m68k_do_bsr_mmu040(uaecptr,uae_s32);
inline uae_u32 m68k_getpci(){return regs.pc;}
inline void m68k_setpci(uae_u32 pc){regs.pc=pc;}
inline uae_u32 get_byte_mmu040(uaecptr a){return get_byte_jit(a);}
inline uae_u32 get_word_mmu040(uaecptr a){return get_word_jit(a);}
inline uae_u32 get_long_mmu040(uaecptr a){return get_long_jit(a);}
inline void put_byte_mmu040(uaecptr a,uae_u32 v){put_byte_jit(a,v);}
inline void put_word_mmu040(uaecptr a,uae_u32 v){put_word_jit(a,v);}
inline void put_long_mmu040(uaecptr a,uae_u32 v){put_long_jit(a,v);}
inline uae_u32 get_ibyte_mmu040(int o){return get_dibyte(o);}
inline uae_u32 get_iword_mmu040(int o){return get_diword(o);}
inline uae_u32 get_ilong_mmu040(int o){return get_dilong(o);}
// CM resumes the saved calculated operand EA, but the instruction cursor must
// still consume its extension stream. Fetch only instruction words: no GPR
// evaluation, effective-address calculation or data-pointer read is repeated.
inline void consume_movem_restart_ea() {
    const auto extension=next_diword();
    if(!(extension&0x100))return;
    if((extension&0x30)==0x20)(void)next_diword();
    else if((extension&0x30)==0x30)(void)next_dilong();
    if((extension&3)==2)(void)next_diword();
    else if((extension&3)==3)(void)next_dilong();
}
inline uaecptr x_get_disp_ea_020(uaecptr a,int i){return get_disp_ea_020(a,i);}
inline void Exception_cpu(int nr){Exception(nr);}
inline uae_u32 x_get_bitfield(uaecptr a,uae_u32 *b,uae_s32 o,int w){return get_bitfield(a,b,o,w);}
inline void x_put_bitfield(uaecptr a,uae_u32 *b,uae_u32 v,uae_s32 o,int w){put_bitfield(a,b,v,o,w);}
inline uae_u32 sfc040_get_byte(uaecptr a){return sfc_nommu_get_byte(a);}
inline uae_u32 sfc040_get_word(uaecptr a){return sfc_nommu_get_word(a);}
inline uae_u32 sfc040_get_long(uaecptr a){return sfc_nommu_get_long(a);}
inline void dfc040_put_byte(uaecptr a,uae_u32 v){dfc_nommu_put_byte(a,v);}
inline void dfc040_put_word(uaecptr a,uae_u32 v){dfc_nommu_put_word(a,v);}
inline void dfc040_put_long(uaecptr a,uae_u32 v){dfc_nommu_put_long(a,v);}
}
