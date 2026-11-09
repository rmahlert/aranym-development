/* SPDX-License-Identifier: GPL-2.0-or-later
 * Ordinary Atari guest; modify only its own writable instruction word.
 */
#include <mint/osbind.h>
extern long nf_id(const char *);
extern long nf_call(long,...);
extern long jit_value(void);
extern void jit_flush(void);
extern void jit_patch(unsigned long);
__asm__(".text\n_nf_id:\n.word 0x7300\nrts\n_nf_call:\n.word 0x7301\nrts\n");
#ifndef EXPECTED_PATCH
#define EXPECTED_PATCH 2
#endif
static long run(void) {
    int i;long result=0;
    for(i=0;i<512;++i)if(jit_value()!=1){result=1;break;}
    if(!result) {
        jit_patch(0x7002UL);
        jit_flush();
        for(i=0;i<512;++i)if(jit_value()!=EXPECTED_PATCH){result=2;break;}
    }
    if(!result) {
        jit_patch(0x7001UL);
        jit_flush();
        for(i=0;i<512;++i)if(jit_value()!=1){result=3;break;}
    }
    nf_call(nf_id("NF_STDERR"),result?"JIT INVALIDATION GUEST FAIL\r\n":"JIT INVALIDATION GUEST PASS warm/patch/CPUSHA/recompile/restore\r\n");
    nf_call(nf_id("NF_EXIT"),result);return result;
}
int main(void){return (int)Supexec(run);}
