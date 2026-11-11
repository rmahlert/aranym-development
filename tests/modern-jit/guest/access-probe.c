/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <mint/osbind.h>
extern long nf_id(const char *),nf_call(long,...),access_probe(void),access_guard_probe(void),ea_jit_query(void);
extern unsigned char access_probe_next,access_guard_next,guard_write_begin,guard_write_end;
unsigned long probe_saved_sp,probe_old_vector,probe_count,probe_pc,probe_fault,probe_wb3a,probe_wb3d;
unsigned long probe_native_writes;
unsigned short probe_sr,probe_format,probe_ssw,probe_wb3s;
__asm__(".text\n_nf_id:\n.word 0x7300\nrts\n_nf_call:\n.word 0x7301\nrts\n");
__asm__(".text\n_ea_jit_query:\nmoveq #-1,%d0\n.word 0x713b\nrts\n");
static int valid(unsigned long pc){return probe_count==1&&probe_pc==pc&&(probe_sr&0x2000)&&probe_format==0x7008&&probe_ssw==0x45&&probe_wb3s==0xc5&&probe_fault==0xfffffa42UL&&probe_wb3a==0xfffffa42UL&&probe_wb3d==3;}
static long run(void){
    volatile unsigned long *const device=(volatile unsigned long *)0x00fed000UL;
    long rc=access_probe();
    if(rc||!valid((unsigned long)&access_probe_next))rc=2;
    if(!rc){
        device[8]=(unsigned long)&guard_write_begin;device[9]=(unsigned long)&guard_write_end;
        probe_count=0;rc=access_guard_probe();
        if(rc||!valid((unsigned long)&access_guard_next))rc=3;
        if((ea_jit_query()&1)&&device[11]&&!probe_native_writes)rc=4;
    }
    nf_call(nf_id("NF_STDERR"),rc?"ACCESS PROBE FAIL\n":"ACCESS PROBE PASS precise FFFFFA42 write/frame/discard/no-retry\n");
    nf_call(nf_id("NF_EXIT"),rc);return rc;
}
int main(void){return (int)Supexec(run);}
