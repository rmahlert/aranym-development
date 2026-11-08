/* Ordinary AES application to exercise the real MiNT poweroff path. */
#include <mint/osbind.h>
#include <mint/mintbind.h>
#include <gem.h>
extern long nf_id(const char *);
extern long nf_call(long,...);
__asm__(".text\n_nf_id:\n.word 0x7300\nrts\n_nf_call:\n.word 0x7301\nrts\n");
static long start(void)
{
 long eth=nf_id("ETHERNET");
 if(!eth)return -1;
 return nf_call(eth+3,0L);
}
int main(void)
{
 long rc;
 appl_init();
 rc=Supexec(start);
 if(rc){nf_call(nf_id("NF_STDERR"),"FULL MINT SHUTDOWN FAIL receiver start\r\n");return 1;}
 nf_call(nf_id("NF_STDERR"),"FULL MINT SHUTDOWN requesting GEMDOS Shutdown(0), receiver active\r\n");
 rc=Shutdown(0);
 nf_call(nf_id("NF_STDERR"),"FULL MINT SHUTDOWN FAIL Shutdown returned\r\n");
 appl_exit();return rc?1:2;
}
