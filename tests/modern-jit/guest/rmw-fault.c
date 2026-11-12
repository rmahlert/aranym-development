/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <mint/osbind.h>
extern long nf_id(const char *),nf_call(long,...),rmwf_enter(void),rmwf_jit_query(void);
extern unsigned char rmwf_begin,rmwf_end,rmwf_target,rmwf_next;
unsigned long rmwf_old_vector,rmwf_target_sp,rmwf_input[16],rmwf_output[16],rmwf_fault_regs[16];
unsigned long rmwf_native_start,rmwf_native_before,rmwf_native_after,rmwf_fault_count,rmwf_frame_pc,rmwf_fault_address,rmwf_wb3_address,rmwf_wb3_data;
unsigned short rmwf_mode,rmwf_ccr,rmwf_reply,rmwf_frame_sr,rmwf_format,rmwf_ssw,rmwf_wb3s,rmwf_ram;
__asm__(".text\n_nf_id:\n.word 0x7300\nrts\n_nf_call:\n.word 0x7301\nrts\n_rmwf_jit_query:\nmoveq #-1,%d0\n.word 0x713b\nrts\n");
static void hex(const char*s,unsigned long v){char b[11];unsigned i;for(i=0;i<8;++i)b[i]="0123456789abcdef"[(v>>(28-4*i))&15];b[8]='\r';b[9]='\n';b[10]=0;nf_call(nf_id("NF_STDERR"),s);nf_call(nf_id("NF_STDERR"),b);}
static long run(void){
    volatile unsigned long *w=(volatile unsigned long*)0x00fec000UL,*dev=(volatile unsigned long*)0x00fed080UL;
    long jit=rmwf_jit_query()&1,error=0;unsigned ccr,mode,i=0,checks=0;unsigned long expected;
    w[0]=(unsigned long)&rmwf_begin;w[1]=(unsigned long)&rmwf_end;w[2]=(unsigned long)&rmwf_target;
    for(mode=0;mode<2;++mode)for(ccr=0;ccr<32;++ccr){
        ++checks;rmwf_mode=mode;rmwf_ccr=ccr;rmwf_fault_count=0;
        for(i=0;i<16;++i)rmwf_input[i]=i<8?0x11110000UL+i:0x22220000UL+i;
        rmwf_enter();
        if(rmwf_fault_count!=1||rmwf_format!=0x7008||rmwf_reply!=0x0a){error=2;goto done;}
        if(rmwf_frame_sr!=(mode?(0x2700|ccr):0x270a)||rmwf_frame_pc!=(unsigned long)(mode?&rmwf_target:&rmwf_next)){error=2;goto done;}
        if(rmwf_ssw!=(mode?0x145:0x45)||rmwf_fault_address!=(mode?0x00fed090UL:0x00fed080UL)){error=2;goto done;}
        if(mode){
            if(rmwf_wb3s||dev[1]||dev[2]||dev[5]!=1||rmwf_ram!=0x8000){error=3;goto done;}
        }else{
            expected=0x8000;
#ifdef RMW_FAULT_WRONG_WB
            expected^=1;
#endif
            if(rmwf_wb3s!=0xc5||rmwf_wb3_address!=0x00fed080UL||rmwf_wb3_data!=expected||dev[1]!=1||dev[2]!=1||dev[3]!=0x8000||dev[5]||rmwf_ram!=0x7fff){error=3;goto done;}
        }
        for(i=0;i<16;++i){
            expected=i==8?(mode?0x00fed090UL:0x00fed082UL):i==15?rmwf_target_sp-60:rmwf_input[i];
            if(rmwf_fault_regs[i]!=expected){error=1;goto done;}
            expected=i==8?(mode?(unsigned long)&rmwf_ram+2:0x00fed082UL):i==15?rmwf_target_sp:rmwf_input[i];
            if(rmwf_output[i]!=expected){error=1;goto done;}
        }
        if(jit&&(rmwf_native_before<=rmwf_native_start||rmwf_native_after!=rmwf_native_before)){error=4;goto done;}
    }
done:
    hex("RMW FAULT checks=",checks);
    if(error){hex("RMW FAULT mode=",mode);hex("RMW FAULT CCR=",ccr);hex("RMW FAULT index=",i);hex("RMW FAULT error=",error);hex("RMW FAULT SR=",rmwf_frame_sr);hex("RMW FAULT PC=",rmwf_frame_pc);hex("RMW FAULT SSW=",rmwf_ssw);hex("RMW FAULT WB3=",rmwf_wb3_data);hex("RMW FAULT before=",rmwf_native_before);hex("RMW FAULT after=",rmwf_native_after);}
    nf_call(nf_id("NF_STDERR"),error?"RMW FAULT ORACLE FAIL\r\n":"RMW FAULT ORACLE PASS read-once writeback/discard read-fault/RTE/retry allCCR allGPR\r\n");
    nf_call(nf_id("NF_EXIT"),error);return error;
}
int main(void){return (int)Supexec(run);}
