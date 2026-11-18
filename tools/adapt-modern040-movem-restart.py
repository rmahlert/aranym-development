#!/usr/bin/env python3
"""Apply the bounded CM instruction-cursor correction to six pinned bodies.
SPDX-License-Identifier: GPL-2.0-or-later
"""
import argparse,hashlib,json,re
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
BEFORE='16531ac2411669b01f601105ca3971e4a5c421868402957202d11a99be0c72ad'
NAMES=['op_48b0_31_ff','op_48f0_31_ff','op_4cb0_31_ff','op_4cf0_31_ff','op_4cbb_31_ff','op_4cfb_31_ff']
def digest(text):return hashlib.sha256(text.encode()).hexdigest()
def correct(text):
    if digest(text)!=BEFORE:raise ValueError('unexpected restart preimage; no output written')
    changes=[]
    for name in NAMES:
        start=text.index('uae_u32 REGPARAM2 '+name+'(');end=text.index('\n}',start)+2;before=text[start:end]
        old='if (mmu040_movem) {\n\t\tsrca = mmu040_movem_ea;'
        assert before.count(old)==1,name
        after=before.replace(old,old+'\n\t\tconsume_movem_restart_ea();')
        text=text[:start]+after+text[end:]
        changes.append(dict(function=name,before_sha256=digest(before),after_sha256=digest(after)))
    text=text.replace('// Verbatim pinned WinUAE instruction bodies; GPL-2.0-or-later.',
        '// Pinned WinUAE bodies plus explicit MOVEM CM cursor correction; GPL-2.0-or-later.',1)
    return text,dict(upstream_pin='d82bc2549f27943d11dc59482d5388daf1c0f337',before_sha256=BEFORE,after_sha256=digest(text),functions=changes,
        correction='Consume instruction EA extensions only on indexed MOVEM CM restart. Reuse stacked calculated data EA; never recalculate index or read an operand pointer.',
        helper='src/modern040/runtime/restart.hpp:consume_movem_restart_ea',
        manual=dict(path='MC68040UM.pdf',sha256='aed5a038586dd2f610068efa01919fc552dc65bfd1a9274a333bad29dbd48894',page=238),
        wider_scope='No vendor pin, default or installer admission change. Targeted and full fault/bus qualification are separate requirements.')
def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--input',type=Path,default=ROOT/'src/modern040/restart.cpp');p.add_argument('--output',type=Path);p.add_argument('--metadata-output',type=Path);p.add_argument('--apply',action='store_true');a=p.parse_args()
    after,record=correct(a.input.read_text())
    if a.apply:(a.output or a.input).write_text(after)
    if a.apply and a.metadata_output:a.metadata_output.write_text(json.dumps(record,indent=2)+'\n')
    print(json.dumps(record,indent=2))
if __name__=='__main__':main()
