/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <mint/osbind.h>
extern long fpu_trap_check(void),nf_id(const char *),nf_call(long,...);
extern void fpu_trap_handler(void);
__asm__(".text\n_nf_id:\n.word 0x7300\nrts\n_nf_call:\n.word 0x7301\nrts\n");
static long run(void){
 (void)Setexc(50,fpu_trap_handler);
 (void)fpu_trap_check();
 nf_call(nf_id("NF_STDERR"),"FPU TRAP CAPABILITY UNEXPECTEDLY RETURNED\r\n");
 nf_call(nf_id("NF_EXIT"),9);return 9;
}
int main(void){return (int)Supexec(run);}
