/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "c-api.h"
#include "core.hpp"
#include "restart.hpp"
#include "softfloat-adapter.hpp"
#include <cstring>
namespace {
bool busy=false;
m040_bus_callback callback=nullptr;void *callback_opaque=nullptr;
m040_precise_bus_callback precise_callback=nullptr;
m040_detailed_bus_callback detailed_callback=nullptr;
m040_frame_storage_callback storage_callback=nullptr;void*storage_opaque=nullptr;
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
modern040::FrameStorage frame_storage(void*,const modern040::BusRequest&source) {
    const m040_bus_request request{M040_ABI_VERSION,uint32_t(source.kind),source.address,source.size,
        source.function_code,uint32_t(source.alternate_function_code.has_value()),source.alternate_function_code.value_or(0),source.transfer_type};
    uint64_t key=0;const auto kind=storage_callback(storage_opaque,&request,&key);
    if(kind<M040_FRAME_UNKNOWN||kind>M040_FRAME_DISJOINT)throw std::runtime_error("malformed frame storage identity");
    return {modern040::FrameStorageClass(kind),key};
}
int failed() noexcept {modern040::ismoves_nommu=false;modern040::host.capability_latched=true;return M040_HOST_ERROR;}
}
extern "C" {
int m040_reset(const m040_options *options) {
    if(busy)return M040_BUSY;
    if(!options||options->abi_version!=M040_ABI_VERSION||options->deliver_exceptions>1||options->lc040_probes>1||options->log_accesses>1)return M040_BAD_ARGUMENT;
    try{Boundary guard;modern040::reset();modern040::install_handlers();callback=nullptr;precise_callback=nullptr;detailed_callback=nullptr;callback_opaque=nullptr;
        reset_callback=nullptr;reset_opaque=nullptr;storage_callback=nullptr;storage_opaque=nullptr;
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
int m040_get_fpu_state(m040_fpu_state *out) {
    if(busy)return M040_BUSY;
    if(!out)return M040_BAD_ARGUMENT;
    try{Boundary guard;const auto state=modern040::architectural_state();*out={};out->abi_version=M040_FPU_STATE_VERSION;
        for(unsigned reg=0;reg<8;++reg)for(unsigned word=0;word<3;++word)out->fp[reg][word]=state.fp[reg][word];
        out->fpcr=state.fpcr;out->fpsr=state.fpsr;out->fpiar=state.fpiar;return M040_OK;
    }catch(...){return failed();}
}
int m040_get_fpu_activity(uint32_t *out) {
    if(busy)return M040_BUSY;
    if(!out)return M040_BAD_ARGUMENT;
    try{Boundary guard;*out=modern040::regs.fpu_state;return M040_OK;}catch(...){return failed();}
}
int m040_set_fpu_activity(uint32_t value) {
    if(busy)return M040_BUSY;
    if(value>1)return M040_BAD_ARGUMENT;
    try{Boundary guard;modern040::regs.fpu_state=value;return M040_OK;}catch(...){return failed();}
}
int m040_set_state(const m040_state *in) {
    if(busy)return M040_BUSY;
    if(!in||in->abi_version!=M040_ABI_VERSION||in->stopped>2||in->sfc>7||in->dfc>7)return M040_BAD_ARGUMENT;
    const auto bank=!(in->sr&0x2000)?in->usp:(in->sr&0x1000)?in->msp:in->isp;
    if(in->regs[15]!=bank)return M040_BAD_ARGUMENT;
    if(modern040::host.service&&modern040::host.service->kind!=modern040::ServiceKind::LegacyFpu&&in->pc!=modern040::host.service->pc)return M040_BAD_ARGUMENT;
    try{Boundary guard;auto &state=modern040::regs;const auto old_tcr=state.tcr,old_urp=state.urp,old_srp=state.srp;
        // Set both old/new bank selectors before the real SR helper so it does
        // not replace the caller's already canonical active A7 with old state.
        state.s=bool(in->sr&0x2000);state.m=bool(in->sr&0x1000);state.sr=in->sr;
        for(unsigned i=0;i<16;++i)state.regs[i]=in->regs[i];
        state.pc=in->pc;state.instruction_pc=in->instruction_pc;state.usp=in->usp;state.isp=in->isp;state.msp=in->msp;state.stopped=in->stopped;
#define IMPORT(field) state.field=in->field
        IMPORT(vbr);IMPORT(sfc);IMPORT(dfc);IMPORT(cacr);IMPORT(caar);IMPORT(itt0);IMPORT(itt1);IMPORT(dtt0);IMPORT(dtt1);
        IMPORT(tcr);IMPORT(mmusr);IMPORT(urp);IMPORT(srp);IMPORT(buscr);IMPORT(pcr);
#undef IMPORT
        modern040::MakeFromSR();if(state.tcr!=old_tcr||state.urp!=old_urp||state.srp!=old_srp)state.tcr=modern040::mmu_set_tc(state.tcr);
        modern040::mmu_tt_modified();return M040_OK;}catch(...){return failed();}
}
int m040_set_fpu_state(const m040_fpu_state *in) {
    if(busy)return M040_BUSY;
    if(!in||in->abi_version!=M040_FPU_STATE_VERSION)return M040_BAD_ARGUMENT;
    try{Boundary guard;auto &state=modern040::regs;
        for(unsigned reg=0;reg<8;++reg)for(unsigned word=0;word<3;++word)state.fp[reg][word]=in->fp[reg][word];
        state.fpcr=in->fpcr;state.fpsr=in->fpsr;state.fpiar=in->fpiar;return M040_OK;
    }catch(...){return failed();}
}
static void install_fpu_result(unsigned destination,const modern040::FpuValue &value) {
    auto &state=modern040::regs;
    state.fp[destination]=value.words;
    const auto exponent=(value.words[0]>>16)&0x7fff;
    const auto significand=(std::uint64_t(value.words[1])<<32)|value.words[2];
    const bool nan=exponent==0x7fff&&(significand<<1)!=0;
    const bool infinity=exponent==0x7fff&&(significand<<1)==0;
    const bool zero=exponent==0&&significand==0;
    const std::uint32_t condition=(value.words[0]&0x80000000?0x08000000u:0)|
        (zero?0x04000000u:0)|(infinity?0x02000000u:0)|(nan?0x01000000u:0);
    const auto status=value.status&0x0000ff00u;
    state.fpsr=(state.fpsr&0x00ff0000u)|condition|status|modern040::accrued_exceptions(state.fpsr, status);
}
int m040_fpu_binary(uint32_t operation,uint32_t destination,uint32_t source) {
    if(busy)return M040_BUSY;
    if(operation>M040_FPU_DIV||destination>7||source>7)return M040_BAD_ARGUMENT;
    try{Boundary guard;
        const auto value=modern040::softfloat_binary(modern040::FpuBinary(operation),modern040::regs.fp[destination],modern040::regs.fp[source],modern040::regs.fpcr);
        install_fpu_result(destination,value);return M040_OK;
    }catch(...){return failed();}
}
int m040_fpu_sqrt(uint32_t destination,uint32_t source) {
    if(busy)return M040_BUSY;
    if(destination>7||source>7)return M040_BAD_ARGUMENT;
    try{Boundary guard;install_fpu_result(destination,modern040::softfloat_sqrt(modern040::regs.fp[source],modern040::regs.fpcr));return M040_OK;
    }catch(...){return failed();}
}
int m040_set_bus(m040_bus_callback function,void *opaque) {
    if(busy)return M040_BUSY;
    modern040::invalidate_completed_frames();modern040::host.completed_frame_irq=false;
    storage_callback=nullptr;storage_opaque=nullptr;modern040::host.frame_storage=nullptr;
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
    modern040::invalidate_completed_frames();modern040::host.completed_frame_irq=false;
    storage_callback=nullptr;storage_opaque=nullptr;modern040::host.frame_storage=nullptr;
    precise_callback=function;callback=nullptr;detailed_callback=nullptr;callback_opaque=opaque;
    modern040::host.detailed_bus=nullptr;
    modern040::host.bus=nullptr;modern040::host.precise_bus=function?precise_bus:nullptr;
    modern040::host.bus_opaque=nullptr;return M040_OK;
}
int m040_set_detailed_bus(m040_detailed_bus_callback function,void *opaque) {
    if(busy)return M040_BUSY;
    modern040::invalidate_completed_frames();modern040::host.completed_frame_irq=false;
    storage_callback=nullptr;storage_opaque=nullptr;modern040::host.frame_storage=nullptr;
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
int m040_set_frame_storage(m040_frame_storage_callback function,void*opaque) {
    if(busy||modern040::host.service)return M040_BUSY;
    modern040::invalidate_completed_frames();storage_callback=function;storage_opaque=opaque;
    modern040::host.frame_storage=function?frame_storage:nullptr;modern040::host.frame_storage_opaque=nullptr;
    return M040_OK;
}
int m040_completed_frame_irq(int enabled) {
    if(busy||modern040::host.service)return M040_BUSY;
    if((enabled!=0&&enabled!=1)||(enabled&&!storage_callback))return M040_BAD_ARGUMENT;
    modern040::invalidate_completed_frames();modern040::host.completed_frame_overflow=false;
    modern040::host.completed_frame_irq=enabled;return M040_OK;
}
int m040_frame_write_notify(const m040_bus_request*request) {
    if(busy)return M040_BUSY;
    if(!request||request->abi_version!=M040_ABI_VERSION||request->kind!=M040_WRITE||!request->size||
       uint64_t(request->address)+request->size>0x100000000ULL||request->function_code>7||request->transfer_type>3||
       request->has_alternate_function_code>1||(request->has_alternate_function_code&&request->alternate_function_code>7))return M040_BAD_ARGUMENT;
    try{Boundary guard;modern040::observe_frame_write({modern040::AccessKind::Write,request->address,request->size,request->function_code,
        request->has_alternate_function_code?std::optional<unsigned>(request->alternate_function_code):std::nullopt,request->transfer_type});return M040_OK;}catch(...){return failed();}
}
int m040_frame_barrier(void) {
    // A hardware RAM writer can run inside the current CPU bus callback.
    // This same-thread metadata-only operation cannot reenter a resolver or
    // alter architectural state, and is deliberately safe at that boundary.
    modern040::invalidate_completed_frames();return M040_OK;
}
int m040_completed_rte_event(int*out) {
    if(busy)return M040_BUSY;
    if(!out)return M040_BAD_ARGUMENT;
    *out=modern040::host.completed_rte_event;return M040_OK;
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
    *pending=bool(modern040::regs.spcflags&modern040::SPCFLAG_MMURESTART)&&!modern040::host.completed_irq_ready;return M040_OK;
}
int m040_native_restart_pending(int *pending) {
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
int m040_interrupt_ready(uint32_t level,int *ready) {
    if(busy)return M040_BUSY;
    if(!ready||level<1||level>7)return M040_BAD_ARGUMENT;
    *ready=modern040::interrupt_ready(level);return M040_OK;
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
    if(enabled&&modern040::host.modern_fpu_operations)return M040_BAD_ARGUMENT;
    modern040::host.legacy_fpu_operations=enabled;return M040_OK;
}
int m040_modern_fpu_operations(int enabled) {
    if(busy||modern040::host.service)return M040_BUSY;
    if(enabled!=0&&enabled!=1)return M040_BAD_ARGUMENT;
    if(enabled&&modern040::host.legacy_fpu_operations)return M040_BAD_ARGUMENT;
    modern040::host.modern_fpu_operations=enabled;return M040_OK;
}
}
