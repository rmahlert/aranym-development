/* SPDX-License-Identifier: GPL-2.0-or-later
 * Immutable host-generated product expectations and full register/CCR checks.
 */
#include <mint/osbind.h>
extern long nf_id(const char *),nf_call(long,...),scalar_jit_query(void);
__asm__(".text\n_nf_id:\n.word 0x7300\nrts\n_nf_call:\n.word 0x7301\nrts\n");
unsigned long scalar_input[16],scalar_output[16],scalar_saved_sp;
unsigned short scalar_ccr,scalar_reply,scalar_saved_sr;
extern unsigned char scalar_begin,scalar_end;
#include "scalar-cases.h"
static unsigned long checks,native_checks,live_mull_fallbacks,attempts;
static void hex(const char *label,unsigned long value){
    char b[11];unsigned i;const char *d="0123456789abcdef";
    for(i=0;i<8;++i)b[i]=d[(value>>(28-4*i))&15];
    b[8]='\r';b[9]='\n';b[10]=0;nf_call(nf_id("NF_STDERR"),label);nf_call(nf_id("NF_STDERR"),b);
}
static long run(void){
    volatile unsigned long *fixture=(volatile unsigned long *)0x00fec000UL;
    unsigned pass,ccr,i,index=0,retry,reg=0;long fail=0,jit=scalar_jit_query()&1;
    unsigned long before=0,after=0,expected[16],flags=0,value;
    const struct ScalarCase *c=0;const struct ScalarForm *f=0;
    fixture[0]=(unsigned long)&scalar_begin;fixture[1]=(unsigned long)&scalar_end;
    for(pass=0;pass<2;++pass)for(index=0;index<sizeof(scalar_cases)/sizeof(scalar_cases[0]);++index)for(ccr=0;ccr<32;++ccr){
        c=&scalar_cases[index];f=&forms[c->form];++checks;
        for(retry=0;retry<64;++retry){
            for(i=0;i<16;++i)scalar_input[i]=i<8?0x11110000UL+i:0x22220000UL+i;
            scalar_input[0]=c->a;scalar_input[1]=c->b;scalar_input[2]=c->a;scalar_ccr=ccr;
            fixture[2]=(unsigned long)f->pc;before=fixture[2];
            f->fn();++attempts;after=fixture[2];
            for(i=0;i<16;++i)expected[i]=scalar_input[i];expected[15]=scalar_saved_sp;
            if(f->kind==4){value=expected[f->left];expected[f->left]=expected[f->right];expected[f->right]=value;flags=ccr;}
            else {expected[1]=c->low;if(f->wide)expected[2]=c->high;flags=(ccr&16)|c->flags;if(f->dead)expected[7]=0;}
#ifdef SCALAR_WRONG_RESULT
            if(pass==1)expected[4]^=1;
#endif
            for(reg=0;reg<16;++reg)if(scalar_output[reg]!=expected[reg]){fail=1;goto done;}
            if(scalar_reply!=flags){fail=2;goto done;}
            if(pass==1&&jit){
                if(f->kind>=2&&f->kind<4&&!f->dead){
                    /* Upstream has no flags-producing MULL compiler entry. */
                    if(after!=before){fail=6;goto done;}++live_mull_fallbacks;break;
                }
                if(after==before+1){++native_checks;break;}
                if(after!=before){fail=5;goto done;}
            }else break;
        }
        if(retry==64){fail=5;goto done;}
    }
done:
    hex("SCALAR checks=",checks);hex("SCALAR native checks=",native_checks);hex("SCALAR live-MULL fallbacks=",live_mull_fallbacks);hex("SCALAR attempts=",attempts);
    if(fail){hex("SCALAR case=",index);hex("SCALAR form=",c?c->form:0);hex("SCALAR input CCR=",ccr);hex("SCALAR register=",reg);hex("SCALAR expected=",reg<16?expected[reg]:flags);hex("SCALAR actual=",reg<16?scalar_output[reg]:scalar_reply);hex("SCALAR expected CCR=",flags);hex("SCALAR actual CCR=",scalar_reply);hex("SCALAR native before=",before);hex("SCALAR native after=",after);}
    nf_call(nf_id("NF_STDERR"),fail?"SCALAR ORACLE FAIL\r\n":"SCALAR ORACLE PASS MULU/MULS/MULL EXG allCCR aliases A7 flags-live/dead\r\n");
    nf_call(nf_id("NF_EXIT"),fail);return fail;
}
int main(void){return (int)Supexec(run);}
