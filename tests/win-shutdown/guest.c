/* Ordinary guest regression: host exit with an idle SLIRP receive thread. */
#include <mint/osbind.h>
extern long nf_id(const char *);
extern long nf_call(long,...);
__asm__(".text\n_nf_id:\n.word 0x7300\nrts\n_nf_call:\n.word 0x7301\nrts\n");
#ifndef START_NETWORK
#define START_NETWORK 1
#endif
#ifndef REPEAT_STOPS
#define REPEAT_STOPS 1
#endif
#ifndef EXPLICIT_STOP
#define EXPLICIT_STOP 0
#endif
static void report(const char *s) { nf_call(nf_id("NF_STDERR"),s); }
static long run(void)
{
    long eth=nf_id("ETHERNET"),shutdown=nf_id("NF_SHUTDOWN");
    unsigned long until;
    int cycle;
    if (!shutdown || (START_NETWORK && !eth)) {
        report("SHUTDOWN REGRESSION FAIL missing NatFeat\r\n");return 1;
    }
    for (cycle=0; START_NETWORK && cycle<REPEAT_STOPS; cycle++) {
        if (nf_call(eth+3,0L)!=0) {
            report("SHUTDOWN REGRESSION FAIL start receiver\r\n");return 1;
        }
        report("SHUTDOWN REGRESSION receiver started\r\n");
        until=*(volatile unsigned long *)0x4baL+40;
        while ((long)(*(volatile unsigned long *)0x4baL-until)<0) { }
        if (EXPLICIT_STOP) {
            report("SHUTDOWN REGRESSION requesting receiver stop\r\n");
            nf_call(eth+4,0L);
            report("SHUTDOWN REGRESSION receiver stopped\r\n");
        }
    }
    report("SHUTDOWN REGRESSION requesting NF_SHUTDOWN\r\n");
    nf_call(shutdown,0L);
    return 0;
}
int main(void) { return (int)Supexec(run); }
