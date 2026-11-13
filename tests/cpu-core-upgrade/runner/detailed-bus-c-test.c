/* SPDX-License-Identifier: GPL-2.0-or-later
 * Public C11 consumer: no private CPU types or callbacks.
 */
#include "c-api.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks;
static void need(int ok,const char *why) {
    ++checks;if(!ok){fprintf(stderr,"FAIL detailed C ABI: %s\n",why);exit(1);}
}
struct device {unsigned char ram[65536];unsigned calls,writes;int deny;};
static int bus(void *opaque,const struct m040_bus_request *q,uint32_t *v,struct m040_bus_detail *d) {
    struct device *b=opaque;struct m040_state state;unsigned i;
    ++b->calls;
    need(q->abi_version==1&&d->abi_version==1&&d->completion==0&&d->origin==0&&
        d->whole_scalar_no_commit==0&&d->has_cpu_physical_address==0&&d->cpu_physical_address==0,
        "new reply for every request");
    need(m040_get_state(&state)==M040_BUSY&&m040_set_detailed_bus(0,0)==M040_BUSY,
        "callback reentry rejected");
    need(q->address<=sizeof(b->ram)-q->size,"owned RAM bounds");
    if(q->kind==M040_WRITE){
        ++b->writes;
        if(b->deny){
            d->completion=M040_BUS_FAULT_BEFORE_TRANSFER;d->origin=M040_ORIGIN_EXTERNAL_BUS;
            d->whole_scalar_no_commit=1;d->has_cpu_physical_address=1;d->cpu_physical_address=q->address;
            return 1;
        }
        for(i=q->size;i>0;--i){b->ram[q->address+i-1]=(unsigned char)*v;*v>>=8;}
    }else{*v=0;for(i=0;i<q->size;++i)*v=(*v<<8)|b->ram[q->address+i];}
    d->completion=M040_BUS_COMPLETED;
    /* Valid irrelevant metadata must not contaminate the next request. */
    d->origin=M040_ORIGIN_ATC_TRANSLATION;d->has_cpu_physical_address=1;d->cpu_physical_address=0xdeadbeef;
    return 1;
}
int main(void) {
    const struct m040_options options={M040_ABI_VERSION,1,0,0};
    struct m040_state state;struct m040_result r;struct device b;unsigned count;
    _Static_assert(M040_ABI_VERSION==1,"old base ABI");
    _Static_assert(sizeof(struct m040_bus_detail)==24,"versioned fixed-width reply");
    memset(&b,0,sizeof(b));
    need(m040_reset(&options)==M040_OK&&m040_fault_recovery(1)==M040_OK,"prepare real restart-aware core");
    need(m040_get_state(&state)==M040_OK,"export canonical state");
    state.regs[0]=0x12345678;state.regs[8]=0x5000;state.regs[15]=state.isp=0x9000;
    need(m040_set_state(&state)==M040_OK&&m040_set_detailed_bus(bus,&b)==M040_OK,"attach public C callback");
    b.ram[0x100]=0x20;b.ram[0x101]=0x80;b.deny=1; /* MOVE.L D0,(A0) */
    need(m040_run(1,&r)==M040_OK&&r.exit==M040_MEMORY_FAULT&&r.vector==2&&r.address==0x5000&&r.size==4,
        "typed write failure remains terminal before frame integration");
    need(b.writes==1&&b.ram[0x5000]==0,"no extra target or frame write");count=b.calls;
    need(m040_run(10,&r)==M040_OK&&r.exit==M040_UNSUPPORTED&&r.instructions==0&&b.calls==count,
        "terminal fault cannot retry implicitly");
    need(m040_reset(&options)==M040_OK&&m040_poke(0x100,2,0x4e71)==M040_OK,
        "reset detaches detailed callback and clears latch");
    need(m040_run(1,&r)==M040_OK&&r.exit==M040_BUDGET&&b.calls==count,"flat RAM after reset");
    need(m040_reset(&options)==M040_OK&&m040_get_state(&state)==M040_OK,"successful detailed execution control");
    state.regs[0]=0x12345678;state.regs[8]=0x5000;state.regs[15]=state.isp=0x9000;
    need(m040_set_state(&state)==M040_OK&&m040_set_detailed_bus(bus,&b)==M040_OK,"reattach fresh callback");
    b.deny=0;b.ram[0x102]=0x4e;b.ram[0x103]=0x72;b.ram[0x104]=0x27;
    need(m040_run(10,&r)==M040_OK&&r.exit==M040_STOPPED&&r.instructions==2,
        "successful MOVE and STOP through detailed callback");
    need(b.ram[0x5000]==0x12&&b.ram[0x5001]==0x34&&b.ram[0x5002]==0x56&&b.ram[0x5003]==0x78,
        "exact numeric big-endian store data");
    printf("PASS: %u pure C detailed-bus assertions\n",checks);return 0;
}
