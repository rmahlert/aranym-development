/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <mint/osbind.h>
extern long nf_id(const char *),nf_call(long,...);
extern unsigned long ram_scalar(unsigned long),ram_memory(unsigned long),ram_fill(unsigned long);
unsigned long ram_source[16384],ram_dest[16384];
__asm__(".text\n_nf_id:\n.word 0x7300\nrts\n_nf_call:\n.word 0x7301\nrts\n");
static long run(void){
    unsigned round,offset,mode,i;long error=0;
    unsigned long (*const fn[3])(unsigned long)={ram_scalar,ram_memory,ram_fill};
    const char *const begin[3]={"RAMBEN BEGIN SCALAR\n","RAMBEN BEGIN MEMORY\n","RAMBEN BEGIN FILL\n"};
    const char *const end[3]={"RAMBEN END SCALAR\n","RAMBEN END MEMORY\n","RAMBEN END FILL\n"};
    for(i=0;i<16384;++i)ram_source[i]=0x9e3779b9UL^i;
    for(mode=0;mode<3;++mode)if(fn[mode](2)!=2)error=2;
    for(round=0;round<5&&!error;++round)for(offset=0;offset<3;++offset){
        mode=(round+offset)%3;
        nf_call(nf_id("NF_STDERR"),begin[mode]);if(fn[mode](128)!=128)error=2;
        nf_call(nf_id("NF_STDERR"),end[mode]);
        for(i=0;i<16384;++i)if(ram_dest[i]!=(mode==2?0x76543210UL:ram_source[i]))error=1;
    }
    nf_call(nf_id("NF_STDERR"),error?"RAMBEN CHECKSUM FAIL\n":"RAMBEN CHECKSUM PASS\n");
    nf_call(nf_id("NF_EXIT"),error);return error;
}
int main(void){return (int)Supexec(run);}
