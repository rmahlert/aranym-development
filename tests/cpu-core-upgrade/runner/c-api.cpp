/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "c-api.h"
#include "core.hpp"
#include "restart.hpp"
#include <cstring>
namespace {
bool busy=false;
m040_bus_callback callback=nullptr;void *callback_opaque=nullptr;
m040_precise_bus_callback precise_callback=nullptr;
m040_detailed_bus_callback detailed_callback=nullptr;
m040_reset_callback reset_callback=nullptr;void *reset_opaque=nullptr;
bool reset_hardware(void *) {
    const auto status=reset_callback(reset_opaque);
    if(status!=0&&status!=1)throw std::runtime_error("reset callback must return0 or1");
    return status==1;
}
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
modern040::BusCompletion precise_bus(void *,const modern040::BusRequest &source,uae_u32 &value) {
    const m040_bus_request request{M040_ABI_VERSION,uint32_t(source.kind),source.address,source.size,
        source.function_code,uint32_t(source.alternate_function_code.has_value()),source.alternate_function_code.value_or(0),source.transfer_type};
    const int status=precise_callback(callback_opaque,&request,&value);
    if(status<0||status>2)throw std::runtime_error("precise bus callback returned an invalid completion");
    return modern040::BusCompletion(status);
}
modern040::BusOutcome detailed_bus(void *,const modern040::BusRequest &source,uae_u32 &value) {
    const m040_bus_request request{M040_ABI_VERSION,uint32_t(source.kind),source.address,source.size,
        source.function_code,uint32_t(source.alternate_function_code.has_value()),source.alternate_function_code.value_or(0),source.transfer_type};
    m040_bus_detail detail{M040_BUS_DETAIL_VERSION,0,0,0,0,0};
    const int supplied=detailed_callback(callback_opaque,&request,&value,&detail);
    if(supplied!=0&&supplied!=1)throw std::runtime_error("detailed bus callback must return0 or1");
    if(!supplied)return {};
    if(detail.abi_version!=M040_BUS_DETAIL_VERSION||detail.completion>2||
       (detail.origin!=M040_ORIGIN_UNSPECIFIED&&detail.origin!=M040_ORIGIN_EXTERNAL_BUS&&detail.origin!=M040_ORIGIN_ATC_TRANSLATION)||
       detail.whole_scalar_no_commit>1||detail.has_cpu_physical_address>1)
        throw std::runtime_error("detailed bus callback returned malformed evidence");
    const auto completion=modern040::BusCompletion(detail.completion);
    if(completion==modern040::BusCompletion::Completed)return {completion,{}};
    if(detail.whole_scalar_no_commit&&completion!=modern040::BusCompletion::FaultBeforeTransfer)
        throw std::runtime_error("whole-scalar no-commit evidence contradicts completion");
    return {completion,modern040::FaultEvidence{modern040::FaultOrigin(detail.origin),bool(detail.whole_scalar_no_commit),
        detail.has_cpu_physical_address?std::optional<uae_u32>(detail.cpu_physical_address):std::nullopt}};
}
int failed() noexcept {modern040::ismoves_nommu=false;modern040::host.capability_latched=true;return M040_HOST_ERROR;}
}
extern "C" {
int m040_reset(const m040_options *options) {
    if(busy)return M040_BUSY;
    if(!options||options->abi_version!=M040_ABI_VERSION||options->deliver_exceptions>1||options->lc040_probes>1||options->log_accesses>1)return M040_BAD_ARGUMENT;
    try{Boundary guard;modern040::reset();modern040::install_handlers();callback=nullptr;precise_callback=nullptr;detailed_callback=nullptr;callback_opaque=nullptr;
        reset_callback=nullptr;reset_opaque=nullptr;
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
    if(modern040::host.service&&modern040::host.service->kind!=modern040::ServiceKind::LegacyFpu&&in->pc!=modern040::host.service->pc)return M040_BAD_ARGUMENT;
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
        modern040::MakeFromSR();modern040::mmu_tt_modified();return M040_OK;}catch(...){return failed();}
}
int m040_set_bus(m040_bus_callback function,void *opaque) {
    if(busy)return M040_BUSY;
    callback=function;callback_opaque=opaque;
    precise_callback=nullptr;detailed_callback=nullptr;modern040::host.precise_bus=nullptr;modern040::host.detailed_bus=nullptr;
    modern040::host.bus=function?bus:nullptr;modern040::host.bus_opaque=nullptr;return M040_OK;
}
int m040_set_reset_handler(m040_reset_callback function,void *opaque) {
    if(busy)return M040_BUSY;
    reset_callback=function;reset_opaque=opaque;
    modern040::host.reset_hardware=function?reset_hardware:nullptr;modern040::host.reset_opaque=nullptr;return M040_OK;
}
int m040_set_precise_bus(m040_precise_bus_callback function,void *opaque) {
    if(busy)return M040_BUSY;
    precise_callback=function;callback=nullptr;detailed_callback=nullptr;callback_opaque=opaque;
    modern040::host.detailed_bus=nullptr;
    modern040::host.bus=nullptr;modern040::host.precise_bus=function?precise_bus:nullptr;
    modern040::host.bus_opaque=nullptr;return M040_OK;
}
int m040_set_detailed_bus(m040_detailed_bus_callback function,void *opaque) {
    if(busy)return M040_BUSY;
    detailed_callback=function;callback=nullptr;precise_callback=nullptr;callback_opaque=opaque;
    modern040::host.bus=nullptr;modern040::host.precise_bus=nullptr;
    modern040::host.detailed_bus=function?detailed_bus:nullptr;modern040::host.bus_opaque=nullptr;return M040_OK;
}
int m040_physical_write_exceptions(int enabled) {
    if(busy||modern040::host.service)return M040_BUSY;
    if(enabled!=0&&enabled!=1)return M040_BAD_ARGUMENT;
    modern040::host.physical_write_exceptions=enabled;return M040_OK;
}
int m040_physical_read_exceptions(int enabled) {
    if(busy||modern040::host.service)return M040_BUSY;
    if(enabled!=0&&enabled!=1)return M040_BAD_ARGUMENT;
    modern040::host.physical_read_exceptions=enabled;return M040_OK;
}
int m040_bitfield_write_exceptions(int enabled) {
    if(busy||modern040::host.service)return M040_BUSY;
    if(enabled!=0&&enabled!=1)return M040_BAD_ARGUMENT;
    modern040::host.bitfield_write_exceptions=enabled;return M040_OK;
}
int m040_fault_recovery(int enabled) {
    if(busy||modern040::host.service)return M040_BUSY;
    if(enabled!=0&&enabled!=1)return M040_BAD_ARGUMENT;
    if(enabled)modern040::install_restart_handlers();
    modern040::host.fault_recovery=enabled;return M040_OK;
}
int m040_restart_pending(int *pending) {
    if(busy)return M040_BUSY;
    if(!pending)return M040_BAD_ARGUMENT;
    *pending=bool(modern040::regs.spcflags&modern040::SPCFLAG_MMURESTART);return M040_OK;
}
int m040_exception_history_limit(uint32_t entries) {
    if(busy)return M040_BUSY;
    modern040::host.frame_history_limit=entries;
    auto &frames=modern040::host.frames;
    if(!entries)std::vector<modern040::Frame>().swap(frames);
    else if(entries<frames.size())frames.erase(frames.begin(),frames.end()-entries);
    return M040_OK;
}
int m040_poke(uint32_t address,uint32_t size,uint32_t value) {
    if(busy)return M040_BUSY;
    if((size!=1&&size!=2&&size!=4)||modern040::host.bus||modern040::host.precise_bus||modern040::host.detailed_bus)return M040_BAD_ARGUMENT;
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
    try{Boundary guard;*accepted=modern040::inject_interrupt(int(vector),level);
        result({modern040::Exit::Budget,0,0,modern040::regs.instruction_pc,modern040::regs.pc},*out);return M040_OK;}
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
int m040_exception(uint32_t vector,m040_result *out) {
    if(busy)return M040_BUSY;
    if(!out||vector>255||vector<4)return M040_BAD_ARGUMENT;
    try{Boundary guard;modern040::host.service.reset();modern040::Exception(int(vector));
        result({modern040::Exit::Budget,0,0,modern040::regs.instruction_pc,modern040::regs.pc},*out);return M040_OK;}
    catch(const modern040::Stop &event){
        if(event.result.exit==modern040::Exit::DoubleFault)modern040::host.halted=true;
        if(event.result.exit==modern040::Exit::MemoryFault)modern040::host.access_fault_latched=true;
        if(event.result.exit==modern040::Exit::Unsupported)modern040::host.capability_latched=true;
        result(event.result,*out);return M040_OK;}
    catch(...){return failed();}
}
int m040_read_probe_exceptions(int enabled) {
    if(busy||modern040::host.service)return M040_BUSY;
    if(enabled!=0&&enabled!=1)return M040_BAD_ARGUMENT;
    modern040::host.read_probe_exceptions=enabled;return M040_OK;
}
int m040_get_exception_event(m040_exception_event *out) {
    if(busy)return M040_BUSY;
    if(!out)return M040_BAD_ARGUMENT;
    *out={};out->abi_version=M040_ABI_VERSION;out->count=modern040::host.exception_count;
    if(modern040::host.last_frame){const auto &frame=*modern040::host.last_frame;out->vector=frame.vector;out->format=frame.format;
        out->stack=frame.stack;out->handler=frame.handler;out->stacked_pc=frame.stacked_pc;out->saved_sr=frame.stacked_sr;out->interrupt=frame.interrupt;}
    return M040_OK;
}
int m040_legacy_fpu_operations(int enabled) {
    if(busy||modern040::host.service)return M040_BUSY;
    if(enabled!=0&&enabled!=1)return M040_BAD_ARGUMENT;
    modern040::host.legacy_fpu_operations=enabled;return M040_OK;
}
}
