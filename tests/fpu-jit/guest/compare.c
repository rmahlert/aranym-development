/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <mint/osbind.h>
#include <string.h>
extern long nf_id(const char *),nf_call(long,...);
extern void fpu_compare_probe(const void *,void *,unsigned long,unsigned long);
__asm__(".text\n_nf_id:\n.word 0x7300\nrts\n_nf_call:\n.word 0x7301\nrts\n");
static const unsigned long values[][6]={
 {0x43ff0000,0x80000000,0,0x3fff0000,0x80000000,0},
 {0x3fff0000,0x80000000,0,0x43ff0000,0x80000000,0},
 {0xc3ff0000,0x80000000,0,0x3fff0000,0x80000000,0},
 {0x3fff0000,0x80000000,0,0x3fff0000,0x80000000,0},
 {0x3fff0000,0x80000000,1,0x3fff0000,0x80000000,0},
 {0xbfff0000,0x80000000,0,0xc3ff0000,0x80000000,0}};
static const unsigned long conditions[]={0,0x08000000,0x08000000,0x04000000,0,0};
static unsigned long cold[9],hot[9];
static void hex(char *p,unsigned long x){int i;for(i=7;i>=0;--i){p[i]="0123456789abcdef"[x&15];x>>=4;}}
static long run(void){unsigned mode,pair,ccr,repeat,i;long fail=0;
 for(mode=0;mode<4;++mode)for(pair=0;pair<6;++pair)for(ccr=0;ccr<32;++ccr){
  unsigned long fpcr=mode==0?0:mode==1?0x10:mode==2?0x40:0x80;
  fpu_compare_probe(values[pair],cold,fpcr,ccr);
  for(repeat=0;repeat<64;++repeat){fpu_compare_probe(values[pair],hot,fpcr,ccr);if(memcmp(cold,hot,sizeof cold)){fail=1;break;}}
  if(hot[0]!=(conditions[pair]|0x005a00f8UL)||hot[2]!=ccr||memcmp(hot+3,values[pair],sizeof values[pair]))fail=2;
  {char line[128];strcpy(line,"FPU-COMP mode=00000000 case=00000000 words=");hex(line+14,fpcr);hex(line+28,pair*32+ccr);for(i=0;i<9;++i)hex(line+43+i*8,hot[i]);line[115]='\r';line[116]='\n';line[117]=0;nf_call(nf_id("NF_STDERR"),line);}
 }
 nf_call(nf_id("NF_STDERR"),fail?"FPU COMPARE FAIL\r\n":"FPU COMPARE PASS\r\n");nf_call(nf_id("NF_EXIT"),fail);return fail;
}
int main(void){return Supexec(run);}
