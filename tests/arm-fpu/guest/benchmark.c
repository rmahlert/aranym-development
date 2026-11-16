/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <mint/osbind.h>
extern long nf_id(const char *),nf_call(long,...),fpu_benchmark(long);
__asm__(".text\n_nf_id:\n.word 0x7300\nrts\n_nf_call:\n.word 0x7301\nrts\n");
static void hex(char *s,unsigned long value){static const char d[]="0123456789abcdef";int i;for(i=7;i>=0;--i){s[i]=d[value&15];value>>=4;}}
static long run(void){
 volatile unsigned long *clock=(void *)0x4ba;
 unsigned long before,elapsed;long result;char report[]="FPU-BENCH ticks=00000000 result=00000000\r\n";
 nf_call(nf_id("NF_STDERR"),"FPU-BENCH BEGIN\r\n");
 before=*clock;result=fpu_benchmark(1000000);elapsed=*clock-before;
 hex(report+16,elapsed);hex(report+32,(unsigned long)result);
 nf_call(nf_id("NF_STDERR"),report);
 nf_call(nf_id("NF_EXIT"),result==3?0:1);return 0;
}
int main(void){return (int)Supexec(run);}
