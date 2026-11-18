#!/usr/bin/env python3
"""Feed a separately captured unissued IU write into actual format7 WB3A.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import argparse,hashlib,json
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
BEFORE='9f3ca856de77c942f24a7abefda2bc87b7f9d465b37574c6a8e7d7caf3fd6d97'
def digest(text):return hashlib.sha256(text.encode()).hexdigest()
def correct(text):
    if digest(text)!=BEFORE:raise ValueError('unexpected scoped-WB1 preimage; no output written')
    start=text.index('void Exception_build_stack_frame(');end=text.index('\n}',start)+2
    before=text[start:end]
    old='x_put_long(m68k_areg(regs, 7), regs.mmu_fault_addr); // WB3A'
    new='x_put_long(m68k_areg(regs, 7), host.physical_writeback&&host.physical_writeback->iu_write?host.physical_writeback->iu_write->request.address:regs.mmu_fault_addr); // WB3A'
    if before.count(old)!=1:raise ValueError('WB3A frame site drift')
    after=before.replace(old,new,1);result=text[:start]+after+text[end:]
    result=result.replace('// Pinned WinUAE/UAE bodies plus explicit scoped physical-WB1 frame adaptation.',
        '// Pinned WinUAE/UAE bodies plus explicit scoped physical writeback frame adaptations.',1)
    return result,dict(upstream_pin='d82bc2549f27943d11dc59482d5388daf1c0f337',before_sha256=BEFORE,after_sha256=digest(result),
        helper=dict(function='Exception_build_stack_frame',before_sha256=digest(before),after_sha256=digest(after)),
        instruction_bodies_unchanged=True,legacy_and_scalar_wb1_bytes_unchanged_without_iu_write=True,
        manual=dict(path='MC68040UM.pdf',sha256='aed5a038586dd2f610068efa01919fc552dc65bfd1a9274a333bad29dbd48894',pdf_pages=[44,237,239,240,244]),
        correction='Only a separately captured unissued logical IU write supplies WB3A. WB3S/data come from that exact CPU request; WB1 retains explicit failed physical provenance. No WB2 or physical address is invented for the unissued write.',
        scope='Pinned bitfield write-only tail and at most one unissued IU write. Not a generic queue, ATC/MMU, unknown/partial recovery or default/frontend admission.')
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--input',type=Path,default=ROOT/'src/modern040/integer.cpp');p.add_argument('--output',type=Path);p.add_argument('--metadata-output',type=Path);p.add_argument('--apply',action='store_true');a=p.parse_args();text,record=correct(a.input.read_text())
    if a.apply:(a.output or a.input).write_text(text)
    if a.apply and a.metadata_output:a.metadata_output.write_text(json.dumps(record,indent=2)+'\n')
    print(json.dumps(record,indent=2))
if __name__=='__main__':main()
