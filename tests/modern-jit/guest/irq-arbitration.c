/* SPDX-License-Identifier: GPL-2.0-or-later
 * Observe existing VBL/MFP priority, saved context and genuine handler entry.
 * No IRQ is synthesized by a host fixture. The optional existing PC counter
 * observes native execution only.
 */
#include <mint/osbind.h>
extern long nf_id(const char *),nf_call(long,...),iarb_jit(void);
extern void iarb_probe(void);
extern unsigned char iarb_begin,iarb_end,iarb_next,iarb_vbl;
#ifndef IARB_SOURCES
#define IARB_SOURCES 3
#endif
unsigned long iarb_sources=IARB_SOURCES,iarb_count,iarb_marker,iarb_timeout;
unsigned long iarb_source[4],iarb_pc[4],iarb_sr[4],iarb_sp[4],iarb_at_marker[4];
unsigned long iarb_base_sp,iarb_warm_before,iarb_warm_after,iarb_before,iarb_after;
unsigned long iarb_ticks,iarb_old_vbl,iarb_old_mfp,iarb_mfp_vector;
unsigned short iarb_release_sr;
unsigned char iarb_old_imra,iarb_old_imrb,iarb_old_iera,iarb_old_ierb;
unsigned char iarb_old_tcdcr,iarb_old_tcdata,iarb_pending;
__asm__(".text\n_nf_id:\n.word 0x7300\nrts\n_nf_call:\n.word 0x7301\nrts\n_iarb_jit:\nmoveq #-1,%d0\n.word 0x713b\nrts\n");
static void hex(const char *key,unsigned long value){
 char out[11];unsigned i;for(i=0;i<8;++i)out[i]="0123456789abcdef"[(value>>(28-4*i))&15];
 out[8]='\r';out[9]='\n';out[10]=0;nf_call(nf_id("NF_STDERR"),key);nf_call(nf_id("NF_STDERR"),out);
}
static long run(void){
 volatile unsigned long *w=(volatile unsigned long *)0x00fec000UL;
 const unsigned long jit=iarb_jit()&1,expected_count=(IARB_SOURCES==3?2:1);
 unsigned long error=0;unsigned i;
 w[0]=(unsigned long)&iarb_begin;w[1]=(unsigned long)&iarb_end;w[2]=(unsigned long)&iarb_next;
 iarb_probe();
 if(iarb_timeout||iarb_ticks!=6||!(iarb_pending&0x20)||iarb_marker!=1){error=7;goto done;}
 if(iarb_count!=expected_count
#ifdef IARB_BAD_COUNT
    +1
#endif
 ){error=5;goto done;}
 for(i=0;i<expected_count;++i){
  unsigned long source=(IARB_SOURCES&2)&&i==0?6:4;
  unsigned long pc=(unsigned long)&iarb_next,sr=iarb_release_sr,sp=iarb_base_sp-8;
#ifdef IARB_BEFORE_ONLY
  /* The before-only audit requires the observed extra VBL frame. It is
   * deliberately different from the architectural success oracle below. */
  if(IARB_SOURCES==3&&i==0){pc=(unsigned long)&iarb_vbl;sr=0x2400;sp-=8;}
#endif
#ifdef IARB_BAD_PC
  pc+=2;
#endif
#ifdef IARB_BAD_SP
  sp+=4;
#endif
  if(iarb_source[i]!=source||iarb_pc[i]!=pc||iarb_sr[i]!=sr){error=3;goto done;}
  if(iarb_sp[i]!=sp||iarb_at_marker[i]){error=4;goto done;}
 }
 if(jit&&(iarb_warm_after<=iarb_warm_before||iarb_after!=iarb_before+1)){error=6;goto done;}
 if(!jit&&(iarb_warm_after!=iarb_warm_before||iarb_after!=iarb_before)){error=6;goto done;}
#ifdef IARB_BAD_NATIVE
 if(jit&&iarb_after>iarb_before){error=6;goto done;}
#endif
done:
 hex("IARB jit=",jit);hex("IARB sources=",iarb_sources);hex("IARB error=",error);
 hex("IARB count=",iarb_count);hex("IARB ticks=",iarb_ticks);hex("IARB timeout=",iarb_timeout);
 hex("IARB pending=",iarb_pending);hex("IARB marker=",iarb_marker);
 hex("IARB resume=",(unsigned long)&iarb_next);hex("IARB VBL-entry=",(unsigned long)&iarb_vbl);
 hex("IARB base-SP=",iarb_base_sp);hex("IARB release-SR=",iarb_release_sr);
 hex("IARB warm-before=",iarb_warm_before);hex("IARB warm-after=",iarb_warm_after);
 hex("IARB before=",iarb_before);hex("IARB after=",iarb_after);
 for(i=0;i<iarb_count&&i<4;++i){
  hex(i?"IARB source1=":"IARB source0=",iarb_source[i]);
  hex(i?"IARB PC1=":"IARB PC0=",iarb_pc[i]);hex(i?"IARB SR1=":"IARB SR0=",iarb_sr[i]);
  hex(i?"IARB SP1=":"IARB SP0=",iarb_sp[i]);hex(i?"IARB marker1=":"IARB marker0=",iarb_at_marker[i]);
 }
 nf_call(nf_id("NF_STDERR"),error?"IARB ORACLE FAIL\r\n":
#ifdef IARB_BEFORE_ONLY
  "IARB BEFORE-ONLY audit observed extra VBL frame before MFP acknowledgement\r\n"
#else
  "IARB ORACLE PASS real VBL/MFP pending priority context native witness\r\n"
#endif
 );
 nf_call(nf_id("NF_EXIT"),error);return error;
}
int main(void){return(int)Supexec(run);}
