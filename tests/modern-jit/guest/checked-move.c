/* SPDX-License-Identifier: GPL-2.0-or-later
 * Ordinary guest exercises the explicit synthetic read-sensitive bus fixture.
 */
#include <mint/osbind.h>
extern long nf_id(const char *);
extern long nf_call(long,...);
extern void bus_read_b(void),bus_read_w(void),bus_read_l(void);
extern void bus_write_b(void),bus_write_w(void),bus_write_l(void);
unsigned long bus_pointer=0x00fef000,bus_input,bus_before_sp,bus_registers[16];
unsigned short bus_ccr,bus_reply_ccr;
__asm__(".text\n_nf_id:\n.word 0x7300\nrts\n_nf_call:\n.word 0x7301\nrts\n");
#ifndef COUNT_DELTA
#define COUNT_DELTA 0
#endif
static volatile unsigned long * const device=(volatile unsigned long *)0x00fef000UL;
static int preserved(unsigned long expected,unsigned size) {
    unsigned i;unsigned long value=expected;
    for(i=0;i<7;++i)if(bus_registers[i]!=0x11110000UL+i)return 0;
    for(i=0;i<6;++i)if(bus_registers[8+i]!=0x22220000UL+i)return 0;
    if(bus_registers[14]!=bus_pointer||bus_registers[15]!=bus_before_sp||bus_registers[7]!=expected)return 0;
    if(size==1)value&=255;else if(size==2)value&=65535;
    if(bus_reply_ccr!=((bus_ccr&16)|(value==0?4:0)|((value&(size==1?128UL:size==2?32768UL:0x80000000UL))?8:0)))return 0;
    return 1;
}
static long checks(void) {
    unsigned width,i;void (*reads[3])(void)={bus_read_b,bus_read_w,bus_read_l};
    void (*writes[3])(void)={bus_write_b,bus_write_w,bus_write_l};
    const unsigned sizes[3]={1,2,4};
    for(width=0;width<3;++width) {
        const unsigned size=sizes[width];const unsigned long mask=size==1?255UL:size==2?65535UL:0xffffffffUL;
        device[1]=0;
        for(i=0;i<512;++i) {
            unsigned long expected;bus_input=0xa5a55a5aUL;bus_ccr=i&31;
            reads[width]();expected=(bus_input&~mask)|((0x12340000UL|(i+1))&mask);
            if(!preserved(expected,size))return 1;
        }
        if(device[1]!=512+COUNT_DELTA||device[2]!=0)return 2;
        device[1]=0;
        for(i=0;i<512;++i) {
            bus_input=0x87654321UL^i;bus_ccr=i&31;writes[width]();
            if(!preserved(bus_input,size))return 3;
        }
        if(device[1]!=0||device[2]!=512||device[3]!=(bus_input&mask))return 4;
    }
#ifdef FAULT_TRANSFER
    device[1]=0;bus_pointer=0x00fef014;bus_ccr=0;bus_read_l();return 5;
#endif
    return 0;
}
static long run(void) {
    long result=checks();
    nf_call(nf_id("NF_STDERR"),result?"CHECKED MOVE GUEST FAIL\r\n":"CHECKED MOVE GUEST PASS B/W/L/allCCR/D/A/A7/read-once/write-once\r\n");
    nf_call(nf_id("NF_EXIT"),result);return result;
}
int main(void){return (int)Supexec(run);}
