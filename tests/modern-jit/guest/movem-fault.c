/* SPDX-License-Identifier: GPL-2.0-or-later
 * CM: MC68040UM PDF238. Genuine RTE must use the saved calculated EA;
 * indexed device-pointer calculation must not be repeated on continuation.
 * Earlier operand loads may repeat only AFTER the guest RTE. Store faults
 * deliberately discard the remainder and test WB3, not a write-restart claim.
 */
#include <mint/osbind.h>
#include <string.h>
extern long nf_id(const char*),nf_call(long,...),movemo_jit_query(void);
extern unsigned char movemo_begin,movemo_end;
extern void movemf_handler(void);
unsigned long movemo_input[16],movemo_output[16],movemo_real_sp,movemo_target_pointer,movemo_pointer;
unsigned long movemf_fault_regs[16],movemf_fault_count,movemf_next,movemf_case,movemf_current_ccr;
unsigned short movemo_ccr,movemo_reply,movemo_saved_sr,movemf_write,movemf_poison_pointer;
unsigned char movemo_data[512],movemf_stack[512],movemf_frame[60];
__asm__(".text\n_nf_id:\n.word 0x7300\nrts\n_nf_call:\n.word 0x7301\nrts\n_movemo_jit_query:\nmoveq #-1,%d0\n.word 0x713b\nrts\n");
static void hex(const char*s,unsigned long x){char b[11];unsigned i;for(i=0;i<8;++i)b[i]="0123456789abcdef"[(x>>(28-4*i))&15];b[8]='\r';b[9]='\n';b[10]=0;nf_call(nf_id("NF_STDERR"),s);nf_call(nf_id("NF_STDERR"),b);}
static unsigned long frame(unsigned offset,unsigned size){unsigned i;unsigned long v=0;for(i=0;i<size;++i)v=(v<<8)|movemf_frame[offset+i];return v;}
void movemf_repeat_fail(void){hex("MOVEMF MULTIPLE FAULTS case=",movemf_case);hex("MOVEMF MULTIPLE FAULTS CCR=",movemf_current_ccr);hex("MOVEMF first-frame-PC=",frame(2,4));hex("MOVEMF first-frame-EA=",frame(8,4));hex("MOVEMF first-frame-SSW=",frame(12,2));nf_call(nf_id("NF_EXIT"),7);}
struct FaultCase{unsigned size,mask,load,update,indirect,fault_index;void(*fn)(void);unsigned char*pc,*next;};
#include "movem-fault-cases.h"
static void init(const struct FaultCase*p,unsigned ccr,int cold){unsigned i;
 for(i=0;i<16;++i)movemo_input[i]=0x89ab8000UL+i*0x103UL+(ccr&3)*0x4000UL;
 for(i=0;i<512;++i)movemo_data[i]=(unsigned char)(i*37+0x80);
 movemo_input[15]=(unsigned long)&movemf_stack[384];movemo_pointer=(unsigned long)&movemo_data[128];
 movemo_input[10]=(unsigned long)&movemo_data[p->update==1?192:128];
 if(p->indirect)movemo_input[10]=(unsigned long)&movemo_pointer;
 if(cold){movemo_input[10]=p->indirect==1?0x00fed240UL:p->update==1?0x00fed240UL:0x00fed200UL;if(p->indirect==2)movemo_pointer=0x00fed200UL;}
 movemo_ccr=ccr;
}
static long run(void){
 volatile unsigned long*w=(volatile unsigned long*)0x00fec000UL,*ctl=(volatile unsigned long*)0x00fed280UL,*debug=(volatile unsigned long*)0x00fed300UL,*vector=(volatile unsigned long*)8;
 unsigned c=0,ccr=0,i=0,j,k,n,checks=0,warmed=0,retry,index=0,events,reads,writes,offsets[16],regs[16];
 long error=0,require_native=0;unsigned long old_vector=*vector,expected_fault[16],expected_output[16],values[16],before=0,after=0,nf_before=0,value=0,ssw,ea,expected_ea,address=0,info=0;
 unsigned long event_address[33],event_info[33],event_value[33];unsigned char device[64];
 const struct FaultCase*p=0;char args[32];long bootstrap=nf_id("BOOTSTRAP");args[0]=0;
 if(bootstrap>0)nf_call(bootstrap+2,args,(long)sizeof(args));require_native=!strcmp(args,"native");
 if(require_native&&!(movemo_jit_query()&1)){error=5;goto done;}
 *vector=(unsigned long)movemf_handler;
 w[0]=(unsigned long)&movemo_begin;w[1]=(unsigned long)&movemo_end;
 for(c=0;c<sizeof(cases)/sizeof(cases[0]);++c)for(ccr=0;ccr<32;++ccr){
  p=&cases[c];++checks;w[2]=(unsigned long)p->pc;movemf_case=c;movemf_current_ccr=ccr;
  if(require_native){
   for(retry=0;retry<64;++retry){init(p,ccr,0);before=w[2];nf_before=w[3];p->fn();after=w[2];if(after==before+1&&w[3]==nf_before+1)break;if(after!=before){error=5;goto done;}}
   if(retry==64){error=5;goto done;}++warmed;
  }
  init(p,ccr,1);movemf_fault_count=0;movemf_write=!p->load;movemf_poison_pointer=p->indirect==2;movemf_next=(unsigned long)p->next;
  for(i=0;i<16;++i)expected_fault[i]=expected_output[i]=movemo_input[i];
  expected_fault[15]-=60;
  for(i=0;i<64;++i)device[i]=(unsigned char)(i*19+0x80);
  n=0;
  if(p->update==1){for(k=16;k>0;--k)if(p->mask&(1u<<(k-1))){regs[n]=k-1;offsets[n]=64-(n+1)*p->size;++n;}}
  else for(k=0;k<16;++k)if(p->mask&(1u<<k)){regs[n]=k;offsets[n]=n*p->size;++n;}
  for(k=0;k<n;++k){
   if(p->load){value=0;for(j=0;j<p->size;++j)value=(value<<8)|device[offsets[k]+j];}
   else {value=movemo_input[regs[k]];if(p->update==1&&regs[k]==10)value-=p->size;if(p->size==2)value&=65535UL;}
   values[k]=value;
   if(p->load){
    value=p->size==2&&(value&0x8000)?value|0xffff0000UL:value;expected_output[regs[k]]=value;
    if(k<p->fault_index)expected_fault[regs[k]]=value;
   }else if(k<p->fault_index)for(j=0;j<p->size;++j)device[offsets[k]+j]=(unsigned char)(value>>(8*(p->size-1-j)));
  }
  if(p->update==2)expected_output[10]=0x00fed200UL+n*p->size;
  ctl[0]=0;ctl[11]=0x00fed200UL;ctl[9]=p->fault_index+(p->indirect==1);
  events=reads=writes=0;
  if(p->indirect==1){event_address[events]=0x00fed240UL;event_info[events]=4;event_value[events]=0x00fed200UL;++events;++reads;}
  for(k=0;k<=p->fault_index;++k){
   event_address[events]=0x00fed200UL+offsets[k];event_info[events]=p->size|(p->load?0:0x100)|(k==p->fault_index?0x10000:0);event_value[events]=p->load&&k==p->fault_index?0:values[k];++events;
   if(k<p->fault_index){if(p->load)++reads;else ++writes;}
  }
  if(p->load)for(k=0;k<n;++k){event_address[events]=0x00fed200UL+offsets[k];event_info[events]=p->size;event_value[events]=values[k];++events;++reads;}
  before=w[2];p->fn();after=w[2];
  if(after!=before){error=5;goto done;}
  ea=p->update==1?0x00fed240UL:0x00fed200UL;expected_ea=ea;
#ifdef MOVEMF_BAD_CM
  expected_ea^=2;
#endif
  ssw=0x1005|(p->size==2?0x40:0)|(p->load?0x100:0);
  if(movemf_fault_count!=1||frame(0,2)!=(0x2700|ccr)||frame(2,4)!=(unsigned long)p->pc||frame(6,2)!=0x7008||frame(8,4)!=expected_ea||frame(12,2)!=ssw||frame(20,4)!=0x00fed200UL+offsets[p->fault_index]){error=2;goto done;}
  if(p->load){if(frame(14,2)){error=2;goto done;}}
  else if(frame(14,2)!=(0x85|(p->size==2?0x40:0))||frame(24,4)!=0x00fed200UL+offsets[p->fault_index]||frame(28,4)!=values[p->fault_index]){error=2;goto done;}
  for(i=0;i<16;++i)if(movemf_fault_regs[i]!=expected_fault[i]||movemo_output[i]!=expected_output[i]){error=1;goto done;}
  if(movemo_reply!=ccr||movemo_pointer!=(p->indirect==2?0x89ab891bUL:(unsigned long)&movemo_data[128])){error=1;goto done;}
  for(i=0;i<512;++i)if(movemo_data[i]!=(unsigned char)(i*37+0x80)){error=1;goto done;}
  for(i=0;i<16;++i){value=0;for(j=0;j<4;++j)value=(value<<8)|device[i*4+j];if(debug[i]!=value){error=1;goto done;}}
#ifdef MOVEMF_BAD_REPEAT
  if(p->load)events-=p->fault_index;
#endif
  if(ctl[1]!=events||ctl[6]!=reads||ctl[7]!=writes||ctl[8]!=1){error=4;goto done;}
  for(index=0;index<events;++index){ctl[2]=index;address=event_address[index];info=event_info[index];value=event_value[index];if(ctl[3]!=address||ctl[4]!=info||ctl[5]!=value){error=4;goto done;}}
 }
done:
 *vector=old_vector;
 hex("MOVEMF checks=",checks);hex("MOVEMF warmed=",warmed);
 if(error){hex("MOVEMF case=",c);hex("MOVEMF CCR=",ccr);hex("MOVEMF index=",i);hex("MOVEMF event=",index);hex("MOVEMF error=",error);hex("MOVEMF frame-PC=",frame(2,4));hex("MOVEMF frame-EA=",frame(8,4));hex("MOVEMF SSW=",frame(12,2));hex("MOVEMF WB3=",frame(28,4));hex("MOVEMF before=",before);hex("MOVEMF after=",after);hex("MOVEMF bus-address=",address);hex("MOVEMF bus-info=",info);hex("MOVEMF bus-value=",value);}
 nf_call(nf_id("NF_STDERR"),error?"MOVEMF ORACLE FAIL\r\n":"MOVEMF ORACLE PASS partial-read CM/RTE savedEA pointer-once partial-write WB3/discard exactbus allGPR allCCR\r\n");nf_call(nf_id("NF_EXIT"),error);return error;
}
int main(void){return (int)Supexec(run);}
