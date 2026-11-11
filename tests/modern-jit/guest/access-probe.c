/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <mint/osbind.h>
extern long nf_id(const char *),nf_call(long,...),access_probe(void);
extern unsigned char access_probe_next;
unsigned long probe_saved_sp,probe_old_vector,probe_count,probe_pc,probe_fault,probe_wb3a,probe_wb3d;
unsigned short probe_sr,probe_format,probe_ssw,probe_wb3s;
__asm__(".text\n_nf_id:\n.word 0x7300\nrts\n_nf_call:\n.word 0x7301\nrts\n");
static long run(void){
    long rc=access_probe();
    if(rc||probe_count!=1||probe_pc!=(unsigned long)&access_probe_next||
       !(probe_sr&0x2000)||probe_format!=0x7008||probe_ssw!=0x45||
       probe_wb3s!=0xc5||probe_fault!=0xfffffa42UL||probe_wb3a!=0xfffffa42UL||probe_wb3d!=3)rc=2;
    nf_call(nf_id("NF_STDERR"),rc?"ACCESS PROBE FAIL\n":"ACCESS PROBE PASS precise FFFFFA42 write/frame/discard/no-retry\n");
    nf_call(nf_id("NF_EXIT"),rc);return rc;
}
int main(void){return (int)Supexec(run);}
