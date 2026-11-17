/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <mint/osbind.h>
#include <stdio.h>
extern long nf_id(const char *),nf_call(long,...),probe(void);
extern char probe_access;
unsigned long saved_sp,old_vector,fault_pc,fault_d1,fault_address,fault_stack,probe_address;
unsigned long expected_registers[15],fault_registers[15];
unsigned short fault_format,fault_sr,fault_ssw;
__asm__(".text\n_nf_id:\n.word 0x7300\nrts\n_nf_call:\n.word 0x7301\nrts\n");
static long run(void){
 char b[360];unsigned i,phase;int failure=0;
 for(phase=0;phase<2;phase++){
  long r;unsigned changed=0;
  probe_address=phase?0x00fffa44UL:0xfffffa44UL;
  r=probe();expected_registers[1]=0x13579bdfUL;
  for(i=0;i<15;i++)if(fault_registers[i]!=expected_registers[i])changed|=1u<<i;
  {
  int bad=r||fault_pc!=(unsigned long)&probe_access||fault_d1!=0x13579bdfUL||
    fault_format!=0x7008||fault_sr!=0x2710||changed||fault_stack+60!=saved_sp-4;
  sprintf(b,"HARDWARE PROBE r=%ld pc=%08lx expected=%08lx d1=%08lx format=%04x sr=%04x ssw=%04x address=%08lx expectedaddress=%08lx registers=%04x stack=%08lx expectedstack=%08lx %s\r\n",r,fault_pc,(unsigned long)&probe_access,fault_d1,fault_format,fault_sr,fault_ssw,fault_address,probe_address,changed,fault_stack+60,saved_sp-4,bad?"FAIL":"PASS");
  nf_call(nf_id("NF_STDERR"),b);failure|=bad;
  }
 }
 nf_call(nf_id("NF_EXIT"),(long)failure);return failure;
}
int main(void){return Supexec(run);}
