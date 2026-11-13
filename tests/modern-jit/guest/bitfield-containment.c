/* SPDX-License-Identifier: GPL-2.0-or-later
 * Existing defined BEFORE_TRANSFER fixture; no new device or bus semantics.
 */
#include <mint/osbind.h>
extern long nf_id(const char*),nf_call(long,...);
extern void bf_byte(void),bf_three(void),bf_five(void),bf_access(void);
extern unsigned char bf_byte_instruction,bf_three_instruction,bf_five_instruction;
unsigned long bf_saved_d0,bf_saved_a0,bf_faults;
unsigned char bf_frame[60];
__asm__(".text\n_nf_id:\n.word 0x7300\nrts\n_nf_call:\n.word 0x7301\nrts\n");
static void text(const char*s){nf_call(nf_id("NF_STDERR"),s);}
static void hex(const char*s,unsigned long v){char b[11];unsigned i;for(i=0;i<8;++i)b[i]="0123456789abcdef"[(v>>(28-4*i))&15];b[8]='\r';b[9]='\n';b[10]=0;text(s);text(b);}
static long run(void){
 volatile unsigned long*ctl=(volatile unsigned long*)0x00fed280UL,*debug=(volatile unsigned long*)0x00fed300UL,*v=(volatile unsigned long*)8;
 unsigned long old=*v;unsigned char expected[64];unsigned i,bit,offset=0,width=8,first=0,last=1,which;unsigned long error=0,events=0,reads=0,writes=0;
#ifdef BF_PREFIX
 first=1;last=2;
#endif
#ifdef BF_READ
 first=2;last=3;
#endif
#ifdef BF_CONTROLS
 first=0;last=3;
#endif
 *v=(unsigned long)bf_access;
 for(which=first;which<last;++which){
  offset=which?7:0;width=which==0?8:which==1?17:32;
  for(i=0;i<64;++i)expected[i]=(unsigned char)(i*19+0x80);expected[0]=0x12;expected[1]=0x34;expected[2]=0x56;expected[3]=0x78;
  for(i=0;i<width;++i){bit=offset+i;expected[bit/8]^=(unsigned char)(1u<<(7-bit%8));}
  ctl[0]=0;ctl[18]=0x12345678UL;bf_faults=0;
#ifndef BF_CONTROLS
  ctl[9]=which==1?3:1;
#endif
  text("BFCON prepared operation=");hex("",which);
  hex("BFCON target=",(unsigned long)(which==0?&bf_byte_instruction:which==1?&bf_three_instruction:&bf_five_instruction));
  if(which==0)bf_byte();else if(which==1)bf_three();else bf_five();
  events=ctl[1];reads=ctl[6];writes=ctl[7];
  hex("BFCON handler-count=",bf_faults);hex("BFCON events=",events);hex("BFCON reads=",reads);hex("BFCON writes=",writes);
#ifdef BF_NEGATIVE_DATA
  expected[0]^=1;
#endif
  for(i=0;i<16;++i){unsigned long value=0;unsigned j;for(j=0;j<4;++j)value=(value<<8)|expected[i*4+j];if(debug[i]!=value){error=2;goto done;}}
#ifdef BF_CONTROLS
  if(bf_faults||ctl[8]||events!=(which?4u:2u)||reads!=(which?2u:1u)||writes!=(which?2u:1u)){error=4;goto done;}
#else
  if(bf_faults!=1||ctl[8]!=1||events!=6||reads!=3||writes!=2){error=4;goto done;}
#endif
 }
done:
 *v=old;hex("BFCON error=",error);text(error?"BFCON ORACLE FAIL\r\n":"BFCON ORACLE PASS\r\n");nf_call(nf_id("NF_EXIT"),error);return error;
}
int main(void){return(int)Supexec(run);}
