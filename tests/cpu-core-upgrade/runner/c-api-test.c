/* SPDX-License-Identifier: GPL-2.0-or-later
 * Compiled as ordinary C11, without either emulator's private CPU headers.
 */
#include "c-api.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static unsigned checks;static int wrong_state;
static void expect(int pass,const char *message) {++checks;if(!pass){fprintf(stderr,"FAIL C ABI: %s\n",message);exit(wrong_state&&strcmp(message,"C result exports expected registers/SR/A7")==0?1:2);}}
static const struct m040_options options={M040_ABI_VERSION,1,1,0};
static void word(uint32_t address,uint32_t value){expect(m040_poke(address,2,value)==M040_OK,"flat program initialization");}
struct device {unsigned char bytes[256];unsigned accesses;int reentry_status;};
static int bus(void *opaque,const struct m040_bus_request *request,uint32_t *value) {
    struct device *dev=opaque;struct m040_state state;unsigned i;
    dev->reentry_status=m040_get_state(&state);++dev->accesses;
    expect(request->abi_version==M040_ABI_VERSION&&request->function_code==(request->kind==M040_FETCH?6u:5u),"C bus exact request version and FC");
    if(request->address<0x01000100||request->address+request->size>0x01000200)return 0;
    if(request->kind==M040_WRITE){uint32_t v=*value;for(i=request->size;i>0;--i){dev->bytes[request->address-0x01000100+i-1]=(unsigned char)v;v>>=8;}}
    else{*value=0;for(i=0;i<request->size;++i)*value=(*value<<8)|dev->bytes[request->address-0x01000100+i];}
    return 1;
}
static int invalid_bus(void *opaque,const struct m040_bus_request *request,uint32_t *value) {(void)opaque;(void)request;(void)value;return -7;}
int main(int argc,char **argv) {
    struct m040_state state,copy;struct m040_result result;struct m040_service service;int accepted;struct device dev={{0},0,0};unsigned i;
    if(argc>2||(argc==2&&strcmp(argv[1],"--negative-state")))return 2;
    wrong_state=argc==2;
    expect(m040_reset(0)==M040_BAD_ARGUMENT,"null options rejected");
    {struct m040_options wrong=options;wrong.abi_version=99;expect(m040_reset(&wrong)==M040_BAD_ARGUMENT,"unknown ABI rejected");}
    expect(m040_reset(&options)==M040_OK,"reset real library");
    expect(m040_get_state(&state)==M040_OK&&state.abi_version==M040_ABI_VERSION,"export initial architecture");
    state.regs[15]=state.isp=0x9000;state.usp=0x7000;state.msp=0x8000;
    expect(m040_set_state(&state)==M040_OK,"import canonical architecture");
    word(0x100,0x7003);word(0x102,0x5280);word(0x104,0x4e72);word(0x106,0x2700);
    expect(m040_run(100,&result)==M040_OK&&result.exit==M040_STOPPED&&result.instructions==3,"C caller executes genuine generated MOVEQ ADDQ STOP");
    expect(m040_get_state(&state)==M040_OK&&state.regs[0]==(wrong_state?5u:4u)&&state.regs[15]==0x9000&&state.sr==0x2700,"C result exports expected registers/SR/A7");
    state.stopped=0;state.pc=0x200;state.sr=0x371f;state.regs[15]=state.msp;
    state.vbr=0x400;state.sfc=1;state.dfc=6;state.cacr=0x80008000;state.caar=0x1234;state.itt0=0x1000;state.itt1=0x2000;state.dtt0=0x3000;state.dtt1=0x4000;state.tcr=0;state.mmusr=0xab;state.urp=0x12340000;state.srp=0x56780000;state.buscr=0x9abc;state.pcr=0xdef0;
    expect(m040_set_state(&state)==M040_OK&&m040_get_state(&copy)==M040_OK,"full architectural C snapshot import/export");
    for(i=0;i<16;++i)expect(copy.regs[i]==state.regs[i],"C snapshot all D/A registers");
#define CHECK(field) expect(copy.field==state.field,"C snapshot " #field)
    CHECK(pc);CHECK(instruction_pc);CHECK(usp);CHECK(isp);CHECK(msp);CHECK(sr);CHECK(stopped);
    CHECK(vbr);CHECK(sfc);CHECK(dfc);CHECK(cacr);CHECK(caar);CHECK(itt0);CHECK(itt1);CHECK(dtt0);CHECK(dtt1);
    CHECK(tcr);CHECK(mmusr);CHECK(urp);CHECK(srp);CHECK(buscr);CHECK(pcr);
#undef CHECK
    copy.regs[15]++;expect(m040_set_state(&copy)==M040_BAD_ARGUMENT,"inconsistent active-bank/A7 rejected");
    word(0x200,0x4e71);expect(m040_run(1,&result)==M040_OK&&result.exit==M040_BUDGET&&result.instructions==1,"bounded C execution after master-state import");
    expect(m040_interrupt(64,3,&accepted,&result)==M040_OK&&!accepted&&result.exit==M040_BUDGET,"masked custom IRQ reports typed non-acceptance");
    expect(m040_interrupt(256,3,&accepted,&result)==M040_BAD_ARGUMENT&&m040_interrupt(64,0,&accepted,&result)==M040_BAD_ARGUMENT,"IRQ bounds checked");
    expect(m040_reset(&options)==M040_OK&&m040_get_state(&state)==M040_OK,"reset for C custom IRQ delivery");
    state.sr=0x201f;state.regs[15]=state.isp=0x9000;expect(m040_set_state(&state)==M040_OK,"unmask C IRQ state");
    expect(m040_poke(64*4,4,0x200)==M040_OK,"custom IRQ vector memory");word(0x200,0x4e73);
    expect(m040_interrupt(64,3,&accepted,&result)==M040_OK&&accepted&&result.exit==M040_BUDGET,"C IRQ API delivers explicit custom vector");
    expect(m040_get_state(&state)==M040_OK&&state.pc==0x200&&state.isp==0x8ff8&&(state.sr&0x700)==0x300,"C IRQ captures real frame and priority");
    expect(m040_run(1,&result)==M040_OK&&result.exit==M040_BUDGET&&m040_get_state(&state)==M040_OK&&state.pc==0x100&&state.isp==0x9000&&state.sr==0x201f,"C IRQ frame returns through actual generated RTE");
    expect(m040_poke(0xffffffff,4,0)==M040_BAD_ARGUMENT&&m040_poke(0x100,3,0)==M040_BAD_ARGUMENT,"flat initialization bounds/width checked");
    expect(m040_run(1,0)==M040_BAD_ARGUMENT&&m040_get_state(0)==M040_BAD_ARGUMENT,"null outputs rejected");
    expect(m040_reset(&options)==M040_OK&&m040_get_state(&state)==M040_OK,"reset between integration profiles");
    state.pc=state.instruction_pc=0x01000100;state.regs[15]=state.isp=0x9000;
    expect(m040_set_state(&state)==M040_OK&&m040_set_bus(bus,&dev)==M040_OK,"attach plain C high-address bus");
    dev.bytes[0]=0x70;dev.bytes[1]=0x09;dev.bytes[2]=0x4e;dev.bytes[3]=0x72;dev.bytes[4]=0x27;dev.bytes[5]=0;
    expect(m040_poke(0x100,2,0)==M040_BAD_ARGUMENT,"flat poke forbidden while external bus attached");
    expect(m040_run(10,&result)==M040_OK&&result.exit==M040_STOPPED&&result.instructions==2,"C bus drives real high-address guest program");
    expect(m040_get_state(&state)==M040_OK&&state.regs[0]==9&&dev.accesses==3&&dev.reentry_status==M040_BUSY,"C callback reentry rejected without corrupting active instruction");
    expect(m040_reset(&options)==M040_OK&&m040_get_state(&state)==M040_OK,"reset clears attached bus");
    state.pc=state.instruction_pc=0x01000100;expect(m040_set_state(&state)==M040_OK&&m040_set_bus(invalid_bus,0)==M040_OK,"attach broken callback for boundary control");
    expect(m040_run(1,&result)==M040_HOST_ERROR,"invalid callback response contained inside C boundary");
    expect(m040_run(1,&result)==M040_OK&&result.exit==M040_UNSUPPORTED&&result.instructions==0,"host failure latches unsafe continuation");
    expect(m040_reset(&options)==M040_OK&&m040_get_state(&state)==M040_OK,"reset clears failure latch");
    state.tcr=0x8000;expect(m040_set_state(&state)==M040_OK,"control snapshot can represent enabled translation");
    expect(m040_run(1,&result)==M040_OK&&result.exit==M040_UNSUPPORTED_MMU&&result.instructions==0,"C ABI cannot silently execute unsupported MMU profile");
    for(i=0;i<sizeof(result.detail);++i)if(result.detail[i]==0)break;
    expect(i<sizeof(result.detail),"bounded terminated result detail");
    expect(m040_reset(&options)==M040_OK&&m040_private_operations(1)==M040_OK&&m040_get_state(&state)==M040_OK,"C private platform extension opt-in");
    state.regs[15]=state.isp=0x9000;expect(m040_set_state(&state)==M040_OK,"C service argument stack");word(0x100,0x7300);
    expect(m040_run(10,&result)==M040_OK&&result.exit==M040_HOST_SERVICE&&result.instructions==1,"C runner suspends actual generated NatFeat body");
    expect(m040_get_service(&service)==M040_OK&&service.kind==M040_NATFEAT_ID&&service.opcode==0x7300&&service.pc==0x100&&service.stack==0x9004&&service.supervisor,"C service metadata exposes real stack ABI");
    expect(m040_private_operations(0)==M040_BUSY,"C cannot disable pending source continuation");
    expect(m040_get_state(&state)==M040_OK,"C host can inspect pending registers");state.regs[0]=42;
    expect(m040_set_state(&state)==M040_OK&&m040_complete_service(&result)==M040_OK&&result.exit==M040_BUDGET&&result.instructions==0,"C reply completes actual generated body without extra guest count");
    expect(m040_get_state(&state)==M040_OK&&state.pc==0x102&&state.regs[0]==42&&state.sr==0x2700,"C service completion preserves expected PC/D0/SR");
    expect(m040_complete_service(&result)==M040_BAD_ARGUMENT&&m040_get_service(&service)==M040_BAD_ARGUMENT,"C cannot complete same private call twice");
    printf("PASS: %u pure C integration ABI assertions\n",checks);return 0;
}
