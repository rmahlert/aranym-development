/* SPDX-License-Identifier: GPL-2.0-or-later
 * Independent wide-integer oracle; no emulator-source flag macros are used.
 * M68000PRM ADDX p117 / SUBX p287: X=C, sticky Z for extend operations.
 */
#include <mint/osbind.h>
extern long nf_id(const char *);
extern long nf_call(long,...);
__asm__(".text\n_nf_id:\n.word 0x7300\nrts\n_nf_call:\n.word 0x7301\nrts\n");
#define DECL(n) extern void alu_##n##_b(void),alu_##n##_w(void),alu_##n##_l(void)
DECL(addx);DECL(subx);DECL(add);DECL(sub);DECL(cmp);DECL(neg);DECL(negx);
extern unsigned char alu_probe_begin,alu_probe_end;
unsigned long alu_src,alu_dst,alu_before_sp,alu_registers[16];
unsigned short alu_ccr,alu_reply_ccr;
static unsigned long checks;
static void hex(const char *label,unsigned long value) {
    char out[11];const char *digits="0123456789abcdef";unsigned i;
    for(i=0;i<8;++i)out[i]=digits[(value>>(28-4*i))&15];
    out[8]='\r';out[9]='\n';out[10]=0;
    nf_call(nf_id("NF_STDERR"),label);nf_call(nf_id("NF_STDERR"),out);
}
static long long signed_value(unsigned long v,unsigned long long modulus,unsigned long sign) {
    return v&sign ? (long long)v-(long long)modulus : (long long)v;
}
static long run(void) {
    void (*probe[7][3])(void)={
        {alu_addx_b,alu_addx_w,alu_addx_l},{alu_subx_b,alu_subx_w,alu_subx_l},
        {alu_add_b,alu_add_w,alu_add_l},{alu_sub_b,alu_sub_w,alu_sub_l},
        {alu_cmp_b,alu_cmp_w,alu_cmp_l},{alu_neg_b,alu_neg_w,alu_neg_l},
        {alu_negx_b,alu_negx_w,alu_negx_l}};
    unsigned pass,op,size,si,di,ccr,i;
    unsigned long expected=0,flags=0;long failure=0;
    volatile unsigned long *registration=(volatile unsigned long *)0x00fef020UL;
    registration[0]=(unsigned long)&alu_probe_begin;
    registration[1]=(unsigned long)&alu_probe_end;
    for(pass=0;pass<2;++pass)for(op=0;op<7;++op)for(size=0;size<3;++size) {
        unsigned width=size==0?8:size==1?16:32;
        unsigned long long modulus=1ULL<<width;
        unsigned long mask=(unsigned long)(modulus-1),sign=(unsigned long)(modulus>>1);
        unsigned long edge[8]={0,1,2,sign-1,sign,sign+1,mask-1,mask};
        for(si=0;si<(op>=5?1:8);++si)for(di=0;di<8;++di)for(ccr=0;ccr<32;++ccr) {
            unsigned long src=edge[si],dst=edge[di],result;
            unsigned long x=(op==0||op==1||op==6)?((ccr>>4)&1):0;
            unsigned long long a=op>=5?dst:src,b=op>=5?0:dst,wide;
            long long smath;
            int subtract=op==1||op==3||op==4||op>=5,carry,overflow;
            wide=subtract?b-a-x:b+a+x;
            result=(unsigned long)(wide&mask);
            carry=subtract?a+x>b:wide>=modulus;
            smath=subtract?signed_value((unsigned long)b,modulus,sign)-signed_value((unsigned long)a,modulus,sign)-x:
                           signed_value((unsigned long)b,modulus,sign)+signed_value((unsigned long)a,modulus,sign)+x;
            overflow=smath<-(long long)sign||smath>=(long long)sign;
            flags=(carry?1:0)|(overflow?2:0)|(result==0?4:0)|(result&sign?8:0)|
                  (op==4?(ccr&16):(carry?16:0));
            if((op==0||op==1||op==6)&&!(ccr&4))flags&=~4UL;
            alu_src=(0x76543210UL&~mask)|src;
            alu_dst=(0xfedcba98UL&~mask)|dst;
            expected=op==4?alu_dst:(alu_dst&~mask)|result;
            alu_ccr=ccr;probe[op][size]();++checks;
#ifdef ALU_WRONG_Z
            if(pass==1)flags^=4;
#endif
            if(alu_registers[0]!=alu_src||alu_registers[1]!=expected||alu_reply_ccr!=flags) {failure=1;goto done;}
            for(i=2;i<8;++i)if(alu_registers[i]!=0x11110000UL+i){failure=2;goto done;}
            for(i=0;i<7;++i)if(alu_registers[i+8]!=0x22220000UL+i){failure=3;goto done;}
            if(alu_registers[15]!=alu_before_sp){failure=4;goto done;}
        }
    }
done:
    if(failure) {
        hex("ALU op=",op);hex("ALU size=",size);hex("ALU CCR=",ccr);
        hex("ALU source=",alu_src);hex("ALU destination=",alu_dst);
        hex("ALU expected result=",expected);hex("ALU actual result=",alu_registers[1]);
        hex("ALU expected CCR=",flags);hex("ALU actual CCR=",alu_reply_ccr);
    }
    hex("ALU checks=",checks);
    nf_call(nf_id("NF_STDERR"),failure?"ALU ORACLE FAIL\r\n":"ALU ORACLE PASS 64512 cases ADDX/SUBX/ADD/SUB/CMP/NEG/NEGX B/W/L allCCR D/A/A7\r\n");
    nf_call(nf_id("NF_EXIT"),failure);return failure;
}
int main(void){return (int)Supexec(run);}
