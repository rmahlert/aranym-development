/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "c-api.h"
#include "core.hpp"
#include <cstring>
namespace {
bool busy=false;
m040_bus_callback callback=nullptr;void *callback_opaque=nullptr;
struct Boundary { Boundary(){busy=true;} ~Boundary(){busy=false;} };
bool bus(void *,const modern040::BusRequest &source,uae_u32 &value) {
    const m040_bus_request request{M040_ABI_VERSION,uint32_t(source.kind),source.address,source.size,
        source.function_code,uint32_t(source.alternate_function_code.has_value()),source.alternate_function_code.value_or(0),source.transfer_type};
    uint32_t data=value;const int status=callback(callback_opaque,&request,&data);
    if(status!=0&&status!=1)throw std::runtime_error("bus callback must return0 or1");
    value=data;return status==1;
}
void result(const modern040::Result &source,m040_result &out) {
    static_assert(unsigned(modern040::Exit::DoubleFault)==M040_DOUBLE_FAULT);
    out={};out.abi_version=M040_ABI_VERSION;out.exit=uint32_t(source.exit);out.vector=source.vector;
    out.instructions=source.instructions;out.instruction_pc=source.instruction_pc;out.current_pc=source.current_pc;
    out.address=source.address;out.size=source.size;
    std::strncpy(out.detail,source.detail.c_str(),sizeof(out.detail)-1);
}
int failed() noexcept {modern040::ismoves_nommu=false;modern040::host.capability_latched=true;return M040_HOST_ERROR;}
}
extern "C" {
int m040_reset(const m040_options *options) {
    if(busy)return M040_BUSY;
    if(!options||options->abi_version!=M040_ABI_VERSION||options->deliver_exceptions>1||options->lc040_probes>1||options->log_accesses>1)return M040_BAD_ARGUMENT;
    try{Boundary guard;modern040::reset();modern040::install_handlers();callback=nullptr;callback_opaque=nullptr;
        modern040::host.deliver_exceptions=options->deliver_exceptions;modern040::host.lc040_probes=options->lc040_probes;
        modern040::host.log_accesses=options->log_accesses;return M040_OK;}catch(...){return failed();}
}
int m040_get_state(m040_state *out) {
    if(busy)return M040_BUSY;
    if(!out)return M040_BAD_ARGUMENT;
    try{Boundary guard;const auto state=modern040::architectural_state();*out={};out->abi_version=M040_ABI_VERSION;
        for(unsigned i=0;i<16;++i)out->regs[i]=state.regs[i];
        out->pc=state.pc;out->instruction_pc=state.instruction_pc;out->usp=state.usp;out->isp=state.isp;out->msp=state.msp;out->sr=state.sr;out->stopped=state.stopped;
#define EXPORT(field) out->field=state.field
        EXPORT(vbr);EXPORT(sfc);EXPORT(dfc);EXPORT(cacr);EXPORT(caar);EXPORT(itt0);EXPORT(itt1);EXPORT(dtt0);EXPORT(dtt1);
        EXPORT(tcr);EXPORT(mmusr);EXPORT(urp);EXPORT(srp);EXPORT(buscr);EXPORT(pcr);
#undef EXPORT
        return M040_OK;}catch(...){return failed();}
}
int m040_set_state(const m040_state *in) {
    if(busy)return M040_BUSY;
    if(!in||in->abi_version!=M040_ABI_VERSION||in->stopped>2||in->sfc>7||in->dfc>7)return M040_BAD_ARGUMENT;
    const auto bank=!(in->sr&0x2000)?in->usp:(in->sr&0x1000)?in->msp:in->isp;
    if(in->regs[15]!=bank)return M040_BAD_ARGUMENT;
    if(modern040::host.service&&in->pc!=modern040::host.service->pc)return M040_BAD_ARGUMENT;
    try{Boundary guard;auto &state=modern040::regs;
        // Set both old/new bank selectors before the real SR helper so it does
        // not replace the caller's already canonical active A7 with old state.
        state.s=bool(in->sr&0x2000);state.m=bool(in->sr&0x1000);state.sr=in->sr;
        for(unsigned i=0;i<16;++i)state.regs[i]=in->regs[i];
        state.pc=in->pc;state.instruction_pc=in->instruction_pc;state.usp=in->usp;state.isp=in->isp;state.msp=in->msp;state.stopped=in->stopped;
#define IMPORT(field) state.field=in->field
        IMPORT(vbr);IMPORT(sfc);IMPORT(dfc);IMPORT(cacr);IMPORT(caar);IMPORT(itt0);IMPORT(itt1);IMPORT(dtt0);IMPORT(dtt1);
        IMPORT(tcr);IMPORT(mmusr);IMPORT(urp);IMPORT(srp);IMPORT(buscr);IMPORT(pcr);
#undef IMPORT
        modern040::MakeFromSR();return M040_OK;}catch(...){return failed();}
}
int m040_set_bus(m040_bus_callback function,void *opaque) {
    if(busy)return M040_BUSY;
    callback=function;callback_opaque=opaque;
    modern040::host.bus=function?bus:nullptr;modern040::host.bus_opaque=nullptr;return M040_OK;
}
int m040_poke(uint32_t address,uint32_t size,uint32_t value) {
    if(busy)return M040_BUSY;
    if((size!=1&&size!=2&&size!=4)||modern040::host.bus)return M040_BAD_ARGUMENT;
    if(address>=modern040::host.ram.size()||size>modern040::host.ram.size()-address)return M040_BAD_ARGUMENT;
    try{Boundary guard;modern040::poke(address,size,value);return M040_OK;}catch(...){return failed();}
}
int m040_run(uint64_t budget,m040_result *out) {
    if(busy)return M040_BUSY;
    if(!out)return M040_BAD_ARGUMENT;
    try{Boundary guard;result(modern040::run(budget),*out);return M040_OK;}catch(...){*out={};out->abi_version=M040_ABI_VERSION;return failed();}
}
int m040_interrupt(uint32_t vector,uint32_t level,int *accepted,m040_result *out) {
    if(busy)return M040_BUSY;
    if(!accepted||!out||vector>255||level<1||level>7)return M040_BAD_ARGUMENT;
    *accepted=0;
    try{Boundary guard;*accepted=modern040::inject_interrupt(int(vector),level);result({modern040::Exit::Budget},*out);return M040_OK;}
    catch(const modern040::Stop &event){if(event.result.exit==modern040::Exit::DoubleFault)modern040::host.halted=true;
        if(event.result.exit==modern040::Exit::MemoryFault)modern040::host.access_fault_latched=true;
        if(event.result.exit==modern040::Exit::Unsupported)modern040::host.capability_latched=true;
        result(event.result,*out);return M040_OK;}
    catch(...){return failed();}
}
int m040_private_operations(int enabled) {
    if(busy||modern040::host.service)return M040_BUSY;
    if(enabled!=0&&enabled!=1)return M040_BAD_ARGUMENT;
    modern040::host.private_operations=enabled;return M040_OK;
}
int m040_get_service(m040_service *out) {
    if(busy)return M040_BUSY;
    if(!out||!modern040::host.service)return M040_BAD_ARGUMENT;
    const auto &service=*modern040::host.service;
    *out={M040_ABI_VERSION,uint32_t(service.kind),service.opcode,service.pc,service.stack,uint32_t(service.supervisor)};
    return M040_OK;
}
int m040_complete_service(m040_result *out) {
    if(busy)return M040_BUSY;
    if(!out||!modern040::host.service)return M040_BAD_ARGUMENT;
    try{Boundary guard;result(modern040::complete_service(),*out);return M040_OK;}catch(...){return failed();}
}
}
