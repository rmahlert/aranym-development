/* SPDX-License-Identifier: GPL-2.0-or-later
 * Real unmapped-port probe, explicit WB3 discard, actual RTE/Trace9.
 * MOVES executes in the genuine core; native witnesses apply separately to
 * ordinary MOVE controls and the warmed successor. No new virtual bus.
 */
#include <mint/osbind.h>
#include <string.h>
extern long nf_id(const char*),nf_call(long,...),movemo_jit_query(void);
extern unsigned char movemo_begin,movemo_end,movemo_capture;
extern void moves_save_fc(void),moves_restore_fc(void),moves_access(void),moves_trace(void);
unsigned long movemo_input[16],movemo_output[16],movemo_real_sp,movemo_target_pointer;
unsigned short movemo_ccr,movemo_reply,movemo_saved_sr,moves_sr;
unsigned char movemo_data[512],moves_stack[512],moves_fault_frame[60],moves_trace_frame[12];
unsigned long moves_saved_d0,moves_saved_sfc,moves_saved_dfc,moves_target,moves_next;
unsigned long moves_fault_regs[16],moves_trace_regs[16],moves_fault_count,moves_trace_count,moves_faults_at_trace,moves_fault_native[2],moves_trace_native[2];
__asm__(".text\n_nf_id:\n.word 0x7300\nrts\n_nf_call:\n.word 0x7301\nrts\n_movemo_jit_query:\nmoveq #-1,%d0\n.word 0x713b\nrts\n");
static void hex(const char*s,unsigned long x){char b[11];unsigned i;for(i=0;i<8;++i)b[i]="0123456789abcdef"[(x>>(28-4*i))&15];b[8]='\r';b[9]='\n';b[10]=0;nf_call(nf_id("NF_STDERR"),s);nf_call(nf_id("NF_STDERR"),b);}
void moves_repeat_fail(void){nf_call(nf_id("NF_STDERR"),"MST duplicate or misordered handler\r\n");nf_call(nf_id("NF_EXIT"),7);}
static unsigned long frame(const unsigned char*f,unsigned n,unsigned size){unsigned i;unsigned long x=0;for(i=0;i<size;++i)x=(x<<8)|f[n+i];return x;}
struct MovesCase{unsigned kind,size,fc,fault;void(*fn)(void);unsigned char*pc,*next,*branch;};
#include "moves-cases.h"
static unsigned long query(volatile unsigned long*w,unsigned char*pc){w[2]=(unsigned long)pc;return w[2];}
static unsigned long mask_for(unsigned size){return size==1?255UL:size==2?65535UL:0xffffffffUL;}
static void init(const struct MovesCase*p,unsigned ccr,unsigned t,int cold){unsigned i;
 for(i=0;i<16;++i)movemo_input[i]=0x12340000UL+i*0x103UL;
 movemo_input[0]=0xfedcba98UL;movemo_input[1]=0x12345678UL;
 for(i=0;i<512;++i)movemo_data[i]=(unsigned char)(i*37+0x80);
 for(i=0;i<4;++i)movemo_data[128+i]=(unsigned char)(0x89abcdefUL>>(8*(3-i)));
 movemo_input[10]=cold&&p->fault?0xfffffa42UL:(unsigned long)&movemo_data[128];movemo_input[15]=(unsigned long)&moves_stack[384];
 movemo_ccr=ccr;moves_sr=(unsigned short)(0x2700|ccr|(cold?t:0));moves_fault_count=moves_trace_count=moves_faults_at_trace=0;
}
static long run(void){
 volatile unsigned long*w=(volatile unsigned long*)0x00fec000UL,*v2=(volatile unsigned long*)8,*v9=(volatile unsigned long*)36;
 unsigned long old2=*v2,old9=*v9,expected[16],before=0,after=0,next_before=0,next_after=0,mask,value=0,flags=0,fc,ct,expected_pc=0,expected_trace=0,wb=0;
 unsigned c=0,ccr=0,t=0,i=0,j,retry,checks=0,warmed=0,fallback=0,normal_native=0,faults=0;unsigned char ram[512];long error=0,native=0;char args[32];const struct MovesCase*p=0;long bootstrap=nf_id("BOOTSTRAP");args[0]=0;
 moves_save_fc();if(bootstrap>0)nf_call(bootstrap+2,args,(long)sizeof(args));native=!strcmp(args,"native");if(native&&!(movemo_jit_query()&1)){error=5;goto done;}
 *v2=(unsigned long)moves_access;*v9=(unsigned long)moves_trace;w[0]=(unsigned long)&movemo_begin;w[1]=(unsigned long)&movemo_end;
 for(c=0;c<sizeof(cases)/sizeof(cases[0]);++c)for(t=0x4000;t<=0x8000;t+=0x4000)for(ccr=0;ccr<32;++ccr){
  p=&cases[c];
#ifdef MOVES_CONTROLS
  if(p->kind==0&&p->fault)continue;
#endif
  ++checks;moves_target=(unsigned long)p->pc;moves_next=(unsigned long)p->next;
  if(native){
   for(retry=0;retry<64;++retry){init(p,ccr,t,0);before=query(w,p->pc);next_before=query(w,p->next);p->fn();after=query(w,p->pc);next_after=query(w,p->next);
    if(moves_fault_count||moves_trace_count||movemo_output[10]!=movemo_input[10]+p->size){error=4;goto done;}
    if(after==before+(p->kind==2?1u:0u)&&next_after==next_before+1)break;
    if(after>before+1||next_after>next_before+1){error=5;goto done;}
   }
   if(retry==64){error=5;goto done;}
#ifdef MOVES_WRONG_NATIVE
   if(p->kind!=2&&after!=before+1){error=5;goto done;}
#endif
   ++warmed;if(p->kind==2)++normal_native;else ++fallback;
  }
  init(p,ccr,t,1);for(i=0;i<16;++i)expected[i]=movemo_input[i];for(i=0;i<512;++i)ram[i]=movemo_data[i];
  mask=mask_for(p->size);value=p->kind==1?0x89abcdefUL>>(8*(4-p->size)):movemo_input[0]&mask;flags=p->kind==2?(ccr&16)|8:ccr;expected[10]+=p->size;
  if(p->kind==1)expected[1]=(expected[1]&~mask)|value;
  else if(!p->fault)for(j=0;j<p->size;++j)ram[128+j]=(unsigned char)(value>>(8*(p->size-1-j)));
  faults=p->fault;before=query(w,p->pc);next_before=query(w,p->next);p->fn();after=query(w,p->pc);next_after=query(w,p->next);
  expected_pc=p->kind==2&&t==0x4000?(unsigned long)&movemo_capture:(unsigned long)p->next;expected_trace=p->kind==2&&t==0x4000?(unsigned long)p->branch:(unsigned long)p->pc;
#ifdef MOVES_BAD_PC
  expected_pc+=2;
#endif
  if(moves_trace_count!=1||frame(moves_trace_frame,0,2)!=(0x2700|t|flags)||frame(moves_trace_frame,2,4)!=expected_pc||frame(moves_trace_frame,6,2)!=0x2024||frame(moves_trace_frame,8,4)!=expected_trace){error=2;goto done;}
#ifdef MOVES_BAD_COUNT
  ++faults;
#endif
  if(moves_fault_count!=faults||moves_faults_at_trace!=faults){error=4;goto done;}
  if(after!=before||moves_trace_native[0]!=before||moves_trace_native[1]!=next_before||next_after>next_before+1||(p->kind==2&&t==0x4000&&next_after!=next_before)){error=5;goto done;}
  for(i=0;i<16;++i)if(movemo_output[i]!=expected[i]||moves_trace_regs[i]!=(i==15?expected[i]-12:expected[i])){error=1;goto done;}
  if(movemo_reply!=flags){error=1;goto done;}
  if(p->fault){
   fc=p->kind==2?5:p->fc==2?1:p->fc==6?5:p->fc;ct=p->kind==0||t==0x8000?0x2000:0;
#ifdef MOVES_BAD_FC
   fc^=4;
#endif
   wb=value;
#ifdef MOVES_BAD_WB
   wb^=1;
#endif
   if(frame(moves_fault_frame,0,2)!=(0x2700|t|flags)||frame(moves_fault_frame,2,4)!=(unsigned long)p->next||frame(moves_fault_frame,6,2)!=0x7008||frame(moves_fault_frame,8,4)!=(ct?(unsigned long)p->pc:0)||frame(moves_fault_frame,12,2)!=(ct|fc|(p->size==1?0x20:p->size==2?0x40:0))){error=3;goto done;}
   if(frame(moves_fault_frame,14,2)!=(0x80|fc|(p->size==1?0x20:p->size==2?0x40:0))||frame(moves_fault_frame,20,4)!=0xfffffa42UL||frame(moves_fault_frame,24,4)!=0xfffffa42UL||frame(moves_fault_frame,28,4)!=wb){error=3;goto done;}
   if(moves_fault_native[0]!=before||moves_fault_native[1]!=next_before){error=5;goto done;}
   for(i=0;i<16;++i)if(moves_fault_regs[i]!=(i==15?expected[i]-60:expected[i])){error=1;goto done;}
  }
#ifdef MOVES_BAD_RAM
  ram[0]^=1;
#endif
  for(i=0;i<512;++i)if(movemo_data[i]!=ram[i]){error=1;goto done;}
 }
done:
 *v2=old2;*v9=old9;moves_restore_fc();hex("MST checks=",checks);hex("MST warmed=",warmed);hex("MST fallback=",fallback);hex("MST normal-native=",normal_native);
 if(error){hex("MST case=",c);hex("MST CCR=",ccr);hex("MST mode=",t);hex("MST index=",i);hex("MST error=",error);hex("MST fault-SSW=",frame(moves_fault_frame,12,2));hex("MST trace-PC=",frame(moves_trace_frame,2,4));hex("MST trace-instruction=",frame(moves_trace_frame,8,4));hex("MST expected-PC=",expected_pc);hex("MST expected-instruction=",expected_trace);hex("MST before=",before);hex("MST after=",after);}
 nf_call(nf_id("NF_STDERR"),error?"MST ORACLE FAIL\r\n":"MST ORACLE PASS real-unmapped-port explicit-WB3-discard T0 T1 MOVES-fallback native-successor allCCR GPR FC RAM\r\n");nf_call(nf_id("NF_EXIT"),error);return error;
}
int main(void){return(int)Supexec(run);}
