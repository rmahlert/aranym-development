/* SPDX-License-Identifier: GPL-2.0-or-later
 * T1 trace of a previously hot native JIT block; ordinary guest vector9.
 */
#include <mint/osbind.h>
extern long nf_id(const char *);
extern long nf_call(long,...);
extern void jit_trace_cycle(void),jit_trace_handler(void);
extern unsigned short jit_trace_mode,jit_trace_sr,jit_trace_format;
extern unsigned long jit_trace_count,jit_trace_pc,jit_trace_instruction;
extern unsigned long jit_trace_before,jit_trace_after,jit_trace_frame;
extern unsigned char jit_trace_nop,jit_trace_next;
__asm__(".text\n_nf_id:\n.word 0x7300\nrts\n_nf_call:\n.word 0x7301\nrts\n");
#ifndef TRACE_PC_DELTA
#define TRACE_PC_DELTA 0
#endif
static long run(void) {
    int i;long result=0;void (*old)(void);
    old=(void (*)(void))Setexc(9,jit_trace_handler);
    jit_trace_mode=0x2715;
    for(i=0;i<512;++i)jit_trace_cycle();
    if(jit_trace_count)result=1;
    jit_trace_mode=0xa715;jit_trace_cycle();
    (void)Setexc(9,old);
    if(!result&&(jit_trace_count!=1||jit_trace_pc!=(unsigned long)&jit_trace_next+TRACE_PC_DELTA||
        jit_trace_instruction!=(unsigned long)&jit_trace_nop))result=2;
    if(!result&&(jit_trace_sr!=0xa715||jit_trace_format!=0x2024||
        jit_trace_before!=jit_trace_after||jit_trace_frame+12!=jit_trace_before))result=3;
    nf_call(nf_id("NF_STDERR"),result?"JIT TRACE GUEST FAIL\r\n":"JIT TRACE GUEST PASS hotblock/T1/savedPC/format2/SR/A7/RTE\r\n");
    nf_call(nf_id("NF_EXIT"),result);return result;
}
int main(void){return (int)Supexec(run);}
