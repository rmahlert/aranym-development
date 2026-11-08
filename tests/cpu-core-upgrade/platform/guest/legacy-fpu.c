/* SPDX-License-Identifier: GPL-2.0-or-later
 * Ordinary owned guest test; soft-float CRT plus separately assembled FPU body.
 */
#include <mint/osbind.h>
extern long nf_id(const char *);
extern long nf_call(long,...);
extern long legacy_fpu_checks(void);
__asm__(".text\n_nf_id:\n.word 0x7300\nrts\n_nf_call:\n.word 0x7301\nrts\n");
static long run(void) {
    long result;unsigned long *jar=*(unsigned long **)0x5a0L;int present=0;
    while(jar&&jar[0]){if(jar[0]==0x5f465055UL)present=jar[1]!=0;jar+=2;}
    if(!present)result=1;else result=legacy_fpu_checks();
    nf_call(nf_id("NF_STDERR"),result?"LEGACY FPU GUEST FAIL\r\n":"LEGACY FPU GUEST PASS finite/rounding/endian/FSAVE/FTRAP-RTE\r\n");
    nf_call(nf_id("NF_EXIT"),result);
    return result;
}
int main(void){return (int)Supexec(run);}
