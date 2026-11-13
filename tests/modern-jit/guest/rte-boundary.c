/* SPDX-License-Identifier: GPL-2.0-or-later
 * Dedicated 64MiB FastRAM guest: valid header at RAM end, unmapped SSW read.
 */
#include <mint/osbind.h>
extern long nf_id(const char*),nf_call(long,...);
extern long rb_execute(void);
__asm__(".text\n_nf_id:\n.word 0x7300\nrts\n_nf_call:\n.word 0x7301\nrts\n");
static void message(const char* s){nf_call(nf_id("NF_STDERR"),s);}
void rb_success(void){message("RTE_BOUNDARY control returned with format0\r\n");nf_call(nf_id("NF_EXIT"),0);}
void rb_recursive(void){message("RTE_BOUNDARY recursive vector2 handler reached\r\n");nf_call(nf_id("NF_EXIT"),4);}
static long run(void){message("RTE_BOUNDARY prepared header=04fffff8 SSW=05000004\r\n");rb_execute();message("RTE_BOUNDARY unexpected return\r\n");nf_call(nf_id("NF_EXIT"),5);return 5;}
int main(void){return(int)Supexec(run);}
