#include <mint/osbind.h>
#include <stdio.h>
extern long nf_id(const char *);
extern long nf_call(long,...);
__asm__(".text\n_nf_id:\n.word 0x7300\nrts\n_nf_call:\n.word 0x7301\nrts\n");
static long test(void) {
    long i,missed=0,accepted=0,fail=0;char line[200];
    short (*probe)(long)=(short (*)(long))0x00e00ca0L;
    volatile unsigned char *middle=(void *)0x660fffffL,*last=(void *)0x7dffffffL;
    unsigned char oldmiddle=*middle,oldlast=*last;
    unsigned long top=*(volatile unsigned long *)0x5a4L;
    for(i=0;i<1000;i++) {
        if(!probe((long)middle)||!probe((long)last))missed++;
        if(probe(0x7e000000L)||probe(0x7e0fffffL))accepted++;
    }
    *middle=0xa5;*last=0x5a;
    if(*middle!=0xa5||*last!=0x5a)fail++;
    *middle=oldmiddle;*last=oldlast;
    if(top!=0x7e000000UL||missed||accepted)fail++;
    sprintf(line,"MEMORY TEST %s ramtop=%08lx missed=%ld guard-accepted=%ld writes-restored=Yes\r\n",fail?"FAIL":"PASS",top,missed,accepted);
    nf_call(nf_id("NF_STDERR"),line);
    return fail;
}
int main(void) { return Supexec(test)!=0; }
