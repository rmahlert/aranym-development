#!/usr/bin/env python3
"""Integrate a scoped explicit physical WB1 into the pinned 040 frame helper.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import argparse,hashlib,json
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
BEFORE='e4268caf87aca6841be8ff5573cf814b4cdf6dfe5ee9b89c2ea249eb1c719023'
def digest(text):return hashlib.sha256(text.encode()).hexdigest()
def correct(text):
    if digest(text)!=BEFORE:raise ValueError('unexpected integer/frame preimage; no output written')
    start=text.index('void Exception_build_stack_frame(');end=text.index('\n}',start)+2
    before=text[start:end];after=before
    substitutions=[
        ('x_put_long(m68k_areg(regs, 7), mmu040_move16[i]);',
         'x_put_long(m68k_areg(regs, 7), i==0&&host.physical_writeback?host.physical_writeback->data:mmu040_move16[i]);'),
        ('x_put_long(m68k_areg(regs, 7), 0); // WB1A',
         'x_put_long(m68k_areg(regs, 7), host.physical_writeback?host.physical_writeback->address:0); // WB1A'),
        ('x_put_word(m68k_areg(regs, 7), 0);\n\t\tm68k_areg(regs, 7) -= 2;\n\t\tx_put_word(m68k_areg(regs, 7), regs.wb2_status);',
         'x_put_word(m68k_areg(regs, 7), host.physical_writeback?host.physical_writeback->status:0); // WB1S\n\t\tm68k_areg(regs, 7) -= 2;\n\t\tx_put_word(m68k_areg(regs, 7), regs.wb2_status);')]
    for old,new in substitutions:
        if after.count(old)!=1:raise ValueError('frame helper write-site drift')
        after=after.replace(old,new,1)
    result=text[:start]+after+text[end:]
    result=result.replace('// Mechanically extracted, unmodified pinned WinUAE/UAE generated bodies and helpers.',
        '// Pinned WinUAE/UAE bodies plus explicit scoped physical-WB1 frame adaptation.',1)
    return result,dict(upstream_pin='d82bc2549f27943d11dc59482d5388daf1c0f337',before_sha256=BEFORE,after_sha256=digest(result),
        helper=dict(function='Exception_build_stack_frame',before_sha256=digest(before),after_sha256=digest(after)),
        instruction_bodies_unchanged=True,legacy_frame_bytes_unchanged_without_scoped_writeback=True,
        manual=dict(path='MC68040UM.pdf',sha256='aed5a038586dd2f610068efa01919fc552dc65bfd1a9274a333bad29dbd48894',pdf_pages=[237,239,240,244]),
        correction='Only the scoped physical-write delivery supplies WB1A/WB1S/WB1D. Legacy MOVE16/PD0 and zero invalid WB1 slots remain unchanged when absent. The delivery boundary owns origin, whole-scalar no-commit, completion and lane validation.',
        scope='One certified completed scalar ExternalBus write only; not a write queue, ATC, MMU, trace/FPU, native or frontend integration.')
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--input',type=Path,default=ROOT/'src/modern040/integer.cpp');p.add_argument('--output',type=Path);p.add_argument('--metadata-output',type=Path);p.add_argument('--apply',action='store_true');a=p.parse_args()
    text,record=correct(a.input.read_text())
    if a.apply:
        (a.output or a.input).write_text(text)
    if a.apply and a.metadata_output:a.metadata_output.write_text(json.dumps(record,indent=2)+'\n')
    print(json.dumps(record,indent=2))
if __name__=='__main__':main()
