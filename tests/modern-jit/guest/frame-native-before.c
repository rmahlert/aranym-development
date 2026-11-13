/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <mint/osbind.h>
extern long nf_id(const char*),nf_call(long,...),nfb_jit(void);
extern void nfb_store(void*,unsigned long),nfb_trigger(void);
extern void nfb_timer(void);extern unsigned long nfb_vbr(void);
extern unsigned char nfb_begin,nfb_end,nfb_patch_target,nfb_next_target;
unsigned long nfb_old_vector,nfb_marker,nfb_warm[16],nfb_faults;
unsigned long nfb_pc_before,nfb_pc_after,nfb_patch_before,nfb_patch_after;
unsigned long nfb_next_before,nfb_next_after,nfb_next_warm_before,nfb_next_warm_after;
unsigned short nfb_format,nfb_ssw;
unsigned long nfb_old_timer,nfb_irq_count,nfb_irq_pc,nfb_irq_marker,nfb_timeout;
unsigned short nfb_irq_sr;unsigned char nfb_pending;
__asm__(".text\n_nf_id:\n.word 0x7300\nrts\n_nf_call:\n.word 0x7301\nrts\n_nfb_jit:\nmoveq #-1,%d0\n.word 0x713b\nrts\n");
static void hex(const char*s,unsigned long v){char b[11];unsigned i;for(i=0;i<8;++i)b[i]="0123456789abcdef"[(v>>(28-4*i))&15];b[8]='\r';b[9]='\n';b[10]=0;nf_call(nf_id("NF_STDERR"),s);nf_call(nf_id("NF_STDERR"),b);}
static long run(void){
 volatile unsigned long*w=(volatile unsigned long*)0x00fec000UL,*ctl=(volatile unsigned long*)0x00fed280UL;
 volatile unsigned char*imra=(volatile unsigned char*)0xfffffa13UL,*imrb=(volatile unsigned char*)0xfffffa15UL,*tcdcr=(volatile unsigned char*)0xfffffa1dUL,*vr=(volatile unsigned char*)0xfffffa17UL;
 volatile unsigned long*timer=(volatile unsigned long*)(nfb_vbr()+(((*vr&0xf0)|5)*4));
 unsigned char old_imra=*imra,old_imrb=*imrb,old_tcdcr=*tcdcr;
 unsigned i;long error=0,jit=nfb_jit()&1;unsigned long warm_before,warm_after;
 w[0]=(unsigned long)&nfb_begin;w[1]=(unsigned long)&nfb_end;w[2]=(unsigned long)&nfb_patch_target;warm_before=w[2];
 for(i=0;i<128;++i)nfb_store(nfb_warm,0x12345678UL);
 warm_after=w[2];nfb_old_timer=*timer;*imra=*imrb=0;*timer=(unsigned long)nfb_timer;nfb_trigger();
 *imra=*imrb=0;*timer=nfb_old_timer;*tcdcr=old_tcdcr;*imra=old_imra;*imrb=old_imrb;
 if(nfb_faults!=1||nfb_format!=0x7008||nfb_ssw!=5||nfb_pc_before!=(unsigned long)&nfb_next_target||nfb_pc_after!=nfb_pc_before||nfb_marker!=0x12345678UL||ctl[1]!=2||ctl[6]!=0||ctl[7]!=1||ctl[8]!=1){error=2;goto done;}
 if(jit&&(warm_after<=warm_before||nfb_next_warm_after<=nfb_next_warm_before||nfb_patch_after!=nfb_patch_before+2)){error=5;goto done;}
 if(!jit&&(warm_after!=warm_before||nfb_patch_after!=nfb_patch_before||nfb_next_warm_after!=nfb_next_warm_before)){error=5;goto done;}
 if(nfb_timeout||!(nfb_pending&0x20)||nfb_irq_count!=1||(nfb_irq_sr&0xff00)!=0x2500){error=2;goto done;}
#ifdef NFB_WRONG_STALE
 if(nfb_irq_pc!=(unsigned long)&nfb_next_target||nfb_irq_marker!=0){error=3;goto done;}
#else
 if(nfb_irq_pc!=(unsigned long)&nfb_next_target+2||nfb_irq_marker!=0x12345678UL){error=3;goto done;}
#endif
done:
 hex("NFB jit=",jit);hex("NFB error=",error);hex("NFB faults=",nfb_faults);hex("NFB patch-before=",nfb_patch_before);hex("NFB patch-after=",nfb_patch_after);hex("NFB next-before=",nfb_next_before);hex("NFB next-after=",nfb_next_after);hex("NFB PC-before=",nfb_pc_before);hex("NFB PC-after=",nfb_pc_after);hex("NFB next-warm-before=",nfb_next_warm_before);hex("NFB next-warm-after=",nfb_next_warm_after);hex("NFB IRQ-PC=",nfb_irq_pc);hex("NFB next-PC=",(unsigned long)&nfb_next_target);hex("NFB IRQ-marker=",nfb_irq_marker);hex("NFB pending=",nfb_pending);hex("NFB IRQ-count=",nfb_irq_count);
 nf_call(nf_id("NF_STDERR"),error?"NFB ORACLE FAIL\r\n":"NFB ORACLE PASS native header stores real MFP IRQ PC data-before-ack WB3 RTE\r\n");nf_call(nf_id("NF_EXIT"),error);return error;
}
int main(void){return(int)Supexec(run);}
