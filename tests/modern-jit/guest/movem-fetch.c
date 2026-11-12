/* SPDX-License-Identifier: GPL-2.0-or-later
 * Warm native target -> data fault -> live EA becomes mapped RAM -> RTE ->
 * extension fetch fault -> RTE -> saved device EA completes. No native target
 * execution is allowed between either RTE and its genuine restarted body.
 */
#include <mint/osbind.h>
#include <string.h>
extern long nf_id(const char*),nf_call(long,...),movemo_jit_query(void);
extern unsigned char movemo_begin,movemo_end;extern void movemx_handler(void);
unsigned long movemo_input[16],movemo_output[16],movemo_real_sp,movemo_target_pointer,movemo_pointer;
unsigned long movemx_fault_regs[32],movemx_fault_count,movemx_native[2],movemx_bus_counts[2],movemx_fetch_counts[2],movemx_live_a2,movemx_live_pointer,movemx_case;
unsigned short movemo_ccr,movemo_reply,movemo_saved_sr;
unsigned char movemo_data[512],movemx_stack[512],movemx_frames[120];
__asm__(".text\n_nf_id:\n.word 0x7300\nrts\n_nf_call:\n.word 0x7301\nrts\n_movemo_jit_query:\nmoveq #-1,%d0\n.word 0x713b\nrts\n");
static void hex(const char*s,unsigned long x){char b[11];unsigned i;for(i=0;i<8;++i)b[i]="0123456789abcdef"[(x>>(28-4*i))&15];b[8]='\r';b[9]='\n';b[10]=0;nf_call(nf_id("NF_STDERR"),s);nf_call(nf_id("NF_STDERR"),b);}
static unsigned long frame(unsigned slot,unsigned offset,unsigned size){unsigned i;unsigned long v=0;for(i=0;i<size;++i)v=(v<<8)|movemx_frames[slot*60+offset+i];return v;}
void movemx_repeat_fail(void){hex("MOVEMX MULTIPLE FAULTS case=",movemx_case);hex("MOVEMX first-PC=",frame(0,2,4));hex("MOVEMX second-PC=",frame(1,2,4));nf_call(nf_id("NF_EXIT"),7);}
struct FetchCase{unsigned size,watch_offset,watch_size,fault_index;int bd,outer;void(*fn)(void);unsigned char*pc;};
#include "movem-fetch-cases.h"
static void init(const struct FetchCase*p,unsigned ccr,int cold){unsigned i;
 for(i=0;i<16;++i)movemo_input[i]=0x89ab8000UL+i*0x103UL+(ccr&3)*0x4000UL;
 for(i=0;i<512;++i)movemo_data[i]=(unsigned char)(i*37+0x80);
 movemx_live_pointer=(unsigned long)&movemo_data[128]-p->outer;movemx_live_a2=(unsigned long)&movemo_pointer-p->bd;
 movemo_pointer=cold?0x89ab891bUL:movemx_live_pointer;
 movemo_input[10]=cold?0x00fed240UL-p->bd:movemx_live_a2;movemo_input[15]=(unsigned long)&movemx_stack[384];movemo_ccr=ccr;
}
static long run(void){
 volatile unsigned long*w=(volatile unsigned long*)0x00fec000UL,*ctl=(volatile unsigned long*)0x00fed280UL,*vector=(volatile unsigned long*)8;
 const unsigned selected[4]={0,1,10,11};unsigned c=0,ccr=0,i=0,j,k,slot,retry,checks=0,warmed=0,fetch_faults=0,event=0,events;
 unsigned long old_vector=*vector,before=0,after=0,nf_before=0,values[4],first[16],second[16],output[16],value=0,address=0,info=0,watched=0,expected_attempts;
 long error=0,require_native=0;const struct FetchCase*p=0;char args[32];long bootstrap=nf_id("BOOTSTRAP");args[0]=0;
 if(bootstrap>0)nf_call(bootstrap+2,args,(long)sizeof(args));require_native=!strcmp(args,"native");
 if(require_native&&!(movemo_jit_query()&1)){error=5;goto done;}
 *vector=(unsigned long)movemx_handler;w[0]=(unsigned long)&movemo_begin;w[1]=(unsigned long)&movemo_end;
 for(c=0;c<sizeof(cases)/sizeof(cases[0]);++c)for(ccr=0;ccr<32;++ccr){
  p=&cases[c];++checks;movemx_case=c;w[2]=(unsigned long)p->pc;ctl[0]=0;
  if(require_native){
   for(retry=0;retry<64;++retry){init(p,ccr,0);before=w[2];nf_before=w[3];p->fn();after=w[2];if(after==before+1&&w[3]==nf_before+1)break;if(after!=before){error=5;goto done;}}
   if(retry==64){error=5;goto done;}++warmed;
  }
  init(p,ccr,1);movemx_fault_count=0;for(i=0;i<16;++i)first[i]=second[i]=output[i]=movemo_input[i];first[15]-=60;second[15]-=60;second[10]=movemx_live_a2;
  for(k=0;k<4;++k){
   value=0;for(j=0;j<p->size;++j)value=(value<<8)|(unsigned char)((k*p->size+j)*19+0x80);values[k]=value;
   if(p->size==2&&(value&0x8000))value|=0xffff0000UL;output[selected[k]]=value;
   if(k<p->fault_index){first[selected[k]]=value;if(selected[k]!=10)second[selected[k]]=value;}
  }
  ctl[0]=0;ctl[11]=0x00fed200UL-p->outer;ctl[9]=p->fault_index+1;watched=(unsigned long)p->pc+p->watch_offset;ctl[12]=watched;
  before=w[2];p->fn();after=w[2];
  if(movemx_fault_count!=2){error=2;goto done;}
  if(after!=before||movemx_native[0]!=before||movemx_native[1]!=before){error=5;goto done;}
  for(slot=0;slot<2;++slot){
   if(frame(slot,0,2)!=(0x2700|ccr)||frame(slot,2,4)!=(unsigned long)p->pc||frame(slot,6,2)!=0x7008||frame(slot,8,4)!=0x00fed200UL||frame(slot,14,2)){error=2;goto done;}
   if(frame(slot,12,2)!=(0x1100|(slot?6:5)|((slot?p->watch_size:p->size)==2?0x40:0))||frame(slot,20,4)!=(slot?watched:0x00fed200UL+p->fault_index*p->size)){error=2;goto done;}
   if(movemx_bus_counts[slot]!=p->fault_index+2||movemx_fetch_counts[slot]!=slot+1){error=4;goto done;}
   for(i=0;i<16;++i)if(movemx_fault_regs[slot*16+i]!=(slot?second[i]:first[i])){error=1;goto done;}
  }
  for(i=0;i<16;++i)if(movemo_output[i]!=output[i]){error=1;goto done;}
  if(movemo_reply!=ccr||movemo_pointer!=movemx_live_pointer){error=1;goto done;}
  for(i=0;i<512;++i)if(movemo_data[i]!=(unsigned char)(i*37+0x80)){error=1;goto done;}
  expected_attempts=3;
#ifdef MOVEMX_BAD_COUNT
  ++expected_attempts;
#endif
  if(ctl[14]!=expected_attempts||ctl[15]!=1||ctl[16]!=p->watch_size||ctl[17]!=6){error=4;goto done;}++fetch_faults;
  events=p->fault_index+6;if(ctl[1]!=events||ctl[6]!=p->fault_index+5||ctl[7]||ctl[8]!=1){error=4;goto done;}
  for(event=0;event<events;++event){
   if(!event){address=0x00fed240UL;info=4;value=0x00fed200UL-p->outer;}
   else if(event<=p->fault_index+1){k=event-1;address=0x00fed200UL+k*p->size;info=p->size|(k==p->fault_index?0x10000:0);value=k==p->fault_index?0:values[k];}
   else {k=event-p->fault_index-2;address=0x00fed200UL+k*p->size;info=p->size;value=values[k];}
   ctl[2]=event;if(ctl[3]!=address||ctl[4]!=info||ctl[5]!=value){error=4;goto done;}
  }
 }
done:
 *vector=old_vector;hex("MOVEMX checks=",checks);hex("MOVEMX warmed=",warmed);hex("MOVEMX fetch-faults=",fetch_faults);
 if(error){hex("MOVEMX case=",c);hex("MOVEMX CCR=",ccr);hex("MOVEMX index=",i);hex("MOVEMX event=",event);hex("MOVEMX error=",error);hex("MOVEMX fault-count=",movemx_fault_count);hex("MOVEMX first-PC=",frame(0,2,4));hex("MOVEMX second-PC=",frame(1,2,4));hex("MOVEMX second-SSW=",frame(1,12,2));hex("MOVEMX first-fetch-count=",movemx_fetch_counts[0]);hex("MOVEMX second-fetch-count=",movemx_fetch_counts[1]);hex("MOVEMX before=",before);hex("MOVEMX after=",after);}
 nf_call(nf_id("NF_STDERR"),error?"MOVEMX ORACLE FAIL\r\n":"MOVEMX ORACLE PASS two-vector2 two-RTE extension-fetch native-holdoff saved-deviceEA pointer-once allGPR allCCR\r\n");nf_call(nf_id("NF_EXIT"),error);return error;
}
int main(void){return (int)Supexec(run);}
