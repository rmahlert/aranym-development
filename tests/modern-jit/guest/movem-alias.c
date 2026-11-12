/* SPDX-License-Identifier: GPL-2.0-or-later
 * PRM232: on 040 predecrement, a base register in the mask stores its INITIAL
 * value minus ONE operand size, independent of preceding selected registers.
 */
#include <mint/osbind.h>
#include <string.h>
extern long nf_id(const char*),nf_call(long,...),movema_jit_query(void);
extern unsigned char movema_begin,movema_end;
#define DECL(n) extern void movema_##n(void);extern unsigned char movema_target_##n
DECL(w2);DECL(l2);DECL(w3);DECL(l3);DECL(wa);DECL(la);
DECL(pw2);DECL(pl2);DECL(pw3);DECL(pl3);DECL(pwa);DECL(pla);
unsigned long movema_input[16],movema_output[16],movema_real_sp;
unsigned short movema_ccr,movema_reply,movema_saved_sr;
unsigned char movema_data[256];
__asm__(".text\n_nf_id:\n.word 0x7300\nrts\n_nf_call:\n.word 0x7301\nrts\n_movema_jit_query:\nmoveq #-1,%d0\n.word 0x713b\nrts\n");
static void hex(const char*s,unsigned long x){char b[11];unsigned i;for(i=0;i<8;++i)b[i]="0123456789abcdef"[(x>>(28-4*i))&15];b[8]='\r';b[9]='\n';b[10]=0;nf_call(nf_id("NF_STDERR"),s);nf_call(nf_id("NF_STDERR"),b);}
struct AliasCase{unsigned size,mask,load;void(*fn)(void);unsigned char*pc;};
static const struct AliasCase cases[]={
 {2,0x0c00,0,movema_w2,&movema_target_w2},{4,0x0c00,0,movema_l2,&movema_target_l2},
 {2,0x8c00,0,movema_w3,&movema_target_w3},{4,0x8c00,0,movema_l3,&movema_target_l3},
 {2,0xffff,0,movema_wa,&movema_target_wa},{4,0xffff,0,movema_la,&movema_target_la},
 {2,0x0c00,1,movema_pw2,&movema_target_pw2},{4,0x0c00,1,movema_pl2,&movema_target_pl2},
 {2,0x8c00,1,movema_pw3,&movema_target_pw3},{4,0x8c00,1,movema_pl3,&movema_target_pl3},
 {2,0xffff,1,movema_pwa,&movema_target_pwa},{4,0xffff,1,movema_pla,&movema_target_pla}};
static long run(void){
 volatile unsigned long*w=(volatile unsigned long*)0x00fec000UL;
 unsigned c=0,ccr=0,i=0,j,retry,offset,count=0,native=0,noflags=0;int reg;long error=0,require_native=0;
 unsigned long expected[16]={0},value=0,before=0,after=0,nf_before=0,nf_after=0;unsigned char bytes[256]={0};
 char args[32];long bootstrap=nf_id("BOOTSTRAP");const struct AliasCase*p=0;args[0]=0;
 if(bootstrap>0)nf_call(bootstrap+2,args,(long)sizeof(args));require_native=!strcmp(args,"native");
 if(require_native&&!(movema_jit_query()&1)){error=5;goto done;}
 w[0]=(unsigned long)&movema_begin;w[1]=(unsigned long)&movema_end;
 for(c=0;c<sizeof(cases)/sizeof(cases[0]);++c)for(ccr=0;ccr<32;++ccr){
  p=&cases[c];++count;
  for(retry=0;retry<64;++retry){
   for(i=0;i<16;++i)movema_input[i]=0x11118000UL+i;
   movema_input[10]=(unsigned long)&movema_data[64];movema_input[11]=0x12348000UL;movema_input[15]=(unsigned long)&movema_data[192];
   for(i=0;i<16;++i)expected[i]=movema_input[i];
   for(i=0;i<256;++i)movema_data[i]=bytes[i]=(unsigned char)(i^0xa5);
   offset=64;
   if(p->load){
    for(reg=0;reg<16;++reg)if(p->mask&(1u<<reg)){
     value=0;for(j=0;j<p->size;++j)value=(value<<8)|bytes[offset+j];
     if(p->size==2&&(value&0x8000))value|=0xffff0000UL;
     expected[reg]=value;offset+=p->size;
    }
   }else for(reg=15;reg>=0;--reg)if(p->mask&(1u<<reg)){
    offset-=p->size;value=movema_input[reg];
    if(reg==10){value-=p->size;
#ifdef MOVEMA_BAD_BASE
     value-=p->size;
#endif
    }
    for(j=0;j<p->size;++j)bytes[offset+j]=(unsigned char)(value>>(8*(p->size-1-j)));
   }
   expected[10]=(unsigned long)&movema_data[offset];
#ifdef MOVEMA_BAD_POST_BASE
   if(p->load)expected[10]=0;
#endif
   movema_ccr=ccr;w[2]=(unsigned long)p->pc;before=w[2];nf_before=w[3];p->fn();after=w[2];nf_after=w[3];
   for(i=0;i<16;++i)if(movema_output[i]!=expected[i]){error=2;goto done;}
   for(i=0;i<256;++i)if(movema_data[i]!=bytes[i]){error=1;goto done;}
   if(movema_reply!=ccr){error=3;goto done;}
   if(!require_native){if(after!=before||nf_after!=nf_before){error=5;goto done;}break;}
   if(after==before+1&&nf_after==nf_before+1){++native;++noflags;break;}
   if(after!=before||nf_after!=nf_before){error=5;goto done;}
  }
  if(retry==64){error=5;goto done;}
 }
done:
 hex("MOVEMA checks=",count);hex("MOVEMA native=",native);hex("MOVEMA noflags=",noflags);
 if(error){hex("MOVEMA case=",c);hex("MOVEMA size=",p?p->size:0);hex("MOVEMA mask=",p?p->mask:0);hex("MOVEMA CCR=",ccr);hex("MOVEMA index=",i);hex("MOVEMA error=",error);hex("MOVEMA expected=",error==1?bytes[i]:i<16?expected[i]:0);hex("MOVEMA actual=",error==1?movema_data[i]:i<16?movema_output[i]:0);hex("MOVEMA before=",before);hex("MOVEMA after=",after);hex("MOVEMA nf-before=",nf_before);hex("MOVEMA nf-after=",nf_after);}
 nf_call(nf_id("NF_STDERR"),error?"MOVEMA ORACLE FAIL\r\n":"MOVEMA ORACLE PASS 040predecbase-minus-one-size allGPR allCCR allRAM\r\n");nf_call(nf_id("NF_EXIT"),error);return error;
}
int main(void){return (int)Supexec(run);}
