/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <mint/osbind.h>
#include <string.h>
extern long nf_id(const char *),nf_call(long,...);
extern long fpu_status_check(void),fpu_transition_check(void),fpu_state_check(void),fpu_trap_check(void);
extern void fpu_trap_handler(void);
extern unsigned long fpu_trap_count;
extern void fpu_rom_probe(unsigned long,unsigned long *);
extern void fpu_probe(unsigned long,const unsigned long *,unsigned long *);
__asm__(".text\n_nf_id:\n.word 0x7300\nrts\n_nf_call:\n.word 0x7301\nrts\n");
#define WORDS (21*5+2+24+3)
static unsigned long output[WORDS],reference[WORDS],inputs[24];
static const unsigned long normal[24]={
 0x40010000,0xc0000000,0, 0x40000000,0x80000000,0,
 0x40000000,0xc0000000,0, 0x40010000,0x80000000,0,
 0x40010000,0xa0000000,0, 0x40020000,0x90000000,0,
 0xc0010000,0xe0000000,0, 0x40020000,0x80000000,0};
static const unsigned long special[][3]={
 {0x3ffd0000,0xaaaaaaaa,0xaaaaaaab}, {0,0,0}, {0x80000000,0,0},
 {0x7fff0000,0x80000000,0}, {0xffff0000,0x80000000,0},
 {0x7fff0000,0xc0123456,0x789abcde}, {0x7fff0000,0x80123456,0x789abcde},
 {0,0,1}, {0x7ffe0000,0xffffffff,0xffffffff},
 {0x00010000,0x80000000,0}, {0x3fff0000,0xffffffff,0xffffffff}};
static void hex(char *s,unsigned long value){static const char d[]="0123456789abcdef";int i;for(i=7;i>=0;--i){s[i]=d[value&15];value>>=4;}}
static void data(unsigned long mode,unsigned long index){
 char line[48+WORDS*8];unsigned long i;
 strcpy(line,"FPU-DATA mode=00000000 case=00000000 words=");
 hex(line+14,mode);hex(line+28,index);
 for(i=0;i<WORDS;++i)hex(line+43+i*8,output[i]);
 line[43+WORDS*8]='\r';line[44+WORDS*8]='\n';line[45+WORDS*8]=0;
 nf_call(nf_id("NF_STDERR"),line);
}
static void report(unsigned long mode,unsigned long index,unsigned long hash,unsigned long difference){
 char line[]="FPU-RESULT mode=00000000 case=00000000 hash=00000000 difference=00000000\r\n";
 hex(line+16,mode);hex(line+30,index);hex(line+44,hash);hex(line+64,difference);
 nf_call(nf_id("NF_STDERR"),line);
}
static long run(void){
 unsigned long mode,index,repeat,i,hash,difference;long result=0;
 for(mode=0;mode<16;++mode)for(index=0;index<12;++index){
  memcpy(inputs,normal,sizeof inputs);
  if(index)memcpy(inputs,special[index-1],12);
  memset(reference,0,sizeof reference);fpu_probe(mode<<4,inputs,reference);
  for(repeat=0;repeat<64;++repeat){
   memset(output,0,sizeof output);fpu_probe(mode<<4,inputs,output);
   if(memcmp(output,reference,sizeof output))break;
  }
  hash=2166136261UL;difference=0;
  for(i=0;i<WORDS;++i){hash=(hash^output[i])*16777619UL;if(output[i]!=reference[i]&&!difference)difference=i+1;}
  report(mode<<4,index,hash,difference);data(mode<<4,index);
  if(difference)result=1;
 }
 for(mode=0;mode<16;++mode){
  memset(output,0,sizeof output);fpu_rom_probe(mode<<4,output);
  hash=2166136261UL;for(i=0;i<WORDS;++i)hash=(hash^output[i])*16777619UL;
  report(mode<<4,0x10000,hash,0);data(mode<<4,0x10000);
 }
 for(repeat=0;repeat<128;++repeat)if(fpu_transition_check())result=2;
 if(fpu_state_check())result=3;
 if(fpu_status_check())result=5;
 nf_call(nf_id("NF_STDERR"),result?"FPU JIT GUEST FAIL\r\n":"FPU JIT GUEST PASS\r\n");
 nf_call(nf_id("NF_EXIT"),result);return result;
}
int main(void){return (int)Supexec(run);}
