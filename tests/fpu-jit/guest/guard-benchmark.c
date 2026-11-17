/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <mint/osbind.h>
extern long nf_id(const char *),nf_call(long,...),fpu_guard_benchmark(unsigned long);
unsigned long guard_iterations;
__asm__(".text\n_nf_id:\n.word 0x7300\nrts\n_nf_call:\n.word 0x7301\nrts\n");
static long run(void){long value;nf_call(nf_id("NF_STDERR"),"FPU GUARDBENCH BEGIN\r\n");value=fpu_guard_benchmark(10000000UL);
 nf_call(nf_id("NF_STDERR"),value==3&&guard_iterations==10000000UL?"FPU GUARDBENCH PASS result=3 iterations=10000000\r\n":"FPU GUARDBENCH FAIL\r\n");nf_call(nf_id("NF_EXIT"),value!=3||guard_iterations!=10000000UL);return 0;}
int main(void){return Supexec(run);}
