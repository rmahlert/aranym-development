/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <mint/osbind.h>
extern long nf_id(const char *);
extern long nf_call(long,...);
extern void emulop_checks(void);
extern unsigned long emulop_regs[16],emulop_after_regs[16],emulop_frame_pc,emulop_expected_pc;
extern unsigned long emulop_frame_sp,emulop_caller_sp,emulop_user_top,emulop_after_usp,emulop_exit_id;
extern unsigned short emulop_ccr,emulop_reply_ccr,emulop_frame_sr,emulop_frame_format;
__asm__(".text\n.globl _nf_id,_nf_call\n_nf_id:\n.word 0x7300\nrts\n_nf_call:\n.word 0x7301\nrts\n");
#ifndef EMULOP_EXPECT_JIT
#define EMULOP_EXPECT_JIT 1
#endif
#ifndef EMULOP_EXPECT_PC_DELTA
#define EMULOP_EXPECT_PC_DELTA 0
#endif
static void reporthex(const char *label,unsigned long value) {
    char text[11];int i;const char *hex="0123456789abcdef";
    for(i=0;i<8;++i)text[i]=hex[(value>>(28-4*i))&15];
    text[8]='\r';text[9]='\n';text[10]=0;
    nf_call(nf_id("NF_STDERR"),label);nf_call(nf_id("NF_STDERR"),text);
}
static long run(void) {
    int ccr,i;long result=0;emulop_exit_id=nf_id("NF_EXIT");
    for(ccr=0;ccr<32&&!result;++ccr){
        emulop_ccr=ccr;emulop_checks();
        if((emulop_regs[0]&1)!=EMULOP_EXPECT_JIT||(emulop_regs[0]&~9UL)||emulop_reply_ccr!=ccr)result=1;
        for(i=1;i<15;++i)if(emulop_regs[i]!=(0x11110000UL+i)||emulop_after_regs[i]!=emulop_regs[i])result=2;
        if(emulop_after_regs[0]!=emulop_regs[0])result=2;
        if(emulop_frame_pc!=emulop_expected_pc+EMULOP_EXPECT_PC_DELTA||emulop_frame_sr!=(0x0700|ccr)||emulop_frame_format!=0x0020)result=3;
        if(emulop_regs[15]!=emulop_user_top||emulop_after_usp!=emulop_user_top||emulop_after_regs[15]!=emulop_caller_sp||emulop_frame_sp!=emulop_caller_sp-8)result=4;
    }
    if(result){
        reporthex("EMULOP failed CCR=",ccr-1);reporthex("EMULOP query A7=",emulop_regs[15]);
        reporthex("EMULOP expected user A7=",emulop_user_top);reporthex("EMULOP returned USP=",emulop_after_usp);
        reporthex("EMULOP returned ISP=",emulop_after_regs[15]);reporthex("EMULOP caller ISP=",emulop_caller_sp);
        reporthex("EMULOP frame SP=",emulop_frame_sp);reporthex("EMULOP frame SR=",emulop_frame_sr);
        reporthex("EMULOP frame format=",emulop_frame_format);reporthex("EMULOP frame PC=",emulop_frame_pc);
        reporthex("EMULOP expected PC=",emulop_expected_pc);
    }
    nf_call(nf_id("NF_STDERR"),result?"EMULOP JIT GUEST FAIL\r\n":"EMULOP JIT GUEST PASS query/allCCR/registers/direct-privilege/format0/RTE\r\n");
    nf_call(emulop_exit_id,result);return result;
}
int main(void){return (int)Supexec(run);}
