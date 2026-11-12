/* SPDX-License-Identifier: GPL-2.0-or-later
 * Actual guest WB3 software completion and RTE, with exact bus ledger.
 * CM restarts are explicitly guest-directed; prior MOVEM stores can repeat
 * AFTER RTE. They are not hidden host retries or a device-atomicity claim.
 */
#include <mint/osbind.h>
#include <string.h>
extern long nf_id(const char*),nf_call(long,...),movemo_jit_query(void);
extern unsigned char movemo_begin,movemo_end;extern void writeback_handler(void);
unsigned long movemo_input[16],movemo_output[16],movemo_real_sp,movemo_target_pointer,movemo_pointer;
unsigned long writeback_fault_regs[16],writeback_fault_count,writeback_target,writeback_next,writeback_saved_a0,writeback_native_fault,writeback_native_next_fault,writeback_bus_after,writeback_live_a2,writeback_live_pointer,writeback_case;
unsigned short movemo_ccr,movemo_reply,movemo_saved_sr,writeback_status_after,writeback_poison;
#ifdef WRITEBACK_SKIP
unsigned short writeback_skip_software=1;
#else
unsigned short writeback_skip_software;
#endif
#ifdef WRITEBACK_KEEP_VALID
unsigned short writeback_keep_valid=1;
#else
unsigned short writeback_keep_valid;
#endif
unsigned char movemo_data[512],writeback_stack[512],writeback_frame[60];
__asm__(".text\n_nf_id:\n.word 0x7300\nrts\n_nf_call:\n.word 0x7301\nrts\n_movemo_jit_query:\nmoveq #-1,%d0\n.word 0x713b\nrts\n");
static void hex(const char*s,unsigned long x){char b[11];unsigned i;for(i=0;i<8;++i)b[i]="0123456789abcdef"[(x>>(28-4*i))&15];b[8]='\r';b[9]='\n';b[10]=0;nf_call(nf_id("NF_STDERR"),s);nf_call(nf_id("NF_STDERR"),b);}
static unsigned long frame(unsigned offset,unsigned size){unsigned i;unsigned long v=0;for(i=0;i<size;++i)v=(v<<8)|writeback_frame[offset+i];return v;}
void writeback_repeat_fail(void){hex("WRITEBACK MULTIPLE FAULTS case=",writeback_case);hex("WRITEBACK frame-PC=",frame(2,4));hex("WRITEBACK WB3S=",frame(14,2));nf_call(nf_id("NF_EXIT"),7);}
struct WriteCase{unsigned kind,size,mask,indirect,fault_index;int update;void(*fn)(void);unsigned char*pc,*next;};
#include "writeback-cases.h"
static unsigned long mask_for(unsigned size){return size==1?255UL:size==2?65535UL:0xffffffffUL;}
static unsigned long pattern(unsigned size,unsigned ccr){const unsigned long mask=mask_for(size),sign=(mask>>1)+1;return (ccr&3)==0?0:(ccr&3)==1?sign-1:(ccr&3)==2?mask:sign;}
static void init(const struct WriteCase*p,unsigned ccr,int cold){unsigned i,j;unsigned long mask=mask_for(p->size),old=pattern(p->size,ccr);
 for(i=0;i<16;++i)movemo_input[i]=0x89ab8000UL+i*0x103UL+(ccr&3)*0x4000UL;
 for(i=0;i<512;++i)movemo_data[i]=(unsigned char)(i*37+0x80);
 if(p->kind!=3){movemo_input[0]=(0x89abcdefUL&~mask)|old;for(j=0;j<p->size;++j)movemo_data[128+j]=(unsigned char)(old>>(8*(p->size-1-j)));}
 movemo_input[15]=(unsigned long)&writeback_stack[384];writeback_live_pointer=(unsigned long)&movemo_data[128];writeback_live_a2=(unsigned long)&movemo_pointer;
 movemo_pointer=cold&&p->indirect?0x89ab891bUL:writeback_live_pointer;
 movemo_input[10]=p->indirect?(cold?0x00fed240UL:writeback_live_a2):p->kind==3&&p->update<0?(cold?0x00fed240UL:(unsigned long)&movemo_data[192]):(cold?0x00fed200UL:(unsigned long)&movemo_data[128])+(p->update<0?p->size:0);
 movemo_ccr=ccr;
}
static unsigned long query(volatile unsigned long*w,unsigned char*pc){w[2]=(unsigned long)pc;return w[2];}
static long run(void){
 volatile unsigned long*w=(volatile unsigned long*)0x00fec000UL,*ctl=(volatile unsigned long*)0x00fed280UL,*debug=(volatile unsigned long*)0x00fed300UL,*vector=(volatile unsigned long*)8;
 unsigned c=0,ccr=0,i=0,j,k,n,retry,checks=0,warmed=0,event=0,events,reads,writes,registers[16],offsets[16],flags=0;
 unsigned long old_vector=*vector,mask=0,sign=0,old=0,value=0,expected_wb=0,values[16],resumed[16],fault[16],output[16],before=0,after=0,next_before=0,next_after=0,nf_before=0,configured=0,address=0,info=0;
 unsigned long event_address[64],event_value[64],event_info[64];unsigned char device[64],ram[512];long error=0,require_native=0;const struct WriteCase*p=0;char args[32];long bootstrap=nf_id("BOOTSTRAP");args[0]=0;
 if(bootstrap>0)nf_call(bootstrap+2,args,(long)sizeof(args));require_native=!strcmp(args,"native");if(require_native&&!(movemo_jit_query()&1)){error=5;goto done;}
 *vector=(unsigned long)writeback_handler;w[0]=(unsigned long)&movemo_begin;w[1]=(unsigned long)&movemo_end;
 for(c=0;c<sizeof(cases)/sizeof(cases[0]);++c)for(ccr=0;ccr<32;++ccr){
  p=&cases[c];++checks;writeback_case=c;writeback_target=(unsigned long)p->pc;writeback_next=(unsigned long)p->next;writeback_poison=p->indirect;ctl[0]=0;
  if(require_native){
   for(retry=0;retry<64;++retry){init(p,ccr,0);before=query(w,p->pc);nf_before=w[3];next_before=query(w,p->next);p->fn();after=query(w,p->pc);next_after=query(w,p->next);if(after==before+1&&next_after==next_before+1)break;if(after>before+1||next_after>next_before+1){error=5;goto done;}}
   if(retry==64){error=5;goto done;}++warmed;
  }
  init(p,ccr,1);writeback_fault_count=0;for(i=0;i<16;++i)fault[i]=output[i]=movemo_input[i];fault[15]-=60;for(i=0;i<512;++i)ram[i]=movemo_data[i];for(i=0;i<64;++i)device[i]=(unsigned char)(i*19+0x80);
  mask=mask_for(p->size);sign=(mask>>1)+1;old=pattern(p->size,ccr);events=reads=writes=0;ctl[0]=0;ctl[11]=0x00fed200UL;
  if(p->kind!=3){
   configured=(p->kind==0?mask:old)<<(8*(4-p->size));ctl[18]=configured;for(j=0;j<4;++j)device[j]=(unsigned char)(configured>>(8*(3-j)));
   value=p->kind==0?movemo_input[0]&mask:p->kind==1?(old+1)&mask:old|1;
   flags=p->kind==1?(value==0?4:0)|(value&sign?8:0)|(old==sign-1?2:0)|(old==mask?17:0):(ccr&16)|(value==0?4:0)|(value&sign?8:0);
   fault[10]=output[10]=movemo_input[10]+p->update*p->size;
   if(p->kind){event_address[events]=0x00fed200UL;event_info[events]=p->size;event_value[events]=old;++events;++reads;}
   event_address[events]=0x00fed200UL;event_info[events]=p->size|0x10100;event_value[events]=value;++events;
   event_address[events]=0x00fed200UL;event_info[events]=p->size|0x100;event_value[events]=value;++events;++writes;
   for(j=0;j<p->size;++j)device[j]=(unsigned char)(value>>(8*(p->size-1-j)));expected_wb=value;n=1;
   ctl[9]=p->kind?1:0;
  }else{
   flags=ccr;n=0;
   if(p->update<0){for(k=16;k>0;--k)if(p->mask&(1u<<(k-1))){registers[n]=k-1;offsets[n]=64-(n+1)*p->size;++n;}}
   else for(k=0;k<16;++k)if(p->mask&(1u<<k)){registers[n]=k;offsets[n]=n*p->size;++n;}
   for(k=0;k<n;++k){value=movemo_input[registers[k]];if(p->update<0&&registers[k]==10)value-=p->size;values[k]=value&mask;resumed[k]=p->indirect&&registers[k]==10?writeback_live_a2&mask:values[k];}
   ctl[9]=p->fault_index+p->indirect;
   if(p->indirect){event_address[events]=0x00fed240UL;event_info[events]=4;event_value[events]=0x00fed200UL;++events;++reads;output[10]=writeback_live_a2;}
   for(k=0;k<=p->fault_index;++k){event_address[events]=0x00fed200UL+offsets[k];event_info[events]=p->size|0x100|(k==p->fault_index?0x10000:0);event_value[events]=values[k];++events;if(k<p->fault_index)++writes;}
   expected_wb=values[p->fault_index];event_address[events]=0x00fed200UL+offsets[p->fault_index];event_info[events]=p->size|0x100;event_value[events]=expected_wb;++events;++writes;
   for(k=0;k<n;++k){event_address[events]=0x00fed200UL+offsets[k];event_info[events]=p->size|0x100;event_value[events]=resumed[k];++events;++writes;for(j=0;j<p->size;++j)device[offsets[k]+j]=(unsigned char)(resumed[k]>>(8*(p->size-1-j)));}
   if(p->update<0)output[10]=0x00fed240UL-n*p->size;
  }
  before=query(w,p->pc);next_before=query(w,p->next);p->fn();after=query(w,p->pc);next_after=query(w,p->next);
  if(writeback_fault_count!=1||writeback_status_after||frame(0,2)!=(0x2700|flags)||frame(2,4)!=(unsigned long)(p->kind==3?p->pc:p->next)||frame(6,2)!=0x7008||frame(8,4)!=(p->kind==3?(p->update<0?0x00fed240UL:0x00fed200UL):0)||frame(12,2)!=((p->kind==3?0x1000:0)|5|(p->size==1?0x20:p->size==2?0x40:0))){error=2;goto done;}
#ifdef WRITEBACK_BAD_VALUE
  expected_wb^=1;
#endif
  address=0x00fed200UL+(p->kind==3?offsets[p->fault_index]:0);
  if(frame(14,2)!=(0x85|(p->size==1?0x20:p->size==2?0x40:0))||frame(20,4)!=address||frame(24,4)!=address||frame(28,4)!=expected_wb){error=2;goto done;}
  if(after!=before||writeback_native_fault!=before||writeback_native_next_fault!=next_before||next_after>next_before+1||(p->kind!=3&&next_after!=next_before)){error=5;goto done;}
  for(i=0;i<16;++i)if(writeback_fault_regs[i]!=fault[i]||movemo_output[i]!=output[i]){error=1;goto done;}
  if(movemo_reply!=flags||movemo_pointer!=writeback_live_pointer){error=1;goto done;}
  for(i=0;i<512;++i)if(movemo_data[i]!=ram[i]){error=1;goto done;}
  for(i=0;i<16;++i){value=0;for(j=0;j<4;++j)value=(value<<8)|device[i*4+j];if(debug[i]!=value){error=1;goto done;}}
  if(writeback_bus_after!=(p->kind==3?p->fault_index+p->indirect+2:p->kind?3:2)){error=4;goto done;}
#ifdef WRITEBACK_BAD_COUNT
  ++events;
#endif
  if(ctl[1]!=events||ctl[6]!=reads||ctl[7]!=writes||ctl[8]!=1){error=4;goto done;}
  for(event=0;event<events;++event){ctl[2]=event;address=event_address[event];info=event_info[event];value=event_value[event];if(ctl[3]!=address||ctl[4]!=info||ctl[5]!=value){error=4;goto done;}}
 }
done:
 *vector=old_vector;hex("WRITEBACK checks=",checks);hex("WRITEBACK warmed=",warmed);
 if(error){hex("WRITEBACK case=",c);hex("WRITEBACK CCR=",ccr);hex("WRITEBACK index=",i);hex("WRITEBACK event=",event);hex("WRITEBACK error=",error);hex("WRITEBACK frame-PC=",frame(2,4));hex("WRITEBACK SSW=",frame(12,2));hex("WRITEBACK WB3=",frame(28,4));hex("WRITEBACK before=",before);hex("WRITEBACK after=",after);hex("WRITEBACK next-before=",next_before);hex("WRITEBACK next-after=",next_after);hex("WRITEBACK NF-before=",nf_before);}
 nf_call(nf_id("NF_STDERR"),error?"WRITEBACK ORACLE FAIL\r\n":"WRITEBACK ORACLE PASS softwareWB3 BWL RTE CM-write guest-directed-repeat exactbus allGPR allCCR native-holdoff\r\n");nf_call(nf_id("NF_EXIT"),error);return error;
}
int main(void){return (int)Supexec(run);}
