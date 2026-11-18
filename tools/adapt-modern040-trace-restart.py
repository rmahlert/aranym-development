#!/usr/bin/env python3
"""Adapt the pinned RTE CT helper to the explicit physical-bus trace boundary.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import argparse,hashlib,json
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
BEFORE='0763136247fd9101659d7268724f1fa567460a1f13a06d1495d3ab7432151631'
def digest(text):return hashlib.sha256(text.encode()).hexdigest()
def correct(text):
    if digest(text)!=BEFORE:raise ValueError('unexpected CT restart preimage; no output written')
    start=text.index('void m68k_do_rte_mmu040 (');end=text.index('\n}',start)+2;before=text[start:end]
    old='''\tif (ssr & MMU_SSW_CT) {
\t\tuaecptr src_a7 = a7 + 8 - 8;
\t\tuaecptr dst_a7 = a7 + 8 + 52;
\t\tput_word_mmu040 (dst_a7 + 0, get_word_mmu040 (src_a7 + 0));
\t\tput_long_mmu040 (dst_a7 + 2, get_long_mmu040 (src_a7 + 2));
\t\t// skip this word
\t\tput_long_mmu040 (dst_a7 + 8, get_long_mmu040 (src_a7 + 8));
\t}'''
    assert before.count(old)==1
    after=before.replace(old,'\tif (ssr & MMU_SSW_CT) {\n\t\trecord_rte_trace(get_long_mmu040(a7 + 8));\n\t}')
    text=text[:start]+after+text[end:]
    text=text.replace('// Pinned WinUAE bodies plus explicit MOVEM CM cursor correction; GPL-2.0-or-later.',
        '// Pinned WinUAE bodies plus explicit physical-bus restart corrections; GPL-2.0-or-later.',1)
    return text,dict(upstream_pin='d82bc2549f27943d11dc59482d5388daf1c0f337',before_sha256=BEFORE,after_sha256=digest(text),
        functions=[dict(function='m68k_do_rte_mmu040',before_sha256=digest(before),after_sha256=digest(after))],
        correction='Record the pending traced instruction EA while the genuine RTE restores SR/PC/banks. The physical-bus run boundary then uses normal Exception9 to build format2 at oldSP+48. No writes above the original stack top and no following opcode before trace.',
        instruction_bodies_unchanged=True,
        manual=dict(path='MC68040UM.pdf',sha256='aed5a038586dd2f610068efa01919fc552dc65bfd1a9274a333bad29dbd48894',page=238),
        scope='Integer CT only. FPU CP/CU continuation remains separate; no pin/default/installer admission change.')
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--input',type=Path,default=ROOT/'src/modern040/restart.cpp');p.add_argument('--output',type=Path);p.add_argument('--metadata-output',type=Path);p.add_argument('--apply',action='store_true');a=p.parse_args();text,record=correct(a.input.read_text())
    if a.apply:(a.output or a.input).write_text(text)
    if a.apply and a.metadata_output:a.metadata_output.write_text(json.dumps(record,indent=2)+'\n')
    print(json.dumps(record,indent=2))
if __name__=='__main__':main()
