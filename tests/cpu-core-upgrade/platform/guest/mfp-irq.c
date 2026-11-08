/* SPDX-License-Identifier: GPL-2.0-or-later
 * Ordinary Atari guest: existing MFP TimerC, no synthetic interrupt injection.
 */
#include <mint/osbind.h>
extern long nf_id(const char *);
extern long nf_call(long,...);
extern void mfp_irq_checks(void);
extern unsigned short irq_saved_sr,irq_format,irq_master_sr,irq_master_format,irq_after_sr;
extern unsigned long irq_pc,irq_frame_sp,irq_master_pc,irq_master_frame_sp;
extern unsigned long irq_after_sp,irq_expected_pc,irq_master_top,irq_vector_offset,irq_caller_sp;
__asm__(".text\n_nf_id:\n.word 0x7300\nrts\n_nf_call:\n.word 0x7301\nrts\n");
#ifndef IRQ_EXPECT_FORMAT
#define IRQ_EXPECT_FORMAT 0x1000
#endif
static void reporthex(const char *label,unsigned long value) {
    char text[11];int i;const char *hex="0123456789abcdef";
    for(i=0;i<8;++i)text[i]=hex[(value>>(28-4*i))&15];
    text[8]='\r';text[9]='\n';text[10]=0;
    nf_call(nf_id("NF_STDERR"),label);nf_call(nf_id("NF_STDERR"),text);
}
static long run(void) {
    long result=0;mfp_irq_checks();
    if(irq_format!=(IRQ_EXPECT_FORMAT|irq_vector_offset)||irq_master_format!=irq_vector_offset)result=1;
    else if(irq_saved_sr!=0x3515||irq_master_sr!=0x3515||irq_after_sr!=0x3515)result=2;
    else if(irq_pc!=irq_expected_pc||irq_master_pc!=irq_expected_pc)result=3;
    else if(irq_frame_sp!=irq_caller_sp-8||irq_master_frame_sp!=irq_master_top-8||irq_after_sp!=irq_master_top)result=4;
    reporthex("MFP IRQ active format=",irq_format);reporthex("MFP IRQ MSP format=",irq_master_format);
    reporthex("MFP IRQ saved SR=",irq_saved_sr);reporthex("MFP IRQ MSP saved SR=",irq_master_sr);
    reporthex("MFP IRQ returned SR=",irq_after_sr);
    reporthex("MFP IRQ saved PC=",irq_pc);reporthex("MFP IRQ expected PC=",irq_expected_pc);reporthex("MFP IRQ MSP saved PC=",irq_master_pc);
    reporthex("MFP IRQ vector offset=",irq_vector_offset);
    reporthex("MFP IRQ ISP frame=",irq_frame_sp);reporthex("MFP IRQ caller ISP=",irq_caller_sp);
    reporthex("MFP IRQ MSP frame=",irq_master_frame_sp);reporthex("MFP IRQ master top=",irq_master_top);
    reporthex("MFP IRQ returned A7=",irq_after_sp);
    nf_call(nf_id("NF_STDERR"),result?"MFP IRQ GUEST FAIL\r\n":"MFP IRQ GUEST PASS TimerC/customvector/STOP/ISP1/MSP0/RTE\r\n");
    nf_call(nf_id("NF_EXIT"),result);return result;
}
int main(void){return (int)Supexec(run);}
