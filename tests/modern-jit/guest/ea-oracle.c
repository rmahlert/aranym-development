/* SPDX-License-Identifier: GPL-2.0-or-later
 * Numeric register/CCR/EA oracle independent of emulator helper code.
 * The bus fixture supplies a fixed value and counts actual transfers.
 */
#include <mint/osbind.h>
extern long nf_id(const char *),nf_call(long,...),ea_jit_query(void);
extern unsigned char ea_probe_begin,ea_probe_end;
unsigned long ea_pointer,ea_input,ea_before_sp,ea_registers[16];
unsigned short ea_ccr,ea_reply_ccr;
__asm__(".text\n_nf_id:\n.word 0x7300\nrts\n_nf_call:\n.word 0x7301\nrts\n");
#include "ea-probes.h"
static volatile unsigned long *const device=(volatile unsigned long *)0x00fed000UL;
#ifdef EA_RAM_TARGET
static volatile unsigned char ram_target[8];
#endif
static unsigned long checks,compiled,fallbacks;
static void hex(const char *label,unsigned long n) {
    char out[11];unsigned i;const char *digits="0123456789abcdef";
    for(i=0;i<8;++i)out[i]=digits[(n>>(28-4*i))&15];
    out[8]='\r';out[9]='\n';out[10]=0;nf_call(nf_id("NF_STDERR"),label);nf_call(nf_id("NF_STDERR"),out);
}
static long run(void) {
    const long disps[5]={-32768,-1,0,1,32767};
    const unsigned long values[8]={0,1,0x7f,0x80,0x7fff,0x8000,0x80000000UL,0xffffffffUL};
    const unsigned sizes[10]={1,2,4,1,2,4,2,4,2,4};
    unsigned pass,g=0,op=0,v=0,ccr=0,retry,i,index;long error=0;
    unsigned long expected[16],mask,value,result,flags,before=0,after=0;
    int native;
    device[8]=(unsigned long)&ea_probe_begin;device[9]=(unsigned long)&ea_probe_end;
    native=(ea_jit_query()&1)&&device[10];
#ifdef EA_RAM_TARGET
    native=(ea_jit_query()&1)&&device[11];
#endif
    for(pass=0;pass<2;++pass)for(g=0;g<5;++g)for(op=0;op<10;++op)for(v=0;v<8;++v)for(ccr=0;ccr<32;++ccr) {
        ++checks;mask=sizes[op]==1?255UL:sizes[op]==2?65535UL:0xffffffffUL;
        value=values[v]&mask;
        ea_pointer=0x00fed000UL-(unsigned long)disps[g];
#ifdef EA_RAM_TARGET
        ea_pointer=(unsigned long)&ram_target[1]-(unsigned long)disps[g];
#endif
        ea_input=op>=3&&op<6?values[v]:0xa5a55a5aUL;
        for(i=0;i<8;++i)expected[i]=0x11110000UL+i;
        for(i=0;i<7;++i)expected[8+i]=0x22220000UL+i;
        expected[7]=ea_input;expected[14]=ea_pointer;
        flags=ccr;
        if(op<6) {
            flags=(ccr&16)|(value?0:4)|((value&(sizes[op]==1?128UL:sizes[op]==2?32768UL:0x80000000UL))?8:0);
            if(op<3)expected[7]=(ea_input&~mask)|value;
        } else {
            /* MOVEA.W sign-extends and MOVEA never changes any CCR bit. */
            result=sizes[op]==2 && (value&0x8000)?value|0xffff0000UL:value;
            expected[op<8?9:14]=result;
        }
#ifdef EA_WRONG_SIGN
        if(pass==1 && op==6 && v==5)expected[9]^=0xffff0000UL;
#endif
        index=op<8?op:op-2;
        for(retry=0;;++retry) {
            device[1]=values[v];ea_ccr=ccr;
#ifdef EA_RAM_TARGET
            for(i=0;i<8;++i)ram_target[i]=0x5a;
            for(i=0;i<sizes[op];++i)ram_target[1+i]=(unsigned char)(value>>(8*(sizes[op]-1-i)));
#endif
            if(pass==1 && native)before=device[16+index];
            probes[g][op]();
            expected[15]=ea_before_sp;
            for(i=0;i<16;++i)if(ea_registers[i]!=expected[i]){error=1;goto done;}
            if(ea_reply_ccr!=flags){error=2;goto done;}
#ifdef EA_RAM_TARGET
            for(i=0;i<sizes[op];++i)if(ram_target[1+i]!=(unsigned char)(value>>(8*(sizes[op]-1-i)))){error=4;goto done;}
            if(ram_target[0]!=0x5a||ram_target[1+sizes[op]]!=0x5a||device[1]||device[2]||device[4]){error=3;goto done;}
#else
            if(device[1]!=(op>=3&&op<6?0:1)||device[2]!=(op>=3&&op<6?1:0)||device[4]){error=3;goto done;}
            if(op>=3&&op<6 && device[3]!=value){error=4;goto done;}
#endif
#ifdef EA_WRONG_COUNT
            if(pass==1 && device[1]!=2){error=5;goto done;}
#endif
            if(pass==1 && native) {
                after=device[16+index];
                if(after==before+1){++compiled;break;}
                if(after!=before || retry>=63){error=6;goto done;}
                ++fallbacks;
            } else break;
        }
    }
#ifdef EA_FAULT
    device[1]=0;ea_pointer=0x00fed014UL+32768UL;ea_ccr=0;probes[0][2]();error=7;
#endif
done:
    if(error){hex("EA group=",g);hex("EA op=",op);hex("EA value=",v);hex("EA CCR=",ccr);hex("EA register index=",i);hex("EA expected=",i<16?expected[i]:0);hex("EA actual=",i<16?ea_registers[i]:0);hex("EA flags=",ea_reply_ccr);hex("EA count before=",before);hex("EA count after=",after);}
    hex("EA checks=",checks);hex("EA compiled checks=",compiled);hex("EA checked fallbacks=",fallbacks);
    for(i=0;i<8;++i)hex("EA native counter=",device[16+i]);
    nf_call(nf_id("NF_STDERR"),error?"EA ORACLE FAIL\r\n":"EA ORACLE PASS displacement/MOVEA/alias/allCCR/state/read-once/write-once\r\n");
    nf_call(nf_id("NF_EXIT"),error);return error;
}
int main(void){return (int)Supexec(run);}
