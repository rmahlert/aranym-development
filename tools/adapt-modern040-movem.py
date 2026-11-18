#!/usr/bin/env python3
"""Reproduce the minimal 040 predecrement base-in-mask compiler correction.
SPDX-License-Identifier: GPL-2.0-or-later

Accepts only the exact original normalized compiler; no vendor pin changes.
FF and NF MOVEM bodies are verified identical, so both receive the same fix.
"""
import argparse
import hashlib
import json
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
BEFORE='d865a4ff366887a8e20ff13448b557480d18d54374143bf81da5e9a25f4a24ce'
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--apply',action='store_true')
p.add_argument('--input',type=Path,default=ROOT/'src/uae_cpu/compiler/imported/upstream-compemu.cpp')
p.add_argument('--output',type=Path)
p.add_argument('--metadata-output',type=Path)
a=p.parse_args();path=a.input;text=path.read_text()
def digest(s):return hashlib.sha256(s.encode()).hexdigest()
assert digest(text)==BEFORE,'unrecognized compiler preimage; never patch a different source silently'
def extract(source,name):
    start=source.index('void REGPARAM2 '+name+'(');end=source.index('\n}\n',start)+3
    return start,end,source[start:end]
def body(function):return function[function.index('\n{'):]
entries=[];original=text
for stem,size,write in [('op_48a0_0_comp_',2,'writeword'),('op_48e0_0_comp_',4,'writelong')]:
    before_bodies=[body(extract(text,stem+mode)[2])for mode in ['ff','nf']]
    assert before_bodies[0]==before_bodies[1],stem+' FF/NF bodies differ; audit separately'
    for mode in ['ff','nf']:
        name=stem+mode;start,end,function=extract(text,name)
        old='\t\t\t} else {\n\t\t\t\tfor (i=0;i<16 && mask;i++) {'
        new=('\t\t\t} else {\n'
             '\t\t\t\t// 020/030/040: stored base is INITIAL minus one size,\n'
             '\t\t\t\t// not the cumulatively decremented effective address.\n'
             '\t\t\t\tconst bool base_in_mask = (mask & (1u << (7 - dstreg))) != 0;\n'
             '\t\t\t\tif (base_in_mask) {\n'
             '\t\t\t\t\tmov_l_rr(native, 8 + dstreg);\n'
             f'\t\t\t\t\tsub_l_ri(native, {size});\n'
             '\t\t\t\t}\n'
             '\t\t\t\tfor (i=0;i<16 && mask;i++) {')
        store=f'\t\t\t\t\t\t{write}(srca,15-i,scratchie);'
        replacement=('\t\t\t\t\t\tconst int value = base_in_mask && (15 - i) == int(8 + dstreg) ? native : 15 - i;\n'
                     f'\t\t\t\t\t\t{write}(srca,value,scratchie);')
        assert function.count(old)==function.count(store)==1,name
        fixed=function.replace(old,new).replace(store,replacement)
        entries.append(dict(function=name,operand_size=size,before_body_sha256=digest(body(function)),after_body_sha256=digest(body(fixed))))
        text=text[:start]+fixed+text[end:]
    assert body(extract(text,stem+'ff')[2])==body(extract(text,stem+'nf')[2])
report=dict(
    upstream_pin='d82bc2549f27943d11dc59482d5388daf1c0f337',
    compiler_path='src/uae_cpu/compiler/imported/upstream-compemu.cpp',before_sha256=BEFORE,after_sha256=digest(text),functions=entries,
    ff_nf_bodies_identical_before_and_after=True,
    contract='For 040 predecrement MOVEM, preserve the initial base minus one operand size in an existing independent scratch before any address decrement. Select it only for the stored base register; final base still uses the complete decremented EA.',
    manual=dict(source_id='30243512848e66e8edd5603f',sha256='06e4864b78da0e815054cead9326b7ec9914661f240fd39a455f2061ff47c4e8',pdf_page=232),
    observed_failure='Independent MOVE-based oracle: Word mask A2/A3 at CCR0 stores low byte54 instead of56, actual native/NF counters0->1; interpreter and wrong-model controls validate the expected value. Same actual failure on Linux and Windows PE/Wine.',
    preserved_before=dict(linux_binary_sha256='a37a651e1dd10d0fdb01dc7b1d83692a9b35f7c9ec5a9289ca3382798e1dcb81',windows_binary_sha256='dcc0d0fd7e1da48fe1e3756eda2e349629465e4a4bb9375a3c089256a9c7188d'),
    scope='Four predecrement W/L FF/NF functions only; no pin update, other compiler bodies or architectural fallbacks changed. Post-fix runtime qualification is a separate gate.')
if a.apply:
    (a.output or path).write_text(text)
    if a.metadata_output:a.metadata_output.write_text(json.dumps(report,indent=2)+'\n')
print(json.dumps(dict(applied=a.apply,before_sha256=BEFORE,after_sha256=digest(text),functions=[e['function']for e in entries]),indent=2))
