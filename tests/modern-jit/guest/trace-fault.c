/* SPDX-License-Identifier: GPL-2.0-or-later
 * Independent manual-derived frames, transfer ledger and all-register oracle.
 */
#include <mint/osbind.h>
#include <string.h>
extern long nf_id(const char*),nf_call(long,...),movemo_jit_query(void);
extern unsigned char movemo_begin,movemo_end,movemo_capture;
extern void trace_access_handler(void),trace_handler(void);
unsigned long movemo_input[16],movemo_output[16],movemo_real_sp,movemo_target_pointer;
unsigned long trace_fault_regs[16],trace_regs[16],trace_fault_count,trace_count,trace_faults_seen;
unsigned long trace_target,trace_next,trace_fault_native[2],trace_native[2],trace_saved_a0;
unsigned short movemo_ccr,movemo_reply,movemo_saved_sr,trace_sr;
unsigned char movemo_data[512],trace_stack[512],trace_fault_frame[60],trace_frame[12];
__asm__(".text\n_nf_id:\n.word 0x7300\nrts\n_nf_call:\n.word 0x7301\nrts\n_movemo_jit_query:\nmoveq #-1,%d0\n.word 0x713b\nrts\n");
static void hex(const char*s,unsigned long x){char b[11];unsigned i;for(i=0;i<8;++i)b[i]="0123456789abcdef"[(x>>(28-4*i))&15];b[8]='\r';b[9]='\n';b[10]=0;nf_call(nf_id("NF_STDERR"),s);nf_call(nf_id("NF_STDERR"),b);}
void trace_repeat_fail(void){nf_call(nf_id("NF_STDERR"),"TRACE duplicate or misordered handler\r\n");nf_call(nf_id("NF_EXIT"),7);}
static unsigned long frame(const unsigned char*f,unsigned off,unsigned size){unsigned i;unsigned long x=0;for(i=0;i<size;++i)x=(x<<8)|f[off+i];return x;}
struct TraceCase{unsigned kind,size,scenario;void(*fn)(void);unsigned char*pc,*next,*branch;};
#include "trace-cases.h"
static unsigned long mask_for(unsigned size){return size==1?255UL:size==2?65535UL:0xffffffffUL;}
static unsigned long query(volatile unsigned long*w,unsigned char*pc){w[2]=(unsigned long)pc;return w[2];}
static void init(const struct TraceCase*p,unsigned ccr,unsigned t,int cold){unsigned i;
 for(i=0;i<16;++i)movemo_input[i]=0x12340000UL+i*0x103UL;
 movemo_input[0]=0xfedcba98UL;movemo_input[1]=0x12345678UL;
 for(i=0;i<512;++i)movemo_data[i]=(unsigned char)(i*37+0x80);
 for(i=0;i<4;++i)movemo_data[128+i]=(unsigned char)(0x89abcdefUL>>(8*(3-i)));
 movemo_input[10]=cold&&(p->scenario==1||p->scenario==2)?0x00fed200UL:(unsigned long)&movemo_data[128];
 movemo_input[15]=(unsigned long)&trace_stack[384];movemo_ccr=ccr;trace_sr=(unsigned short)(0x2700|ccr|(cold?t:0));
 trace_fault_count=trace_count=trace_faults_seen=0;
}
static long run(void){
 volatile unsigned long*w=(volatile unsigned long*)0x00fec000UL,*ctl=(volatile unsigned long*)0x00fed280UL,*debug=(volatile unsigned long*)0x00fed300UL;
 volatile unsigned long*v2=(volatile unsigned long*)8,*v9=(volatile unsigned long*)36;
 unsigned long old2=*v2,old9=*v9,expected[16],fault[16],before=0,after=0,next_before=0,next_after=0,mask,old,value,flags,fc,ct,expected_pc=0,expected_trace=0,data_expected;
 unsigned long ev_a[4],ev_i[4],ev_v[4],ram_word;unsigned c=0,ccr=0,t=0,i=0,j,event=0,events,reads,writes,retry,checks=0,warmed=0,faults,fetches;
 unsigned char ram[512],device[64];long error=0,native=0;char args[32];const struct TraceCase*p=0;long bootstrap=nf_id("BOOTSTRAP");args[0]=0;
 if(bootstrap>0)nf_call(bootstrap+2,args,(long)sizeof(args));native=!strcmp(args,"native");if(native&&!(movemo_jit_query()&1)){error=5;goto done;}
 *v2=(unsigned long)trace_access_handler;*v9=(unsigned long)trace_handler;w[0]=(unsigned long)&movemo_begin;w[1]=(unsigned long)&movemo_end;
 for(c=0;c<sizeof(cases)/sizeof(cases[0]);++c)for(t=0x4000;t<=0x8000;t+=0x4000)for(ccr=0;ccr<32;++ccr){
  p=&cases[c];++checks;trace_target=(unsigned long)p->pc;trace_next=(unsigned long)p->next;ctl[0]=0;
  if(native){for(retry=0;retry<64;++retry){init(p,ccr,t,0);before=query(w,p->pc);next_before=query(w,p->next);p->fn();after=query(w,p->pc);next_after=query(w,p->next);if(after==before+1&&next_after==next_before+1)break;if(after>before+1||next_after>next_before+1){error=5;goto done;}}if(retry==64){error=5;goto done;}++warmed;}
  init(p,ccr,t,1);for(i=0;i<16;++i)expected[i]=fault[i]=movemo_input[i];for(i=0;i<512;++i)ram[i]=movemo_data[i];for(i=0;i<64;++i)device[i]=(unsigned char)(i*19+0x80);
  ctl[0]=0;ctl[18]=0x89abcdefUL;for(i=0;i<4;++i)device[i]=(unsigned char)(0x89abcdefUL>>(8*(3-i)));
  mask=mask_for(p->size);old=0x89abcdefUL>>(8*(4-p->size));value=p->kind==0?old:p->kind==1?movemo_input[0]&mask:p->kind==2?(old+1)&mask:movemo_input[1]|1;
  flags=p->kind==2?8:p->kind==3?(ccr&16):(ccr&16)|8;
  if(p->kind==0)expected[1]=(movemo_input[1]&~mask)|value;
  if(p->kind==3)expected[1]=value;else expected[10]+=p->size;
  if(p->scenario==2)for(i=0;i<16;++i)fault[i]=expected[i];fault[15]-=60;
  faults=p->scenario==0?0:1;fetches=p->scenario>=3?1:0;events=reads=writes=0;
  if(p->scenario==1||p->scenario==2){
   if(p->kind==2&&p->scenario==2){ev_a[events]=0x00fed200UL;ev_i[events]=p->size;ev_v[events]=old;++events;++reads;}
   ev_a[events]=0x00fed200UL;ev_i[events]=p->size|0x10000|(p->scenario==2?0x100:0);ev_v[events]=p->scenario==2?value:0;++events;
   if(p->scenario==1){ev_a[events]=0x00fed200UL;ev_i[events]=p->size;ev_v[events]=old;++events;++reads;}
   if(p->kind!=0){ev_a[events]=0x00fed200UL;ev_i[events]=p->size|0x100;ev_v[events]=value;++events;++writes;for(j=0;j<p->size;++j)device[j]=(unsigned char)(value>>(8*(p->size-1-j)));}
   ctl[9]=p->kind==2&&p->scenario==2?1:0;
  }else{
   if(p->kind==1||p->kind==2)for(j=0;j<p->size;++j)ram[128+j]=(unsigned char)(value>>(8*(p->size-1-j)));
   if(fetches){ctl[12]=(unsigned long)p->pc+(p->scenario==4?2:0);ctl[13]=1;}
  }
  before=query(w,p->pc);next_before=query(w,p->next);p->fn();after=query(w,p->pc);next_after=query(w,p->next);
  expected_pc=t==0x8000?(unsigned long)p->next:(unsigned long)&movemo_capture;expected_trace=t==0x8000?(unsigned long)p->pc:(unsigned long)p->branch;
#ifdef TRACE_BAD_PC
  expected_pc+=2;
#endif
  if(trace_count!=1||trace_fault_count!=faults||frame(trace_frame,0,2)!=(0x2700|t|flags)||frame(trace_frame,2,4)!=expected_pc||frame(trace_frame,6,2)!=0x2024||frame(trace_frame,8,4)!=expected_trace){error=2;goto done;}
#ifdef TRACE_BAD_ORDER
  ++faults;
#endif
  if(trace_faults_seen!=faults){error=3;goto done;}
  if(after!=before||trace_native[0]!=before||trace_native[1]!=next_before||next_after>next_before+1||(t==0x4000&&next_after!=next_before)){error=5;goto done;}
  for(i=0;i<16;++i)if(movemo_output[i]!=expected[i]||trace_regs[i]!=(i==15?expected[i]-12:expected[i])){error=1;goto done;}
  if(movemo_reply!=flags){error=1;goto done;}
  if(faults){
   fc=fetches?6:5;ct=p->scenario==2&&t==0x8000?0x2000:0;
   if(frame(trace_fault_frame,0,2)!=(0x2700|t|(p->scenario==2?flags:ccr))||frame(trace_fault_frame,2,4)!=(unsigned long)(p->scenario==2?p->next:p->pc)||frame(trace_fault_frame,6,2)!=0x7008||frame(trace_fault_frame,8,4)!=(ct?(unsigned long)p->pc:0)||frame(trace_fault_frame,12,2)!=(ct|(p->scenario==2?0:0x100)|fc|(fetches?(p->scenario==4?0:0x40):(p->size==1?0x20:p->size==2?0x40:0)))){error=2;goto done;}
   if(frame(trace_fault_frame,20,4)!=(fetches?(unsigned long)p->pc+(p->scenario==4?2:0):0x00fed200UL)||frame(trace_fault_frame,14,2)!=(p->scenario==2?(0x85|(p->size==1?0x20:p->size==2?0x40:0)):0)||(p->scenario==2&&(frame(trace_fault_frame,24,4)!=0x00fed200UL||frame(trace_fault_frame,28,4)!=value))){error=2;goto done;}
   for(i=0;i<16;++i)if(trace_fault_regs[i]!=fault[i]){error=1;goto done;}
   if(trace_fault_native[0]!=before||trace_fault_native[1]!=next_before){error=5;goto done;}
  }
#ifdef TRACE_BAD_DATA
  ram[0]^=1;
#endif
  for(i=0;i<512;++i)if(movemo_data[i]!=ram[i]){error=1;goto done;}
  for(i=0;i<16;++i){data_expected=0;for(j=0;j<4;++j)data_expected=(data_expected<<8)|device[i*4+j];if(debug[i]!=data_expected){error=1;goto done;}}
#ifdef TRACE_BAD_COUNT
  ++events;
#endif
  if(ctl[1]!=events||ctl[6]!=reads||ctl[7]!=writes||ctl[8]!=(p->scenario==1||p->scenario==2?1u:0u)||ctl[15]!=fetches){error=4;goto done;}
  for(event=0;event<events;++event){ctl[2]=event;if(ctl[3]!=ev_a[event]||ctl[4]!=ev_i[event]||ctl[5]!=ev_v[event]){error=4;goto done;}}
  if(fetches&&(ctl[14]!=2||ctl[16]!=(p->scenario==4?4u:2u)||ctl[17]!=6)){error=4;goto done;}
  ram_word=frame(trace_stack,384,4);if(ram_word){error=1;goto done;}
 }
done:
 *v2=old2;*v9=old9;hex("TRACE checks=",checks);hex("TRACE warmed=",warmed);
 if(error){hex("TRACE case=",c);hex("TRACE CCR=",ccr);hex("TRACE mode=",t);hex("TRACE index=",i);hex("TRACE event=",event);hex("TRACE error=",error);hex("TRACE actual-PC=",frame(trace_frame,2,4));hex("TRACE expected-PC=",expected_pc);hex("TRACE trace-instruction=",frame(trace_frame,8,4));hex("TRACE before=",before);hex("TRACE after=",after);hex("TRACE fault-count=",trace_fault_count);hex("TRACE SSW=",frame(trace_fault_frame,12,2));}
 nf_call(nf_id("NF_STDERR"),error?"TRACE ORACLE FAIL\r\n":"TRACE ORACLE PASS T1 T0 native-holdoff read write fetch CT WB3 realRTE exactframes allGPR allCCR exactbus\r\n");nf_call(nf_id("NF_EXIT"),error);return error;
}
int main(void){return(int)Supexec(run);}
