/* SPDX-License-Identifier: GPL-2.0-or-later
 * RAM/register/CCR/device oracle, using immutable host-integer expectations.
 */
#include <mint/osbind.h>
extern long nf_id(const char *),nf_call(long,...),rmw_jit_query(void);
__asm__(".text\n_nf_id:\n.word 0x7300\nrts\n_nf_call:\n.word 0x7301\nrts\n_rmw_jit_query:\nmoveq #-1,%d0\n.word 0x713b\nrts\n");
volatile unsigned char rmw_data[256];volatile unsigned long rmw_pointers[16];
unsigned long rmw_input[16],rmw_output[16],rmw_saved_sp,rmw_fake_sp;
unsigned short rmw_ccr,rmw_reply,rmw_saved_sr;
extern unsigned char rmw_probe_begin,rmw_probe_end;
#include "rmw-cases.h"
static unsigned long checks,native_checks,noflags_checks,device_checks,warmed_checks;
static void hex(const char *s,unsigned long x){char b[11];unsigned i;for(i=0;i<8;++i)b[i]="0123456789abcdef"[(x>>(28-4*i))&15];b[8]='\r';b[9]='\n';b[10]=0;nf_call(nf_id("NF_STDERR"),s);nf_call(nf_id("NF_STDERR"),b);}
static void init(const struct RmwCase*p,const struct RmwVector*v){unsigned i;volatile unsigned char *target;
    for(i=0;i<16;++i)rmw_input[i]=i<8?0x11110000UL+i:0x22220000UL+i;
    rmw_input[1]=4;rmw_input[2]=0xfffffff9UL;rmw_input[6]=1;rmw_input[13]=3;
    rmw_input[10]=(unsigned long)(p->pointer?(void*)&rmw_pointers[4]:(void*)&rmw_data[64]);
    rmw_fake_sp=(unsigned long)&rmw_data[64];
    for(i=0;i<256;++i)rmw_data[i]=(unsigned char)(i^0xa5);
    for(i=0;i<16;++i)rmw_pointers[i]=(unsigned long)&rmw_data[(i*8)&255];
    rmw_pointers[4]=(unsigned long)&rmw_data[96];
    target=p->space?p->pc_data+p->offset:&rmw_data[p->offset];
    for(i=0;i<p->size;++i)target[i]=(unsigned char)(v->input>>(8*(p->size-1-i)));
}
static long run(void){
    volatile unsigned long *const witness=(volatile unsigned long*)0x00fec000UL,*const device=(volatile unsigned long*)0x00fed000UL;
    unsigned pass,c=0,ccr=0,i=0,retry,warm,vi=0,ordinal,choices;long error=0,jit=rmw_jit_query()&1;
    unsigned long expected[16],pointers[16],before=0,after=0,nf_before=0,nf_after=0,read_count,write_count,configured;
    unsigned char bytes[256];const struct RmwCase*p=0;const struct RmwVector*v=0;
    witness[0]=(unsigned long)&rmw_probe_begin;witness[1]=(unsigned long)&rmw_probe_end;
    for(pass=0;pass<2;++pass)for(c=0;c<sizeof(rmw_cases)/sizeof(rmw_cases[0]);++c)for(ccr=0;ccr<32;++ccr){
        p=&rmw_cases[c];ordinal=p->size==1?0:p->size==2?1:2;choices=p->full_values?8:1;
        for(vi=0;vi<choices;++vi){
            v=&rmw_vectors[p->family][ordinal][p->full_values?vi:(ccr+pass)&7][ccr];++checks;rmw_ccr=ccr;
            if(pass&&jit&&p->device){
                witness[2]=(unsigned long)p->pc;before=witness[2];nf_before=witness[3];
                for(warm=0;warm<64;++warm){init(p,v);p->fn();}
                after=witness[2];nf_after=witness[3];if(after==before||(RMW_FLAGS_DEAD&&nf_after==nf_before)||(!RMW_FLAGS_DEAD&&nf_after!=nf_before)){error=7;goto done;}++warmed_checks;
            }
            for(retry=0;retry<64;++retry){
                init(p,v);configured=p->device==3?(unsigned long)&rmw_data[96]:v->input;
                device[1]=configured;
                if(p->device==1||p->device==3)rmw_input[10]=0x00fed000UL;
                if(p->device==2)rmw_pointers[4]=0x00fed000UL;
                for(i=0;i<16;++i){expected[i]=rmw_input[i];pointers[i]=rmw_pointers[i];}
                for(i=0;i<256;++i)bytes[i]=rmw_data[i];
                if(!p->a7)expected[10]+=p->update;
                if(RMW_FLAGS_DEAD)expected[6]=0;
                if((!p->device||p->device==3)&&!p->space&&p->family!=4&&p->family!=14)
                    for(i=0;i<p->size;++i)bytes[p->offset+i]=(unsigned char)(v->output>>(8*(p->size-1-i)));
                rmw_ccr=ccr;witness[2]=(unsigned long)p->pc;before=witness[2];nf_before=witness[3];p->fn();after=witness[2];nf_after=witness[3];
                expected[15]=p->a7?rmw_fake_sp+p->update:rmw_saved_sp;
#ifdef RMW_WRONG_RESULT
                if(pass)expected[4]^=1;
#endif
                for(i=0;i<16;++i)if(rmw_output[i]!=expected[i]){error=1;goto done;}
                for(i=0;i<256;++i)if(rmw_data[i]!=bytes[i]){error=2;goto done;}
                for(i=0;i<16;++i)if(rmw_pointers[i]!=pointers[i]){error=3;goto done;}
                if(rmw_reply!=(RMW_FLAGS_DEAD?4:v->flags)){error=4;goto done;}
                if(p->space)for(i=0;i<p->size;++i)if(p->pc_data[p->offset+i]!=(unsigned char)(v->input>>(8*(p->size-1-i)))){error=8;goto done;}
                read_count=!p->device?0:p->device==3?1:p->family==2?0:1;
                write_count=p->device&&p->device!=3&&p->family!=4&&p->family!=14?1:0;
                if(device[1]!=read_count||device[2]!=write_count||(write_count&&device[3]!=v->output)){error=6;goto done;}
                if(!pass||!jit)break;
                if(p->device){if(after!=before||nf_after!=nf_before){error=5;goto done;}++device_checks;break;}
                if(after==before+1){if(nf_after!=nf_before+RMW_FLAGS_DEAD){error=5;goto done;}++native_checks;if(RMW_FLAGS_DEAD)++noflags_checks;break;}
                if(after!=before){error=5;goto done;}
            }
            if(retry==64){error=5;goto done;}
        }
    }
done:
#ifdef RMW_WRONG_COUNT
    if(!error)++checks;
#endif
    hex("RMW checks=",checks);hex("RMW native=",native_checks);hex("RMW noflags=",noflags_checks);hex("RMW device-deopt=",device_checks);hex("RMW warmed-device=",warmed_checks);
    if(error){hex("RMW case=",c);hex("RMW family=",p?p->family:0);hex("RMW size=",p?p->size:0);hex("RMW device=",p?p->device:0);hex("RMW CCR=",ccr);hex("RMW vector=",vi);hex("RMW index=",i);hex("RMW error=",error);hex("RMW expected CCR=",v?v->flags:0);hex("RMW actual CCR=",rmw_reply);hex("RMW expected register=",i<16?expected[i]:0);hex("RMW actual register=",i<16?rmw_output[i]:0);hex("RMW before=",before);hex("RMW after=",after);}
    nf_call(nf_id("NF_STDERR"),error?"RMW ORACLE FAIL\r\n":"RMW ORACLE PASS immutable-host-ALU allGPR allCCR allRAM A7 device-once\r\n");
    nf_call(nf_id("NF_EXIT"),error);return error;
}
int main(void){return (int)Supexec(run);}
