/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <mint/osbind.h>
extern long nf_id(const char *),nf_call(long,...);
extern unsigned long bench_addx(unsigned long),bench_add(unsigned long),bench_addq(unsigned long);
__asm__(".text\n_nf_id:\n.word 0x7300\nrts\n_nf_call:\n.word 0x7301\nrts\n");
#ifndef BENCH_ITERATIONS
#define BENCH_ITERATIONS 250000000UL
#endif
static long run(void) {
    unsigned round,offset,mode;long error=0;
    unsigned long (*const fn[3])(unsigned long)={bench_addx,bench_add,bench_addq};
    const char *const start[3]={"BENCH BEGIN ADDX\n","BENCH BEGIN ADD\n","BENCH BEGIN ADDQ\n"};
    const char *const end[3]={"BENCH END ADDX\n","BENCH END ADD\n","BENCH END ADDQ\n"};
    for(mode=0;mode<3;++mode)if(fn[mode](100000UL)!=400000UL)error=1;
    for(round=0;round<5 && !error;++round)for(offset=0;offset<3;++offset) {
        mode=(round+offset)%3;
        nf_call(nf_id("NF_STDERR"),start[mode]);
        if(fn[mode](BENCH_ITERATIONS)!=4*BENCH_ITERATIONS)error=2;
        nf_call(nf_id("NF_STDERR"),end[mode]);
    }
    nf_call(nf_id("NF_STDERR"),error?"BENCH CHECKSUM FAIL\n":"BENCH CHECKSUM PASS\n");
    nf_call(nf_id("NF_EXIT"),error);return error;
}
int main(void){return (int)Supexec(run);}
