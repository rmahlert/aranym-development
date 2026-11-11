/* SPDX-License-Identifier: GPL-2.0-or-later
 * Independent numeric model: complete RAM bytes, all GPRs and all initial CCRs.
 */
#include <mint/osbind.h>
#include "ram-alias-probes.h"
extern long nf_id(const char *),nf_call(long,...),ea_jit_query(void);
extern unsigned char ram_alias_begin,ram_alias_end;
unsigned long ram_alias_input[16],ram_alias_output[16],ram_alias_sp;
unsigned short ram_alias_ccr,ram_alias_reply;
static volatile unsigned char data[256];
__asm__(".text\n_nf_id:\n.word 0x7300\nrts\n_nf_call:\n.word 0x7301\nrts\n_ea_jit_query:\nmoveq #-1,%d0\n.word 0x713b\nrts\n");
static void hex(const char *s,unsigned long x){char out[11];unsigned i;for(i=0;i<8;++i)out[i]="0123456789abcdef"[(x>>(28-4*i))&15];out[8]='\n';out[9]=0;nf_call(nf_id("NF_STDERR"),s);nf_call(nf_id("NF_STDERR"),out);}
static long run(void){
    volatile unsigned long *const device=(volatile unsigned long *)0x00fed000UL;
    unsigned pass,c,ccr,i,retry,compiled=0,checks=0;long error=0;
    unsigned long expected[16],value,sa,da,base=(unsigned long)&data[0],before=0,after;
    unsigned char bytes[256];unsigned flags;int native;
    device[8]=(unsigned long)&ram_alias_begin;device[9]=(unsigned long)&ram_alias_end;
    native=(ea_jit_query()&1)&&device[11];
    for(pass=0;pass<2;++pass)for(c=0;c<sizeof(cases)/sizeof(cases[0]);++c)for(ccr=0;ccr<32;++ccr){
        const struct RamCase *p=&cases[c];
        ++checks;
        for(i=0;i<8;++i)expected[i]=0x11110000UL+i;
        for(i=0;i<7;++i)expected[8+i]=0x22220000UL+i;
        expected[10]=base+128;expected[11]=base+192;
        for(i=0;i<15;++i)ram_alias_input[i]=expected[i];
        for(i=0;i<256;++i)bytes[i]=(unsigned char)(i^0xa5);
        sa=expected[10];
        if(p->src==4){sa-=p->size;expected[10]=sa;}
        if(p->src==5)sa+=3;
        value=0;for(i=0;i<p->size;++i)value=(value<<8)|bytes[sa-base+i];
        if(p->src==3)expected[10]+=p->size;
        flags=ccr;
        if(p->kind==0){
            da=expected[p->alias?10:11];
            if(p->dst==4){da-=p->size;expected[p->alias?10:11]=da;}
            if(p->dst==5)da-=5;
            if(p->dst==3)expected[p->alias?10:11]+=p->size;
            for(i=0;i<p->size;++i)bytes[da-base+i]=(unsigned char)(value>>(8*(p->size-1-i)));
        }else if(p->kind==1){
            unsigned long mask=p->size==1?255UL:p->size==2?65535UL:0xffffffffUL;
            expected[0]=(expected[0]&~mask)|value;
        }else expected[p->alias?10:11]=p->size==2&&(value&0x8000)?value|0xffff0000UL:value;
        if(p->kind!=2)flags=(ccr&16)|(value?0:4)|((value&(p->size==1?128UL:p->size==2?32768UL:0x80000000UL))?8:0);
        for(retry=0;;++retry){
            for(i=0;i<256;++i)data[i]=(unsigned char)(i^0xa5);
            ram_alias_ccr=ccr;if(pass&&native)before=device[16+p->index];
            p->fn();expected[15]=ram_alias_sp;
            for(i=0;i<16;++i)if(ram_alias_output[i]!=expected[i]){error=1;goto done;}
            for(i=0;i<256;++i)if(data[i]!=bytes[i]){error=2;goto done;}
            if(ram_alias_reply!=flags){error=3;goto done;}
            if(!pass||!native)break;
            after=device[16+p->index];if(after==before+1){++compiled;break;}
            if(after!=before||retry>=63){error=4;goto done;}
        }
    }
done:
    hex("RAM ALIAS checks=",checks);hex("RAM ALIAS native=",compiled);
    if(error){hex("RAM ALIAS case=",c);hex("RAM ALIAS CCR=",ccr);hex("RAM ALIAS index=",i);hex("RAM ALIAS error=",error);}
    nf_call(nf_id("NF_STDERR"),error?"RAM ALIAS FAIL\n":"RAM ALIAS PASS allGPR/allCCR/allRAM/update/two-EA-alias\n");
    nf_call(nf_id("NF_EXIT"),error);return error;
}
int main(void){return (int)Supexec(run);}
