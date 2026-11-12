/* SPDX-License-Identifier: GPL-2.0-or-later
 * Actual TimerC pending/acknowledgement across physical fault/RTE/Trace9.
 */
#include <mint/osbind.h>
#include <string.h>
extern long nf_id(const char*),nf_call(long,...),movemo_jit_query(void);
extern unsigned long irqb_vbr(void);
extern void irqb_access(void),irqb_trace(void),irqb_timer(void),irqb_probe(void);
extern unsigned char movemo_begin,movemo_end,irqb_target,irqb_next;
unsigned long movemo_input[16],movemo_output[16],movemo_real_sp,movemo_target_pointer;
unsigned short movemo_ccr,movemo_reply,movemo_saved_sr,irqb_target_sr;
unsigned char movemo_data[512],irqb_stack[512];
unsigned long irqb_fault_regs[16],irqb_trace_regs[16],irqb_irq_regs[16],irqb_saved_d0,irqb_saved_a0;
unsigned long irqb_fault_count,irqb_irq_count,irqb_trace_count,irqb_trace_started,irqb_next_started,irqb_trace_at_irq,irqb_next_at_irq;
unsigned char irqb_fault_frame[60],irqb_trace_frame[12];
unsigned char irqb_pending_before,irqb_pending_at_irq;
unsigned short irqb_irq_sr,irqb_irq_format;
unsigned long irqb_irq_pc,irqb_old_timer;
unsigned long irqb_failure_reason;
__asm__(".text\n_nf_id:\n.word 0x7300\nrts\n_nf_call:\n.word 0x7301\nrts\n_movemo_jit_query:\nmoveq #-1,%d0\n.word 0x713b\nrts\n");
static void hex(const char*s,unsigned long x){char b[11];unsigned i;for(i=0;i<8;++i)b[i]="0123456789abcdef"[(x>>(28-4*i))&15];b[8]='\r';b[9]='\n';b[10]=0;nf_call(nf_id("NF_STDERR"),s);nf_call(nf_id("NF_STDERR"),b);}
void irqb_timeout(void){hex("IRQB failure-reason=",irqb_failure_reason);hex("IRQB IERB=",*(volatile unsigned char*)0xfffffa09UL);hex("IRQB IPRB=",*(volatile unsigned char*)0xfffffa0dUL);hex("IRQB IMRB=",*(volatile unsigned char*)0xfffffa15UL);hex("IRQB TCDCR=",*(volatile unsigned char*)0xfffffa1dUL);nf_call(nf_id("NF_STDERR"),"IRQB bounded timer/handler failure\r\n");nf_call(nf_id("NF_EXIT"),7);}
static unsigned long frame(const unsigned char*f,unsigned n,unsigned size){unsigned i;unsigned long x=0;for(i=0;i<size;++i)x=(x<<8)|f[n+i];return x;}
static unsigned long query(volatile unsigned long*w,unsigned char*pc){w[2]=(unsigned long)pc;return w[2];}
static void init(unsigned ccr,int cold){unsigned i;for(i=0;i<16;++i)movemo_input[i]=0x12340000UL+i*0x103UL;movemo_input[0]=0xfedcba98UL;movemo_input[1]=0x12345678UL;
 movemo_input[10]=cold?0x00fed200UL:(unsigned long)&movemo_data[128];movemo_input[15]=(unsigned long)&irqb_stack[384];movemo_ccr=ccr;irqb_target_sr=(unsigned short)(0x2700|ccr|(cold&&IRQB_TRACE?0x8000:0));
 irqb_fault_count=irqb_irq_count=irqb_trace_count=irqb_trace_started=irqb_next_started=irqb_trace_at_irq=irqb_next_at_irq=0;
}
static long run(void){
 volatile unsigned char*imra=(volatile unsigned char*)0xfffffa13UL,*imrb=(volatile unsigned char*)0xfffffa15UL,*tcdcr=(volatile unsigned char*)0xfffffa1dUL,*vr=(volatile unsigned char*)0xfffffa17UL;
 volatile unsigned long*w=(volatile unsigned long*)0x00fec000UL,*ctl=(volatile unsigned long*)0x00fed280UL;
 unsigned long vbr=irqb_vbr();volatile unsigned long*v2=(volatile unsigned long*)(vbr+8),*v9=(volatile unsigned long*)(vbr+36),*vt=(volatile unsigned long*)(vbr+(((*vr&0xf0)|5)*4));
 unsigned long old2=*v2,old9=*v9,expected[16],fault[16],expected_pc=0,before=0,after=0,next_before=0,next_after=0;
 unsigned ccr=0,i=0,retry,checks=0,warmed=0,handler_warmed=0,flags=0;unsigned long handler_before=0,handler_after=0;unsigned char saved_imra=*imra,saved_imrb=*imrb,saved_tcdcr=*tcdcr;long error=0,native=0;char args[32];long bootstrap=nf_id("BOOTSTRAP");args[0]=0;
 if(bootstrap>0)nf_call(bootstrap+2,args,(long)sizeof(args));native=!strcmp(args,"native");if(native&&!(movemo_jit_query()&1)){error=5;goto done;}
 irqb_old_timer=*vt;*v2=(unsigned long)irqb_access;*v9=(unsigned long)irqb_trace;*vt=(unsigned long)irqb_timer;
 *imra=*imrb=0;w[0]=(unsigned long)irqb_trace<(unsigned long)&movemo_begin?(unsigned long)irqb_trace:(unsigned long)&movemo_begin;w[1]=(unsigned long)&movemo_end;
 for(ccr=0;ccr<32;++ccr){
  ++checks;*imrb=0;*tcdcr=saved_tcdcr;ctl[0]=0;
  if(native){for(retry=0;retry<64;++retry){init(ccr,0);before=query(w,&irqb_target);next_before=query(w,&irqb_next);irqb_probe();after=query(w,&irqb_target);next_after=query(w,&irqb_next);if(after==before+1&&next_after==next_before+1)break;if(after>before+1||next_after>next_before+1){error=5;goto done;}}if(retry==64){error=5;goto done;}++warmed;}
  if(native&&IRQB_TRACE){
   /* A separate no-fault T1 run reaches the actual handler while MFP is
    * masked. Require its exact first MOVE to execute natively before cold CT. */
   for(retry=0;retry<128;++retry){init(ccr,0);irqb_target_sr=(unsigned short)(0xa700|ccr);handler_before=query(w,(unsigned char*)irqb_trace);irqb_probe();handler_after=query(w,(unsigned char*)irqb_trace);if(handler_after==handler_before+1)break;if(handler_after>handler_before+1){error=5;goto done;}}
   if(retry==128){error=5;goto done;}++handler_warmed;
  }
  init(ccr,1);for(i=0;i<16;++i)expected[i]=fault[i]=movemo_input[i];ctl[0]=0;ctl[18]=0x89abcdefUL;ctl[9]=0;
  flags=(ccr&16)|8;expected[10]+=4;if(!IRQB_WRITE)expected[1]=0x89abcdefUL;
  if(IRQB_WRITE)for(i=0;i<16;++i)fault[i]=expected[i];fault[15]-=60;
  before=query(w,&irqb_target);next_before=query(w,&irqb_next);irqb_probe();after=query(w,&irqb_target);next_after=query(w,&irqb_next);
  if(irqb_fault_count!=1||irqb_irq_count!=1||irqb_trace_count!=(IRQB_TRACE?1u:0u)){error=4;goto done;}
  if(!(irqb_pending_before&0x20)){error=4;goto done;}
  expected_pc=IRQB_TRACE?(unsigned long)irqb_trace:(unsigned long)&irqb_next;
#ifdef IRQB_BAD_PC
  expected_pc+=2;
#endif
  if(irqb_irq_pc!=expected_pc||irqb_irq_sr!=(0x2500|flags)||irqb_irq_format!=(((*vr&0xf0)|5)*4)||irqb_trace_at_irq||irqb_next_at_irq){error=2;goto done;}
  for(i=0;i<15;++i)if(irqb_irq_regs[i]!=expected[i]||irqb_fault_regs[i]!=fault[i]||movemo_output[i]!=expected[i]){error=1;goto done;}
  if(irqb_fault_regs[15]!=fault[15]||irqb_irq_regs[15]!=expected[15]-(IRQB_TRACE?20u:8u)||movemo_output[15]!=expected[15]){error=1;goto done;}
  if(frame(irqb_fault_frame,0,2)!=(0x2700|(IRQB_TRACE?0x8000:0)|(IRQB_WRITE?flags:ccr))||frame(irqb_fault_frame,2,4)!=(unsigned long)(IRQB_WRITE?&irqb_next:&irqb_target)||frame(irqb_fault_frame,6,2)!=0x7008||frame(irqb_fault_frame,12,2)!=(IRQB_WRITE?(IRQB_TRACE?0x2005:0x05):0x105)){error=3;goto done;}
  if(IRQB_TRACE&&(frame(irqb_trace_frame,0,2)!=(0xa500|flags)||frame(irqb_trace_frame,2,4)!=(unsigned long)&irqb_next||frame(irqb_trace_frame,6,2)!=0x2024||frame(irqb_trace_frame,8,4)!=(unsigned long)&irqb_target)){error=3;goto done;}
  if(after!=before||next_after>next_before+1){error=5;goto done;}
  if(ctl[1]!=2||ctl[8]!=1||ctl[6]!=(IRQB_WRITE?0u:1u)||ctl[7]!=(IRQB_WRITE?1u:0u)){error=4;goto done;}
 }
done:
 *imra=0;*imrb=0;*v2=old2;*v9=old9;if(irqb_old_timer)*vt=irqb_old_timer;*tcdcr=saved_tcdcr;*imra=saved_imra;*imrb=saved_imrb;
 hex("IRQB checks=",checks);hex("IRQB warmed=",warmed);hex("IRQB handler-warmed=",handler_warmed);hex("IRQB handler-before=",handler_before);hex("IRQB handler-after=",handler_after);hex("IRQB error=",error);hex("IRQB CCR=",ccr);hex("IRQB PC=",irqb_irq_pc);hex("IRQB expected-PC=",expected_pc);hex("IRQB saved-SR=",irqb_irq_sr);hex("IRQB trace-started=",irqb_trace_at_irq);hex("IRQB next-started=",irqb_next_at_irq);hex("IRQB pending-before=",irqb_pending_before);hex("IRQB pending-at-IRQ=",irqb_pending_at_irq);hex("IRQB IRQ-count=",irqb_irq_count);hex("IRQB trace-count=",irqb_trace_count);hex("IRQB before=",before);hex("IRQB after=",after);hex("IRQB fault-SSW=",frame(irqb_fault_frame,12,2));hex("IRQB fault-PC=",frame(irqb_fault_frame,2,4));
 nf_call(nf_id("NF_STDERR"),error?"IRQB ORACLE FAIL\r\n":"IRQB ORACLE PASS real-MFP-TimerC pending ack RTE trace-order allCCR GPR\r\n");nf_call(nf_id("NF_EXIT"),error);return error;
}
int main(void){return(int)Supexec(run);}
