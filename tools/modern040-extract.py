#!/usr/bin/env python3
"""Pinned 68040 source extraction helpers.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import re

CALLS = set("""if while for switch sizeof
    m68k_dreg m68k_areg m68k_getpc m68k_incpc m68k_setpc_j m68k_setpc
    GET_XFLG GET_CFLG GET_NFLG GET_VFLG GET_ZFLG CLEAR_CZNV
    SET_XFLG SET_CFLG SET_NFLG SET_VFLG SET_ZFLG COPY_CARRY
    SET_ALWAYS_CFLG SET_ALWAYS_NFLG get_byte_jit get_word_jit get_long_jit
    put_byte_jit put_word_jit put_long_jit get_dibyte get_diword get_dilong
    get_disp_ea_020 cctrue check_t0_trace MakeSR MakeFromSR MakeFromSR_T0
    MakeFromSR_STOP Exception Exception_cpu_oldpc op_illg m68k_move2c
    m68k_movec2 m68k_setstopped do_cycles_stop checkint m68k_do_rts m68k_do_bsr
    exception3_read_prefetch exception3_read_prefetch_only
    exception3_read_prefetch_68040bug branch_stack_push branch_stack_pop_rts
    branch_stack_pop_rte fpuop_bcc fpuop_save fpuop_restore get_bitfield
    put_bitfield divbyzero_special setdivuflags setdivsflags setchkundefinedflags
    setchk2undefinedflags m68k_mull m68k_divl op_unimpl
    sfc_nommu_get_byte sfc_nommu_get_word sfc_nommu_get_long
    dfc_nommu_put_byte dfc_nommu_put_word dfc_nommu_put_long cpureset
    flush_cpu_caches_040 flush_icache""".split())

def extract(source, name, occurrence=None):
    pattern = r"^.*\b" + re.escape(name) + r"\s*\([^;\n]*\)\n\{"
    matches = list(re.finditer(pattern, source, re.M))
    if occurrence is None and len(matches) != 1:
        raise RuntimeError(f"Expected one function {name}, found {len(matches)}")
    match = matches[0 if occurrence is None else occurrence]
    depth, pos, mode = 1, match.end(), "code"
    while depth:
        if pos == len(source): raise RuntimeError("Unclosed function " + name)
        c, nxt = source[pos], source[pos:pos+2]
        if mode == "comment":
            if nxt == "*/": mode = "code"; pos += 2; continue
        elif mode == "line":
            if c == "\n": mode = "code"
        elif mode in ('"', "'"):
            if c == "\\": pos += 2; continue
            if c == mode: mode = "code"
        elif nxt == "/*": mode = "comment"; pos += 2; continue
        elif nxt == "//": mode = "line"; pos += 2; continue
        elif c in ('"', "'"): mode = c
        elif c == "{": depth += 1
        elif c == "}": depth -= 1
        pos += 1
    return source[match.start():pos]
