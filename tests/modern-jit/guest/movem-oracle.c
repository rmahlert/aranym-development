/* SPDX-License-Identifier: GPL-2.0-or-later
 * Independent literal-EA table plus PRM-derived register-list semantics.
 * No instruction decoder or emulator EA routine is reused by the oracle.
 */
#include <mint/osbind.h>
#include <string.h>
extern long nf_id(const char*),nf_call(long,...),movemo_jit_query(void);
extern unsigned char movemo_begin,movemo_end;
unsigned long movemo_input[16],movemo_output[16],movemo_real_sp,movemo_target_pointer,movemo_pointer;
unsigned short movemo_ccr,movemo_reply,movemo_saved_sr;
unsigned char movemo_data[512];
__asm__(".text\n_nf_id:\n.word 0x7300\nrts\n_nf_call:\n.word 0x7301\nrts\n_movemo_jit_query:\nmoveq #-1,%d0\n.word 0x713b\nrts\n");
static void hex(const char*s,unsigned long x){char b[11];unsigned i;for(i=0;i<8;++i)b[i]="0123456789abcdef"[(x>>(28-4*i))&15];b[8]='\r';b[9]='\n';b[10]=0;nf_call(nf_id("NF_STDERR"),s);nf_call(nf_id("NF_STDERR"),b);}
struct MovemCase{
 unsigned size,mask,load,update,base,pc_data;
 int offset,a2_pointer,a2_offset;unsigned long d0,d1,a1;unsigned a7_offset;
 void(*fn)(void);unsigned char*pc;
};
#include "movem-cases.h"
static long run(void){
 volatile unsigned long*w=(volatile unsigned long*)0x00fec000UL;
 unsigned c=0,ccr=0,i=0,j,retry,offset,n,count=0,native=0,noflags=0,zero=0;int reg;long error=0,require_native=0;
 unsigned long expected[16]={0},value=0,before=0,after=0,nf_before=0,nf_after=0;
 unsigned char bytes[512]={0};char args[32];long bootstrap=nf_id("BOOTSTRAP");const struct MovemCase*p=0;args[0]=0;
 if(bootstrap>0)nf_call(bootstrap+2,args,(long)sizeof(args));require_native=!strcmp(args,"native");
 if(require_native&&!(movemo_jit_query()&1)){error=5;goto done;}
 w[0]=(unsigned long)&movemo_begin;w[1]=(unsigned long)&movemo_end;
 for(c=0;c<sizeof(cases)/sizeof(cases[0]);++c)for(ccr=0;ccr<32;++ccr){
  p=&cases[c];++count;n=0;for(i=0;i<16;++i)if(p->mask&(1u<<i))++n;
  for(retry=0;retry<64;++retry){
   for(i=0;i<16;++i)movemo_input[i]=0x5a008000UL+i*0x10801UL+(ccr&3)*0x4000UL;
   movemo_pointer=(unsigned long)&movemo_data[128];
   movemo_input[0]=p->d0;movemo_input[1]=p->d1;movemo_input[9]=p->a1;
   movemo_input[10]=(p->a2_pointer?(unsigned long)&movemo_pointer:(unsigned long)&movemo_data[0])+p->a2_offset;
   movemo_input[15]=(unsigned long)&movemo_data[p->a7_offset];
   for(i=0;i<16;++i)expected[i]=movemo_input[i];
   for(i=0;i<512;++i)movemo_data[i]=bytes[i]=(unsigned char)(i*37+0x80+(ccr&3)*17);
   offset=p->offset;
   if(p->update==1)offset-=n*p->size;
   if(p->load){
    for(reg=0;reg<16;++reg)if(p->mask&(1u<<reg)){
     value=0;for(j=0;j<p->size;++j)value=(value<<8)|(p->pc_data?(unsigned char)((offset+j)*19+0x80):bytes[offset+j]);
     if(p->size==2&&(value&0x8000))value|=0xffff0000UL;
     expected[reg]=value;offset+=p->size;
    }
   }else if(p->update==1){
    offset=p->offset;
    for(reg=15;reg>=0;--reg)if(p->mask&(1u<<reg)){
     offset-=p->size;value=movemo_input[reg];
     if(reg==8+(int)p->base){value-=p->size;
#ifdef MOVEMO_BAD_BASE
      value-=p->size;
#endif
     }
     for(j=0;j<p->size;++j)bytes[offset+j]=(unsigned char)(value>>(8*(p->size-1-j)));
    }
   }else for(reg=0;reg<16;++reg)if(p->mask&(1u<<reg)){
    value=movemo_input[reg];for(j=0;j<p->size;++j)bytes[offset+j]=(unsigned char)(value>>(8*(p->size-1-j)));offset+=p->size;
   }
   if(p->update)expected[8+p->base]=(unsigned long)&movemo_data[offset];
#ifdef MOVEMO_BAD_POST_BASE
   if(p->update==2&&(p->mask&(1u<<(8+p->base))))expected[8+p->base]=0;
#endif
   movemo_ccr=ccr;w[2]=(unsigned long)p->pc;before=w[2];nf_before=w[3];p->fn();after=w[2];nf_after=w[3];
   for(i=0;i<16;++i)if(movemo_output[i]!=expected[i]){error=2;goto done;}
   if(movemo_pointer!=(unsigned long)&movemo_data[128]){error=6;goto done;}
   for(i=0;i<512;++i)if(movemo_data[i]!=bytes[i]){error=1;goto done;}
   if(movemo_reply!=ccr){error=3;goto done;}
   if(!require_native||!p->mask){if(after!=before||nf_after!=nf_before){error=5;goto done;}if(!p->mask)++zero;break;}
   if(after==before+1&&nf_after==nf_before+1){++native;++noflags;break;}
   if(after!=before||nf_after!=nf_before){error=5;goto done;}
  }
  if(retry==64){error=5;goto done;}
 }
done:
 hex("MOVEMO checks=",count);hex("MOVEMO native=",native);hex("MOVEMO noflags=",noflags);hex("MOVEMO zero-mask=",zero);
 if(error){hex("MOVEMO case=",c);hex("MOVEMO size=",p?p->size:0);hex("MOVEMO mask=",p?p->mask:0);hex("MOVEMO PC=",p?(unsigned long)p->pc:0);hex("MOVEMO CCR=",ccr);hex("MOVEMO index=",i);hex("MOVEMO error=",error);hex("MOVEMO expected=",error==1?bytes[i]:i<16?expected[i]:0);hex("MOVEMO actual=",error==1?movemo_data[i]:i<16?movemo_output[i]:0);hex("MOVEMO before=",before);hex("MOVEMO after=",after);hex("MOVEMO nf-before=",nf_before);hex("MOVEMO nf-after=",nf_after);}
 nf_call(nf_id("NF_STDERR"),error?"MOVEMO ORACLE FAIL\r\n":"MOVEMO ORACLE PASS literalEA allGPR allCCR allRAM zero-mask-fallback\r\n");nf_call(nf_id("NF_EXIT"),error);return error;
}
int main(void){return (int)Supexec(run);}
