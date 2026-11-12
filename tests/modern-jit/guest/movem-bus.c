/* SPDX-License-Identifier: GPL-2.0-or-later
 * Read-sensitive, ordered bus oracle. All real register setup/capture uses
 * movem-oracle.S individual MOVEs. Device inspection is outside operands.
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
struct BusCase {unsigned size,mask,load,update,base,kind;void(*fn)(void);unsigned char*pc;};
#include "movem-bus-cases.h"
static void init(const struct BusCase*p,unsigned ccr,int cold){unsigned i;
 for(i=0;i<16;++i)movemo_input[i]=0x89ab8000UL+i*0x103UL+(ccr&3)*0x4000UL;
 for(i=0;i<512;++i)movemo_data[i]=(unsigned char)(i*37+0x80);
 movemo_pointer=(unsigned long)&movemo_data[128];
 movemo_input[10]=(unsigned long)&movemo_data[p->update==1?192:128];
 movemo_input[15]=(unsigned long)&movemo_data[p->base==7?(p->update==1?192:128):384];
 if(p->kind>=2&&p->kind<=4)movemo_input[10]=(unsigned long)&movemo_pointer;
 if(cold){
  if(p->kind==0)movemo_input[10]=p->update==1?0x00fed240UL:0x00fed200UL;
  if(p->kind==1)movemo_input[15]=p->update==1?0x00fed240UL:0x00fed200UL;
  if(p->kind==2)movemo_pointer=0x00fed200UL;
  if(p->kind==3||p->kind==4)movemo_input[10]=0x00fed240UL;
 }
 movemo_ccr=ccr;
}
static long run(void){
 volatile unsigned long*w=(volatile unsigned long*)0x00fec000UL,*ctl=(volatile unsigned long*)0x00fed280UL,*debug=(volatile unsigned long*)0x00fed300UL;
 unsigned c=0,ccr=0,i=0,j,n,checks=0,warmed=0,zero=0,retry,event_count,reads,writes,index=0;int reg;
 long error=0,require_native=0;unsigned long expected[16],before=0,after=0,nf_before=0,offset,value=0,operand=0,address=0,info=0;
 unsigned long event_address[17],event_info[17],event_value[17];unsigned char ram[512],device[64];
 const struct BusCase*p=0;char args[32];long bootstrap=nf_id("BOOTSTRAP");args[0]=0;
 if(bootstrap>0)nf_call(bootstrap+2,args,(long)sizeof(args));require_native=!strcmp(args,"native");
 if(require_native&&!(movemo_jit_query()&1)){error=5;goto done;}
 w[0]=(unsigned long)&movemo_begin;w[1]=(unsigned long)&movemo_end;
 for(c=0;c<sizeof(cases)/sizeof(cases[0]);++c)for(ccr=0;ccr<32;++ccr){
  p=&cases[c];++checks;n=0;for(i=0;i<16;++i)if(p->mask&(1u<<i))++n;
  w[2]=(unsigned long)p->pc;
  if(require_native&&p->mask&&p->kind<5){
   for(retry=0;retry<64;++retry){init(p,ccr,0);before=w[2];nf_before=w[3];p->fn();after=w[2];if(after==before+1&&w[3]==nf_before+1)break;if(after!=before){error=5;goto done;}}
   if(retry==64){error=5;goto done;}++warmed;
  }
  init(p,ccr,1);for(i=0;i<16;++i)expected[i]=movemo_input[i];
  for(i=0;i<512;++i)ram[i]=movemo_data[i];for(i=0;i<64;++i)device[i]=(unsigned char)(i*19+0x80);
  ctl[0]=0;ctl[11]=p->kind==3?(unsigned long)&movemo_data[128]:0x00fed200UL;
  event_count=reads=writes=0;
  if(p->kind==3||p->kind==4){event_address[0]=0x00fed240UL;event_info[0]=4;event_value[0]=ctl[11]=p->kind==3?(unsigned long)&movemo_data[128]:0x00fed200UL;++event_count;++reads;}
  operand=p->kind==3?(unsigned long)&movemo_data[128]:p->kind==6?0xfffffa80UL:0x00fed200UL;
  offset=p->update==1?64-n*p->size:0;
  if(p->load){
   for(reg=0;reg<16;++reg)if(p->mask&(1u<<reg)){
    value=0;for(j=0;j<p->size;++j)value=(value<<8)|(p->kind==3?ram[128+offset+j]:device[offset+j]);
    expected[reg]=p->size==2&&(value&0x8000)?value|0xffff0000UL:value;
    if(p->kind!=3){event_address[event_count]=operand+offset;event_info[event_count]=p->size;event_value[event_count]=value;++event_count;++reads;}
    offset+=p->size;
   }
  }else if(p->update==1){
   offset=64;
   for(reg=15;reg>=0;--reg)if(p->mask&(1u<<reg)){
    offset-=p->size;value=movemo_input[reg];if(reg==8+(int)p->base)value-=p->size;
    for(j=0;j<p->size;++j)device[offset+j]=(unsigned char)(value>>(8*(p->size-1-j)));
    event_address[event_count]=operand+offset;event_info[event_count]=p->size|0x100;event_value[event_count]=p->size==2?value&65535UL:value;++event_count;++writes;
   }
  }else for(reg=0;reg<16;++reg)if(p->mask&(1u<<reg)){
   value=movemo_input[reg];for(j=0;j<p->size;++j)if(p->kind==3)ram[128+offset+j]=(unsigned char)(value>>(8*(p->size-1-j)));else device[offset+j]=(unsigned char)(value>>(8*(p->size-1-j)));
   if(p->kind!=3){event_address[event_count]=operand+offset;event_info[event_count]=p->size|0x100;event_value[event_count]=p->size==2?value&65535UL:value;++event_count;++writes;}
   offset+=p->size;
  }
  if(p->update)expected[8+p->base]=operand+offset;
  before=w[2];p->fn();after=w[2];
  if(after!=before){error=5;goto done;}
  for(i=0;i<16;++i)if(movemo_output[i]!=expected[i]){error=2;goto done;}
  if(movemo_reply!=ccr){error=3;goto done;}
  if(movemo_pointer!=(p->kind==2?0x00fed200UL:(unsigned long)&movemo_data[128])){error=1;goto done;}
  for(i=0;i<512;++i)if(movemo_data[i]!=ram[i]){error=1;goto done;}
  for(i=0;i<16;++i){value=0;for(j=0;j<4;++j)value=(value<<8)|device[i*4+j];if(debug[i]!=value){error=1;goto done;}}
#ifdef MOVEMB_BAD_COUNT
  ++event_count;
#endif
  if(ctl[1]!=event_count||ctl[6]!=reads||ctl[7]!=writes||ctl[8]){error=4;goto done;}
  for(index=0;index<event_count;++index){
   ctl[2]=index;address=event_address[index];info=event_info[index];value=event_value[index];
#ifdef MOVEMB_BAD_ORDER
   if(!index)address^=2;
#endif
   if(ctl[3]!=address||ctl[4]!=info||ctl[5]!=value){error=4;goto done;}
  }
  if(!p->mask)++zero;
 }
done:
 hex("MOVEMB checks=",checks);hex("MOVEMB warmed=",warmed);hex("MOVEMB zero-mask=",zero);
 if(error){hex("MOVEMB case=",c);hex("MOVEMB size=",p?p->size:0);hex("MOVEMB mask=",p?p->mask:0);hex("MOVEMB kind=",p?p->kind:0);hex("MOVEMB CCR=",ccr);hex("MOVEMB index=",index);hex("MOVEMB error=",error);hex("MOVEMB address=",address);hex("MOVEMB info=",info);hex("MOVEMB value=",value);hex("MOVEMB before=",before);hex("MOVEMB after=",after);}
 nf_call(nf_id("NF_STDERR"),error?"MOVEMB ORACLE FAIL\r\n":"MOVEMB ORACLE PASS warmed-deopt exactbus-order counts values pointer-once allGPR allCCR allRAM\r\n");nf_call(nf_id("NF_EXIT"),error);return error;
}
int main(void){return (int)Supexec(run);}
