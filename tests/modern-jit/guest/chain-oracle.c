/* SPDX-License-Identifier: GPL-2.0-or-later
 * Independent wide-integer oracle; no emulator-source flag macros are used.
 * M68000PRM ADDX p117 / SUBX p287: X=C, sticky Z for extend operations.
 */
#include <mint/osbind.h>
extern long nf_id(const char *);
extern long nf_call(long,...);
__asm__(".text\n_nf_id:\n.word 0x7300\nrts\n_nf_call:\n.word 0x7301\nrts\n");
#define DECL(n) extern void alu_##n##_b(void),alu_##n##_w(void),alu_##n##_l(void)
DECL(addx_live);DECL(addx_dead);DECL(subx_live);DECL(subx_dead);DECL(negx_live);DECL(negx_dead);
extern unsigned char alu_probe_begin,alu_probe_end;
extern long alu_jit_query(void);
unsigned long alu_src,alu_dst,alu_before_sp,alu_registers[16];
unsigned short alu_ccr,alu_reply_ccr;
static unsigned long checks,compiled_checks,attempts,fallback_attempts;
static void hex(const char *label,unsigned long value) {
    char out[11];const char *digits="0123456789abcdef";unsigned i;
    for(i=0;i<8;++i)out[i]=digits[(value>>(28-4*i))&15];
    out[8]='\r';out[9]='\n';out[10]=0;
    nf_call(nf_id("NF_STDERR"),label);nf_call(nf_id("NF_STDERR"),out);
}
#include "chain-cases.h"
static long run(void) {
    void (*probe[6][3])(void)={
        {alu_addx_live_b,alu_addx_live_w,alu_addx_live_l},{alu_addx_dead_b,alu_addx_dead_w,alu_addx_dead_l},
        {alu_subx_live_b,alu_subx_live_w,alu_subx_live_l},{alu_subx_dead_b,alu_subx_dead_w,alu_subx_dead_l},
        {alu_negx_live_b,alu_negx_live_w,alu_negx_live_l},{alu_negx_dead_b,alu_negx_dead_w,alu_negx_dead_l}};
    const unsigned family[6]={0,0,1,1,6,6};
    unsigned pass,op=0,size=0,ccr=0,i,retry; unsigned long index;
    unsigned long expected=0,flags=0,before=0,after=0;long failure=0,jit;
    volatile unsigned long *registration=(volatile unsigned long *)0x00fef020UL;
    registration[0]=(unsigned long)&alu_probe_begin;
    registration[1]=(unsigned long)&alu_probe_end;
    jit=alu_jit_query()&1;
    for(pass=0;pass<2;++pass)for(index=0;index<sizeof(alu_cases)/sizeof(alu_cases[0]);++index) {
            const struct alu_case *c=&alu_cases[index];
            op=c->op;size=c->size;ccr=c->ccr;
            alu_src=c->src;alu_dst=c->dst;expected=c->result;flags=c->flags;
            ++checks;
#ifdef ALU_WRONG_Z
            if(pass==1)flags^=4;
#endif
            for(retry=0;;++retry) {
                alu_ccr=ccr;
                if(pass==1 && jit)before=((volatile unsigned long *)0x00fef040UL)[family[op]*3+size];
                probe[op][size]();++attempts;
                if(alu_registers[0]!=alu_src||alu_registers[1]!=expected||alu_reply_ccr!=flags) {failure=1;goto done;}
                for(i=2;i<8;++i)if(alu_registers[i]!=0x11110000UL+i){failure=2;goto done;}
                for(i=0;i<7;++i)if(alu_registers[i+8]!=0x22220000UL+i){failure=3;goto done;}
                if(alu_registers[15]!=alu_before_sp){failure=4;goto done;}
                if(pass==1 && jit) {
                    after=((volatile unsigned long *)0x00fef040UL)[family[op]*3+size];
                    if(after==before+4){++compiled_checks;break;}
                    /* A valid interpreter fallback is still checked, then the
                     * same case is retried until it actually translates. */
                    if(after!=before || retry>=63){failure=5;goto done;}
                    ++fallback_attempts;
                } else break;
            }
    }
done:
    if(failure) {
        hex("CHAIN op=",op);hex("CHAIN size=",size);hex("CHAIN CCR=",ccr);
        hex("CHAIN source=",alu_src);hex("CHAIN destination=",alu_dst);
        hex("CHAIN expected result=",expected);hex("CHAIN actual result=",alu_registers[1]);
        hex("CHAIN expected CCR=",flags);hex("CHAIN actual CCR=",alu_reply_ccr);hex("CHAIN count before=",before);hex("CHAIN count after=",after);
    }
    hex("CHAIN checks=",checks);hex("CHAIN compiled checks=",compiled_checks);hex("CHAIN attempts=",attempts);hex("CHAIN checked fallbacks=",fallback_attempts);
    nf_call(nf_id("NF_STDERR"),failure?"CHAIN ORACLE FAIL\r\n":"CHAIN ORACLE PASS 52224 cases four-operation ADDX/SUBX/NEGX chains live/dead-Z B/W/L allCCR D/A/A7\r\n");
    nf_call(nf_id("NF_EXIT"),failure);return failure;
}
int main(void){return (int)Supexec(run);}
