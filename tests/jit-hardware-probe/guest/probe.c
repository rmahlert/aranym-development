/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <mint/osbind.h>
#include <stdio.h>
extern long nf_id(const char *),nf_call(long,...),probe(void);
extern char probe_access;
unsigned long saved_sp,old_vector,fault_pc,fault_d1;unsigned short fault_format;
__asm__(".text\n_nf_id:\n.word 0x7300\nrts\n_nf_call:\n.word 0x7301\nrts\n");
static long run(void){long r=probe();char b[200];int bad=r||fault_pc!=(unsigned long)&probe_access||fault_d1!=0x13579bdfUL||fault_format!=0x7008;sprintf(b,"HARDWARE PROBE r=%ld pc=%08lx expected=%08lx d1=%08lx format=%04x %s\r\n",r,fault_pc,(unsigned long)&probe_access,fault_d1,fault_format,bad?"FAIL":"PASS");nf_call(nf_id("NF_STDERR"),b);nf_call(nf_id("NF_EXIT"),(long)bad);return bad;}
int main(void){return Supexec(run);}
