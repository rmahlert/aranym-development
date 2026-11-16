/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <mint/osbind.h>
extern long nf_id(const char *);
extern long nf_call(long,...);
extern long arm64_fpu_checks(void);
__asm__(".text\n.globl _nf_id,_nf_call\n_nf_id:\n.word 0x7300\nrts\n_nf_call:\n.word 0x7301\nrts\n");
static long run(void) {
    long result=arm64_fpu_checks();
    nf_call(nf_id("NF_STDERR"),result?
        "ARM FPU FALLBACK FAIL mixed integer CCR and floating-point result\r\n":
        "ARM FPU FALLBACK PASS mixed integer CCR and floating-point result\r\n");
    nf_call(nf_id("NF_EXIT"),result);
    return result;
}
int main(void) { return (int)Supexec(run); }
