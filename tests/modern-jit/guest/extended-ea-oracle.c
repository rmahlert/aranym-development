/* SPDX-License-Identifier: GPL-2.0-or-later
 * Independent byte/RAM/register/CCR model using literal expected offsets.
 */
#include <mint/osbind.h>
extern long nf_id(const char *),nf_call(long,...),extended_jit_query(void);
__asm__(".text\n_nf_id:\n.word 0x7300\nrts\n_nf_call:\n.word 0x7301\nrts\n_extended_jit_query:\nmoveq #-1,%d0\n.word 0x713b\nrts\n");
volatile unsigned char ea_data[256];volatile unsigned long ea_pointers[16];
unsigned long ea_input[16],ea_output[16],ea_saved_sp,ea_fake_sp;
unsigned short ea_ccr,ea_reply,ea_saved_sr;
extern unsigned char ea_extended_begin,ea_extended_end;
#include "extended-cases.h"
static unsigned long checks,native_checks,device_checks,warmed_checks;
static void hex(const char *s,unsigned long x){char b[11];unsigned i;for(i=0;i<8;++i)b[i]="0123456789abcdef"[(x>>(28-4*i))&15];b[8]='\r';b[9]='\n';b[10]=0;nf_call(nf_id("NF_STDERR"),s);nf_call(nf_id("NF_STDERR"),b);}
static void init(unsigned pattern,const struct ExtendedCase *p){unsigned i;
    for(i=0;i<16;++i)ea_input[i]=i<8?0x11110000UL+i:0x22220000UL+i;
    ea_input[0]=0x89abcdefUL;ea_input[1]=4;ea_input[2]=0xfffffff9UL;ea_input[6]=1;ea_input[13]=3;
    ea_input[10]=(unsigned long)(p->source_pointer?(void*)&ea_pointers[4]:(void*)&ea_data[64]);
    ea_input[11]=(unsigned long)(p->dest_pointer?(void*)&ea_pointers[12]:(void*)&ea_data[160]);
    ea_fake_sp=(unsigned long)&ea_data[64];
    if(p->custom==7)ea_input[13]=0x80000000UL;
    if(p->custom==8)ea_input[13]=0x60000000UL;
    if(p->custom==9)ea_input[1]=0xfffffff9UL;
    for(i=0;i<256;++i)ea_data[i]=(unsigned char)(pattern==0?0:pattern==1?0x80:pattern==2?0xff:(i^0xa5));
    for(i=0;i<16;++i)ea_pointers[i]=(unsigned long)&ea_data[(i*8)&255];
    ea_pointers[4]=(unsigned long)&ea_data[96];ea_pointers[12]=(unsigned long)&ea_data[192];
}
static long run(void){
    volatile unsigned long *const witness=(volatile unsigned long*)0x00fec000UL,*const device=(volatile unsigned long*)0x00fed000UL;
    unsigned pass,c=0,ccr=0,i=0,retry,warm,pattern;long error=0,jit=extended_jit_query()&1;
    unsigned long expected[16],pointer_expected[16],before=0,after=0,value=0,mask,flags=0,address,read_count,write_count,configured;
    unsigned char bytes[256];const struct ExtendedCase *p=0;
    witness[0]=(unsigned long)&ea_extended_begin;witness[1]=(unsigned long)&ea_extended_end;
    for(pass=0;pass<2;++pass)for(c=0;c<sizeof(extended_cases)/sizeof(extended_cases[0]);++c)for(ccr=0;ccr<32;++ccr){
        p=&extended_cases[c];++checks;pattern=(ccr+pass)&3;ea_ccr=ccr;
        if(pass&&jit&&p->device){
            init(pattern,p);witness[2]=(unsigned long)p->pc;before=witness[2];
            for(warm=0;warm<64;++warm){init(pattern,p);p->fn();}
            after=witness[2];if(after==before){error=7;goto done;}++warmed_checks;
        }
        for(retry=0;retry<64;++retry){
            init(pattern,p);mask=p->size==1?255UL:p->size==2?65535UL:0xffffffffUL;
            configured=p->device==2?(unsigned long)&ea_data[96]:0x89abcdefUL;
            device[1]=configured;
            if(p->device==1||p->device==2||p->device==6)ea_input[10]=0x00fed000UL;
            if(p->device==3)ea_pointers[4]=0x00fed000UL;
            if(p->device==4||p->device==6)ea_input[11]=0x00fed000UL;
            if(p->device==5)ea_pointers[12]=0x00fed000UL;
            for(i=0;i<16;++i){expected[i]=ea_input[i];pointer_expected[i]=ea_pointers[i];}
            for(i=0;i<256;++i)bytes[i]=ea_data[i];
            address=p->source_space==1?p->source_offset:p->source_space==2?(unsigned long)p->pc_data+p->source_offset:p->source_space==3?(unsigned long)ea_pointers+p->source_offset:(unsigned long)&ea_data[p->source_offset];
            value=0;
            if(p->kind==3||p->kind==4)value=0x89abcdefUL&mask;
            else if(p->kind==5)value=address;
            else if(p->device==1||p->device==3||p->device==6)value=configured&mask;
            else for(i=0;i<p->size;++i)value=(value<<8)|((volatile unsigned char*)address)[i];
            if(p->source_update)expected[10]+=p->source_update;
            if(p->dest_update)expected[11]+=p->dest_update;
            if(p->kind==1)expected[0]=(expected[0]&~mask)|value;
            else if(p->kind==2)expected[12]=p->size==2&&(value&0x8000)?value|0xffff0000UL:value;
            else if(p->kind==5)expected[12]=value;
            else if(!p->device)for(i=0;i<p->size;++i)bytes[p->dest_offset+i]=(unsigned char)(value>>(8*(p->size-1-i)));
            flags=p->kind==2||p->kind==5?ccr:(ccr&16)|(value?0:4)|((value&(p->size==1?128UL:p->size==2?32768UL:0x80000000UL))?8:0);
            ea_ccr=ccr;witness[2]=(unsigned long)p->pc;before=witness[2];p->fn();after=witness[2];expected[15]=ea_saved_sp;
            if(p->custom==1)expected[15]=(unsigned long)&ea_data[66];
            if(p->custom==2)expected[15]=(unsigned long)&ea_data[60];
#ifdef EXTENDED_WRONG_RESULT
            if(pass)expected[5]^=1;
#endif
            for(i=0;i<16;++i)if(ea_output[i]!=expected[i]){error=1;goto done;}
            for(i=0;i<256;++i)if(ea_data[i]!=bytes[i]){error=2;goto done;}
            for(i=0;i<16;++i)if(ea_pointers[i]!=pointer_expected[i]){error=3;goto done;}
            if(ea_reply!=flags){error=4;goto done;}
            read_count=p->device&&p->device<=3?1:p->device==6?1:0;write_count=p->device>=4?1:0;
            if(device[1]!=read_count||device[2]!=write_count||(write_count&&device[3]!=(value&mask))){error=6;goto done;}
            if(!pass||!jit)break;
            if(p->device){if(after!=before){error=5;goto done;}++device_checks;break;}
            if(after==before+1){++native_checks;break;}
            if(after!=before){error=5;goto done;}
        }
        if(retry==64){error=5;goto done;}
    }
done:
#ifdef EXTENDED_WRONG_COUNT
    if(!error)++checks;
#endif
    hex("EXTENDED checks=",checks);hex("EXTENDED native=",native_checks);hex("EXTENDED device-deopt=",device_checks);hex("EXTENDED warmed-device=",warmed_checks);
    if(error){hex("EXTENDED case=",c);hex("EXTENDED kind=",p?p->kind:0);hex("EXTENDED device=",p?p->device:0);hex("EXTENDED CCR=",ccr);hex("EXTENDED index=",i);hex("EXTENDED error=",error);hex("EXTENDED expected CCR=",flags);hex("EXTENDED actual CCR=",ea_reply);hex("EXTENDED expected register=",i<16?expected[i]:0);hex("EXTENDED actual register=",i<16?ea_output[i]:0);hex("EXTENDED before=",before);hex("EXTENDED after=",after);}
    nf_call(nf_id("NF_STDERR"),error?"EXTENDED EA ORACLE FAIL\r\n":"EXTENDED EA ORACLE PASS allCCR allGPR allRAM absolute/PC/index/indirect/LEA device-once\r\n");
    nf_call(nf_id("NF_EXIT"),error);return error;
}
int main(void){return (int)Supexec(run);}
