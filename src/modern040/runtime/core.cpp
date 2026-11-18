/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "core.hpp"
#include "restart.hpp"
#include "mmu-walk.hpp"
#include <limits>
#include <cstdio>
#include <cstdlib>
namespace modern040 {
State regs;
Host host;
MmuFixup mmufixup[2];
bool mmu_restart=true;
int mmu040_movem=0;
uae_u32 mmu040_movem_ea=0;
std::array<Handler,65536> restart_handlers{};
bool ismoves_nommu=false;
bool mmu_ttr_enabled=false,mmu_ttr_enabled_ins=false,mmu_ttr_enabled_data=false;
uae_u8 mmu_cache_state=0;
std::array<Handler, 65536> handlers{};
std::array<PrivateHandler,65536> private_handlers{};
std::array<bool, 65536> legal{};
std::array<bool, 65536> requires_lc040_probes{};
std::array<bool,65536> legacy_fpu_opcodes{};
int movem_index1[256], movem_index2[256], movem_next[256];
uae_u32 mmu040_move16[4]{};
uae_u32 last_addr_for_exception_3 = 0, last_fault_for_exception_3 = 0, last_op_for_exception_3 = 0;
bool last_writeaccess_for_exception_3 = false, last_notinstruction_for_exception_3 = false;
int last_fc_for_exception_3 = 0, last_size_for_exception_3 = 0, last_di_for_exception_3 = 0;
uae_u16 last_sr_for_exception3 = 0;
namespace {
bool mmu_table_accessing=false;
struct AtcLine {bool valid=false;uae_u32 tag=0,physical=0,status=0;};
std::array<std::array<std::array<AtcLine,4>,16>,2> mmu_atc{};
std::array<unsigned,2> mmu_atc_next{};
void flush_atc() {for(auto&type:mmu_atc)for(auto&slot:type)for(auto&line:slot)line={};mmu_atc_next={};}
void flush_atc_address(uae_u32 address,bool supervisor,bool include_global) {
    const bool page8k=regs.tcr&0x4000;const auto tag=((supervisor?0x80000000u:0)|(address>>1))&(page8k?0xffff0000u:0xffff8000u);
    const unsigned index=page8k?(address&0x1e000)>>13:(address&0xf000)>>12;
    for(auto&type:mmu_atc)for(auto&line:type[index])if(line.valid&&line.tag==tag&&(include_global||!(line.status&0x400)))line={};
}
void frame_debug(const char*event,uae_u32 address,unsigned size,std::uint64_t id=0) {
    static const bool enabled=std::getenv("ARANYM_MODERN040_FRAME_IRQ_DIAGNOSTICS")!=nullptr;
    static unsigned messages=0;
    if(enabled&&messages<512){++messages;std::fprintf(stderr,"FRAME_IRQ %s address=%08x size=%u id=%llu count=%u pc=%08x ssw=%04x\n",event,address,size,(unsigned long long)id,host.completed_frame_count,regs.pc,regs.mmu_ssw);}
}
[[noreturn]] void stop(Exit exit, int vector, uae_u32 address, unsigned size, const char *detail) {
    throw Stop{{exit, 0, vector, regs.instruction_pc, regs.pc, address, size, detail}};
}
bool bitfield_memory_writer(uae_u16 opcode) {
    const auto family=opcode&0xffc0,ea=opcode&0x3f;
    return (family==0xeac0||family==0xecc0||family==0xeec0||family==0xefc0)&&
        ((ea>=0x10&&ea<=0x17)||(ea>=0x28&&ea<=0x39));
}
[[noreturn]] void abort_bitfield_tail(const char*detail) {
    const auto&request=host.deferred_bitfield->primary.request;
    throw Stop{{Exit::MemoryFault,0,2,host.start_pc,regs.pc,request.address,request.size,detail}};
}
bool valid(AccessKind kind, uae_u32 address, unsigned size) {
    if (!host.bus&&!host.precise_bus&&!host.detailed_bus&&(address >= host.ram.size() || size > host.ram.size() - address)) return false;
    if (host.denied && kind == host.denied->kind) {
        const auto first = host.denied->address;
        const auto end = std::uint64_t(first) + host.denied->size;
        if (address < end && std::uint64_t(address) + size > first) return false;
    }
    return true;
}
BusRequest request(AccessKind kind,uae_u32 address,unsigned size) {
    if(mmu_table_accessing)return {kind,address,size,7};
    if(!ismoves_nommu)return {kind,address,size,unsigned((regs.s?4:0)|(kind==AccessKind::Fetch?2:1))};
    const auto selected=unsigned(kind==AccessKind::Write?regs.dfc:regs.sfc)&7;
    // MC68040UM Table3-2 (PDF71): MOVES instruction-space references
    // become data references; other alternate spaces use TT=10.
    const auto effective=(selected==2||selected==6)?selected-1:selected;
    const unsigned tt=(selected==0||selected==3||selected==4||selected==7)?2:0;
    return {kind,address,size,effective,selected,tt};
}
bool mmu_table_access(void*,uae_u32 address,bool writing,uae_u32&value) {
    struct Scope {Scope(){mmu_table_accessing=true;}~Scope(){mmu_table_accessing=false;}} scope;
    try {if(writing)write(address,4,value);else value=read(AccessKind::Read,address,4);return true;}
    catch(const Stop&){return false;}
}
uae_u32 translate_address(const BusRequest&logical,uae_u32 supplied=0) {
    if(mmu_table_accessing||logical.transfer_type)return logical.address;
    const bool supervisor=logical.function_code&4,data=logical.kind!=AccessKind::Fetch,writing=logical.kind==AccessKind::Write;
    const auto transparent=mmu_ttr_enabled?mmu_match_ttr(logical.address,supervisor,data):0;
    if(transparent==2)return logical.address;
    if(transparent==1&&writing) {
        regs.mmu_fault_addr=logical.address;regs.mmusr=4;
        host.mmu_fault=MmuFault{logical,supplied,4};
        stop(Exit::MemoryFault,2,logical.address,logical.size,"transparent-region write protection fault");
    }
    if(!(regs.tcr&0x8000))return logical.address;
    const bool page8k=regs.tcr&0x4000;const auto page_mask=page8k?0x1fffu:0xfffu;
    const auto tag=((supervisor?0x80000000u:0)|(logical.address>>1))&(page8k?0xffff0000u:0xffff8000u);
    const unsigned type=data?1:0,index=page8k?(logical.address&0x1e000)>>13:(logical.address&0xf000)>>12;
    auto*replacement=&mmu_atc[type][index][mmu_atc_next[type]++&3];
    for(auto&line:mmu_atc[type][index])if(line.valid&&line.tag==tag) {
        if(!(line.status&1)||(!supervisor&&(line.status&0x80))||(writing&&(line.status&4))) {
            regs.mmu_fault_addr=logical.address;regs.mmusr=line.status;
            host.mmu_fault=MmuFault{logical,supplied,line.status};
            stop(Exit::MemoryFault,2,logical.address,logical.size,"68040 ATC protection/invalid fault");
        }
        if(!writing||(line.status&16))return line.physical|(logical.address&page_mask);
        replacement=&line;break; // first write rewalks to install descriptor M.
    }
    const auto walked=mmu_walk_040(logical.address,supervisor,writing,bool(regs.tcr&0x4000),regs.urp,regs.srp,mmu_table_access,nullptr);
    regs.mmusr=walked.status;
    if(!walked.valid) {
        *replacement={true,tag,0,walked.status}; // 68040 caches invalid/protection translations too.
        regs.mmu_fault_addr=logical.address;
        host.mmu_fault=MmuFault{logical,supplied,walked.status};
        stop(Exit::MemoryFault,2,logical.address,logical.size,"68040 paged translation fault");
    }
    *replacement={true,tag,walked.physical&~page_mask,walked.status};
    return walked.physical;
}
FrameStorage frame_storage(const BusRequest&request) {
    if(!host.frame_storage)return {};
    auto location=host.frame_storage(host.frame_storage_opaque,request);
    if(location.kind==FrameStorageClass::Ram&&request.size>std::numeric_limits<std::uint64_t>::max()-location.address)return {};
    return location;
}
void register_completed_frame(uae_u32 stack,uae_u32 pc,uae_u16 ssw) {
    if(host.completed_frame_irq&&host.completed_write_delivery)frame_debug("register-attempt",stack,60);
    if(!host.completed_frame_irq||host.completed_frame_overflow||!host.completed_write_delivery||
       (ssw&~0x67)||(ssw&0x60)==0x60||((ssw&7)!=1&&(ssw&7)!=5))return;
    const auto storage=frame_storage({AccessKind::Read,stack,60,5});
    if(storage.kind!=FrameStorageClass::Ram){frame_debug("register-unmapped",stack,60);return;}
    for(auto&frame:host.completed_frames)if(frame.valid&&frame.storage==storage.address){frame.valid=false;--host.completed_frame_count;}
    if(host.completed_frame_count==host.completed_frames.size()||host.completed_frame_serial==std::numeric_limits<std::uint64_t>::max()) {
        invalidate_completed_frames();host.completed_frame_overflow=true;return;
    }
    for(auto&frame:host.completed_frames)if(!frame.valid) {
        frame={true,++host.completed_frame_serial,storage.address,stack,pc,ssw};++host.completed_frame_count;return;
    }
}
void select_rte_completion(uae_u32 stack,uae_u16 format) {
    host.rte_completion.reset();
    if(!host.completed_frame_irq||!host.completed_frame_count||format!=0x7008||!host.rte_header_pc)return;
    const auto storage=frame_storage({AccessKind::Read,stack,60,5});
    if(storage.kind!=FrameStorageClass::Ram)return;
    for(const auto&frame:host.completed_frames)if(frame.valid&&frame.storage==storage.address&&frame.pc==*host.rte_header_pc) {
        frame_debug("rte-match",stack,60,frame.id);
        host.rte_completion=RteCompletion{frame.id,frame.pc};return;
    }
}
void complete_rte(std::uint64_t frames_before) {
    if(!host.rte_completion||!host.rte_completion->ssw_loaded||host.exception_count!=frames_before||regs.pc!=host.rte_completion->pc)return;
    for(auto&frame:host.completed_frames)if(frame.valid&&frame.id==host.rte_completion->id) {
        // Only successful return of the unchanged genuine RTE body consumes
        // this construction identity. No match by copied bytes or write SSW.
        frame.valid=false;--host.completed_frame_count;
        frame_debug("consume",frame.stack,60,frame.id);
        host.completed_rte_event=true;
        // IRQ eligibility is architectural retirement state. Keep the raw
        // restart flag for the existing native successor/compilation holdoff.
        host.completed_irq_ready=!regs.t0&&!regs.t1&&!(regs.spcflags&(SPCFLAG_TRACE|SPCFLAG_DOTRACE));
        return;
    }
}
struct CompletedDelivery {
    bool previous;
    explicit CompletedDelivery(bool completed):previous(host.completed_write_delivery){host.completed_write_delivery=completed;}
    ~CompletedDelivery(){host.completed_write_delivery=previous;}
};
void enter_exception(int nr, bool interrupt, unsigned level) {
    if(host.deferred_bitfield)abort_bitfield_tail("bitfield tail attempted another exception; reset required");
    if (host.halted) stop(Exit::DoubleFault,nr,regs.pc,0,"halted until reset");
    if (!host.deliver_exceptions) {
        stop(Exit::GuestException,nr,nr==3?last_fault_for_exception_3:regs.pc,nr==3?2:0,"terminal guest exception; frame delivery disabled");
    }
    if (nr==2&&!host.delivering_read_probe&&!host.delivering_fault) stop(Exit::Unsupported,2,regs.mmu_fault_addr,0,"format7 access-fault continuation/writeback not integrated");
    if (!interrupt&&nr>=48&&nr<=55) stop(Exit::Unsupported,nr,regs.pc,0,"floating-point arithmetic exception profile not integrated");
    if (host.exception_depth>=2) stop(Exit::DoubleFault,nr,regs.pc,0,"nested exception delivery limit");
    struct Depth {
        Depth() { ++host.exception_depth; }
        ~Depth() { --host.exception_depth; }
    } depth;
    try {
        resume_stopped();
        MakeSR();
        const auto saved_sr=regs.sr;
        const auto current_pc=regs.pc;
        const auto oldpc=regs.instruction_pc;
        if (!regs.s) {
            regs.usp=regs.regs[15];
            regs.regs[15]=regs.m?regs.msp:regs.isp;
            regs.s=true;
        }
        unsigned format=0;
        const bool absent_fpu=nr==11&&regs.fp_unimp_ins;
        if(absent_fpu&&!host.lc040_probes) stop(Exit::Unsupported,nr,regs.pc,0,"LC040 recognized-FPU profile disabled");
        uae_u32 stacked_pc=absent_fpu?current_pc:exception_pc(nr);
        if (interrupt && regs.m) {
            // Master entry: original context on MSP; throwaway context on ISP.
            // The throwaway SR retains M=1 so upstream RTE switches back to MSP.
            Exception_build_stack_frame(oldpc,current_pc,0,nr,0);
            regs.msp=regs.regs[15];
            regs.sr|=0x2000;
            regs.m=false;
            regs.regs[15]=regs.isp;
            Exception_build_stack_frame(oldpc,current_pc,0,nr,1);
            format=1; stacked_pc=current_pc;
        } else if (interrupt) {
            Exception_build_stack_frame(oldpc,current_pc,0,nr,0);
            stacked_pc=current_pc;
        } else if(nr==2) {
            stacked_pc=host.delivering_fault?host.recovery_pc:host.read_probe_next_pc;
            Exception_build_stack_frame(oldpc,stacked_pc,regs.mmu_ssw,nr,7);
            format=7;
        } else if (nr==3) {
            Exception_build_stack_frame(last_fault_for_exception_3,current_pc,0,nr,2);
            format=2; stacked_pc=current_pc;
            if (last_op_for_exception_3 & 0x10000) x_put_word(regs.regs[15],last_sr_for_exception3);
        } else {
            Exception_build_stack_frame_common(oldpc,stacked_pc,regs.mmu_ssw,nr,nr);
            format=absent_fpu?4:(nr==5||nr==6||nr==7||nr==9)?2:0;
        }
        const auto handler=read(AccessKind::Read,regs.vbr+4u*uae_u32(nr),4);
        const auto frame_sr=uae_u16(format==1?(saved_sr|0x2000):nr==3&&(last_op_for_exception_3&0x10000)?last_sr_for_exception3:saved_sr);
        host.last_frame=Frame{nr,format,regs.regs[15],handler,stacked_pc,frame_sr,interrupt};
        ++host.exception_count;
        if(host.frame_history_limit) {
            if(host.frames.size()>=host.frame_history_limit)host.frames.erase(host.frames.begin());
            host.frames.push_back(*host.last_frame);
        }
        if (handler&1) {
            if (nr==2||nr==3) stop(Exit::DoubleFault,nr,handler,2,"odd bus/address-error vector");
            complete_exception_trace(nr);
            exception3_notinstruction(regs.opcode,handler);
            return;
        }
        if (interrupt) regs.intmask=int(level);
        m68k_setpc(handler);
        complete_exception_trace(nr);
        if(nr==2&&format==7&&!(saved_sr&0xc000))register_completed_frame(host.last_frame->stack,stacked_pc,regs.mmu_ssw);
    } catch (const Stop &event) {
        if (event.result.exit==Exit::MemoryFault)
            stop(Exit::DoubleFault,event.result.vector,event.result.address,event.result.size,"fault while writing frame or fetching vector");
        throw;
    }
}
}
void invalidate_completed_frames() noexcept {
    if(host.completed_frame_count)frame_debug("barrier",0,0);
    for(auto&frame:host.completed_frames)frame.valid=false;
    host.completed_frame_count=0;host.rte_completion.reset();host.completed_irq_ready=false;
}
void observe_frame_write(const BusRequest&request) {
    if(!host.completed_frame_irq||!host.completed_frame_count)return;
    const auto storage=frame_storage(request);
    if(storage.kind==FrameStorageClass::Unknown){frame_debug("write-unknown",request.address,request.size);invalidate_completed_frames();return;}
    if(storage.kind!=FrameStorageClass::Ram)return;
    for(auto&frame:host.completed_frames)if(frame.valid&&storage.address<frame.storage+14&&frame.storage+2<storage.address+request.size) {
        // Saved SR is intentionally outside this identity window: actual
        // handlers alter IPL/trace there. PC/format/EA/SSW writes invalidate
        // even if the writer restores identical bytes or later fails.
        if(host.rte_completion&&host.rte_completion->id==frame.id)host.rte_completion.reset();
        frame_debug("write-overlap",request.address,request.size,frame.id);
        frame.valid=false;--host.completed_frame_count;
    }
}
uae_u32 read(AccessKind kind, uae_u32 address, unsigned size) {
    if(host.deferred_bitfield)abort_bitfield_tail("bitfield tail attempted a further read/fetch; reset required");
    if(!mmu_table_accessing&&(regs.tcr&0x8000)&&size>1) {
        const auto page_size=(regs.tcr&0x4000)?8192u:4096u;
        if((address&(page_size-1))+size>page_size) {
            uae_u32 joined=0;for(unsigned i=0;i<size;++i)joined=(joined<<8)|read(kind,address+i,1);return joined;
        }
    }
    uae_u32 value = 0;
    auto req=request(kind,address,size);req.address=translate_address(req);
    const bool detailed_profile=host.detailed_bus!=nullptr;
    const bool restartable_read=detailed_profile&&mmu_restart&&!host.exception_depth&&!host.service_resuming&&
        (host.in_instruction?host.using_restart:kind==AccessKind::Fetch);
    bool ok=valid(kind,req.address,size);
    BusOutcome outcome{ok?BusCompletion::Completed:BusCompletion::FaultBeforeTransfer,{}};
    if(ok&&host.detailed_bus)outcome=host.detailed_bus(host.bus_opaque,req,value);
    else if(ok&&host.precise_bus)outcome.completion=host.precise_bus(host.bus_opaque,req,value);
    else if(ok&&host.bus)outcome.completion=host.bus(host.bus_opaque,req,value)?BusCompletion::Completed:BusCompletion::UnknownFault;
    const auto completion=outcome.completion;
    ok=completion==BusCompletion::Completed;
    if (!ok) {
        host.bus_fault=BusFault{req,regs.opcode,host.in_instruction,completion,0,host.rte_internal_load,detailed_profile,false,outcome.evidence,restartable_read};
        if (host.log_accesses) host.accesses.push_back({kind, req.address, size, 0, false});
        stop(Exit::MemoryFault, 2, address, size, "bus read/fetch fault; access-error continuation not integrated");
    }
    if(!host.bus&&!host.precise_bus&&!host.detailed_bus)for (unsigned i = 0; i < size; ++i) value = (value << 8) | host.ram[req.address + i];
    if(size<4)value&=(uae_u32{1}<<(size*8))-1;
    if (host.log_accesses) host.accesses.push_back({kind, req.address, size, value, true});
    if(host.completed_frame_irq&&host.using_restart&&host.in_instruction&&regs.opcode==0x4e73&&
       kind==AccessKind::Read&&size==4&&address==regs.regs[15]+2)host.rte_header_pc=value;
    // Capability check at the real generated RTE's format-word read. No added
    // read, no modified instruction body and no hidden frame normalization.
    if (host.in_instruction && regs.opcode==0x4e73 && kind==AccessKind::Read && size==2 && address==regs.regs[15]+6) {
        const auto format=value>>12;
        if (format==3||(format==4&&!host.lc040_probes)||(format==7&&!host.using_restart))
            stop(Exit::Unsupported,0,address,2,"RTE FPU/access-error continuation profile not integrated");
        // The recognized format7 payload reloads internal state. A physical
        // bus error here halts (MC68040UM 8.2.1), without another frame. The
        // preceding SR/PC/format reads remain outside this bounded phase.
        host.rte_internal_load=format==7&&host.using_restart;
        select_rte_completion(regs.regs[15],uae_u16(value));
    }
    if(host.using_restart&&host.in_instruction&&regs.opcode==0x4e73&&kind==AccessKind::Read&&size==2&&address==regs.regs[15]+12) {
        if(value&0xc000)stop(Exit::Unsupported,0,address,2,"RTE FPU continuation requires integrated pending FPU state");
        // The synchronous integer producer never combines a completed CT
        // instruction with unfinished MOVEM execution sharing the EA field.
        if((value&0x3000)==0x3000)stop(Exit::Unsupported,0,address,2,"combined CT/CM continuation is outside the declared physical-bus profile");
        if(host.rte_completion)for(const auto&frame:host.completed_frames)if(frame.valid&&frame.id==host.rte_completion->id)host.rte_completion->ssw_loaded=value==frame.ssw;
    }
    return value;
}
void poke(uae_u32 address, unsigned size, uae_u32 value) {
    if (address >= host.ram.size() || size > host.ram.size() - address) throw std::out_of_range("runner RAM initialization");
    for (unsigned i = size; i > 0; --i) { host.ram[address + i - 1] = value & 255; value >>= 8; }
}
void write(uae_u32 address, unsigned size, uae_u32 value) {
    if (size < 4) value &= (uae_u32{1} << (size * 8)) - 1;
    if(!mmu_table_accessing&&(regs.tcr&0x8000)&&size>1) {
        const auto page_size=(regs.tcr&0x4000)?8192u:4096u;
        if((address&(page_size-1))+size>page_size) {
            for(unsigned i=0;i<size;++i)write(address+i,1,(value>>(8*(size-1-i)))&255);
            return;
        }
    }
    auto req=request(AccessKind::Write,address,size);req.address=translate_address(req,value);
    if(host.deferred_bitfield) {
        auto&tail=*host.deferred_bitfield;
        if(!tail.iu_write&&host.in_instruction&&host.using_restart&&!host.exception_depth&&!host.service_resuming&&
           regs.opcode==tail.primary.opcode&&(size==1||size==2||size==4)&&
           (req.function_code==1||req.function_code==5)&&!req.transfer_type) {
            // Captured at IU, before TTR/translation or any backend request.
            tail.iu_write=PendingIuWrite{req,value};return;
        }
        abort_bitfield_tail("bitfield tail exceeded its proved single unissued write; reset required");
    }
    observe_frame_write(req);
    const bool detailed_profile=host.detailed_bus!=nullptr;
    // Snapshot the actual producer before calling an external bus adapter.
    // No callback field can claim that the instruction has completed.
    const bool completed_producer=(detailed_profile||host.completed_frame_irq)&&host.in_instruction&&host.using_restart&&!mmu_restart&&
        !host.exception_depth&&!host.service_resuming;
    const bool bitfield_tail=host.bitfield_write_exceptions&&host.physical_write_exceptions&&host.deliver_exceptions&&detailed_profile&&
        host.in_instruction&&host.using_restart&&mmu_restart&&!host.exception_depth&&!host.service_resuming&&
        !mmu040_movem&&!host.physical_writeback&&(regs.s||!regs.m)&&bitfield_memory_writer(regs.opcode);
    bool ok = valid(AccessKind::Write, req.address, size);
    BusOutcome outcome{ok?BusCompletion::Completed:BusCompletion::FaultBeforeTransfer,{}};
    if(ok&&host.detailed_bus){
        auto supplied=value;
        outcome=host.detailed_bus(host.bus_opaque,req,supplied);
    }
    else if(ok&&host.precise_bus)outcome.completion=host.precise_bus(host.bus_opaque,req,value);
    else if(ok&&host.bus)outcome.completion=host.bus(host.bus_opaque,req,value)?BusCompletion::Completed:BusCompletion::UnknownFault;
    const auto completion=outcome.completion;
    ok=completion==BusCompletion::Completed;
    if (host.log_accesses) host.accesses.push_back({AccessKind::Write, req.address, size, value, ok});
    if (!ok) {
        host.bus_fault=BusFault{req,regs.opcode,host.in_instruction,completion,value,false,detailed_profile,completed_producer,outcome.evidence};
        if(bitfield_tail&&completion==BusCompletion::FaultBeforeTransfer&&outcome.evidence&&
           outcome.evidence->origin==FaultOrigin::ExternalBus&&outcome.evidence->whole_scalar_no_commit&&
           outcome.evidence->cpu_physical_address&&(size==1||size==2||size==4)&&
           (req.function_code==1||req.function_code==5)&&!req.transfer_type) {
            host.deferred_bitfield=DeferredBitfieldWrite{*host.bus_fault,{}};return;
        }
        stop(Exit::MemoryFault, 2, address, size, "bus write fault; access-error continuation not integrated");
    }
    if(!host.bus&&!host.precise_bus&&!host.detailed_bus)poke(req.address, size, value);
}
void reset() {
    regs = State{}; host = Host{};
    flush_atc();mmu_table_accessing=false;
    mmufixup[0]=mmufixup[1]=MmuFixup{};mmu_restart=true;mmu040_movem=0;mmu040_movem_ea=0;
    ismoves_nommu=false;
    mmu_ttr_enabled=mmu_ttr_enabled_ins=mmu_ttr_enabled_data=false;mmu_cache_state=0;
    last_addr_for_exception_3=last_fault_for_exception_3=last_op_for_exception_3=0;
    last_writeaccess_for_exception_3=last_notinstruction_for_exception_3=false;
    last_fc_for_exception_3=last_size_for_exception_3=last_di_for_exception_3=0;
    last_sr_for_exception3=0;
    for(auto &word:mmu040_move16) word=0;
    for (int i = 0; i < 256; ++i) {
        int bit = 0;
        while (bit < 8 && !(i & (1 << bit))) ++bit;
        movem_index1[i] = bit; movem_index2[i] = 7 - bit; movem_next[i] = i & ~(1 << bit);
    }
}
bool execute_mmu_control(uae_u16 opcode) {
    if((opcode&0xf000)!=0xf000)return false;
    if(!regs.s){Exception(8);return true;}
    if((opcode&0x0fe0)==0x0500) {
        const bool include_global=opcode&8;
        if(opcode&16)flush_atc();
        else flush_atc_address(regs.regs[8+(opcode&7)],bool(regs.dfc&4),include_global);
        regs.pc=regs.instruction_pc+2;return true;
    }
    if((opcode&0x0fd8)==0x0548) {
        const bool writing=(opcode&32)==0,supervisor=regs.dfc&4,data=(regs.dfc&3)!=2;
        const auto address=regs.regs[8+(opcode&7)];flush_atc_address(address,supervisor,true);
        const auto transparent=mmu_ttr_enabled?mmu_match_ttr(address,supervisor,data):0;
        if(transparent)regs.mmusr=transparent==1&&writing?0x800:3;
        else {
            const bool page8k=regs.tcr&0x4000;const auto mask=page8k?0x1fffu:0xfffu;
            const auto walked=mmu_walk_040(address,supervisor,writing,page8k,regs.urp,regs.srp,mmu_table_access,nullptr);
            regs.mmusr=(walked.valid?(walked.physical&~mask):0)|walked.status;
            const auto tag=((supervisor?0x80000000u:0)|(address>>1))&(page8k?0xffff0000u:0xffff8000u);
            const unsigned type=data?1:0,index=page8k?(address&0x1e000)>>13:(address&0xf000)>>12;
            auto&line=mmu_atc[type][index][mmu_atc_next[type]++&3];
            line={true,tag,walked.valid?walked.physical&~mask:0,walked.status};
        }
        regs.pc=regs.instruction_pc+2;return true;
    }
    return false;
}
bool cpuboard_fc_check(uae_u32 address,uae_u32 *,int size,bool writing) {
    // This is the platform seam in the real imported MOVES helpers. There is
    // no Amiga CPU board. An attached bus receives every FC explicitly;
    // flat RAM supports ordinary user/supervisor data/program spaces only.
    const auto fc=writing?regs.dfc:regs.sfc;
    if(!host.bus&&!host.precise_bus&&!host.detailed_bus&&fc!=1&&fc!=2&&fc!=5&&fc!=6)
        stop(Exit::Unsupported,0,address,1u<<size,"flat RAM has no mapping for this alternate function-code space");
    return false;
}
void checkint() { ++host.irq_checks; }
void mmu_set_super(bool) {}
void set_cpu_caches(bool) { ++host.cache_controls; }
uae_u32 mmu_set_tc(uae_u32 value) { flush_atc();mmu_flush_cache();return value&0xc000; }
void flush_icache(int) { ++host.cache_controls; }
void flush_cpu_caches_040_2(int cache,int scope,uae_u32 address,bool push,bool invalidate) {
    // This direct-fetch, synchronous-write interpreter has no resident lines
    // or pending writebacks. Cache maintenance therefore has an empty set;
    // record the real wrapper's parameters without inventing RAM/MMIO access.
    if(host.log_accesses)host.cache_operations.push_back({cache,scope,address,push,invalidate});
    ++host.cache_controls;
}
void write_log(const TCHAR *, ...) {}
bool cpureset() {
    if(!host.reset_hardware)stop(Exit::Reset,0,regs.pc,2,"RESET requires explicit platform hardware reset callback");
    if(!host.reset_hardware(host.reset_opaque))stop(Exit::Unsupported,0,regs.pc,2,"platform hardware reset did not complete");
    return false; // Atari RESET resets hardware, not the CPU PC/registers.
}
void m68k_setstopped(int type) { if (!regs.stopped) regs.stopped = type; }
void do_cycles_stop(int cycles) { host.stop_cycles += cycles; }
void resume_stopped() {
    if (regs.stopped == 1) regs.pc += 4;
    else if (regs.stopped == 2) regs.pc += 6;
    regs.stopped = 0;
}
void ExceptionX(int nr, uae_u32, uae_u32 oldpc) {
    if (oldpc!=0xffffffff) regs.instruction_pc=oldpc;
    enter_exception(nr,false,0);
}
void op_illg(uae_u32 opcode) { Exception((opcode & 0xf000)==0xa000?10:(opcode & 0xf000)==0xf000?11:4); }
void record_rte_trace(uaecptr pc) noexcept {host.rte_trace_pc=pc;host.rte_trace_pending=true;}
[[noreturn]] void unsupported_frame(int format) { stop(Exit::Unsupported,14,regs.pc,unsigned(format),"non-040 exception frame format excluded"); }
[[noreturn]] void unsupported_fpu_body() { stop(Exit::Unsupported,0,regs.pc,0,"active-FPU/arithmetic tail outside declared absent-LC040 probe profile"); }
bool interrupt_ready(unsigned level) noexcept {
    return level>=1&&level<=7&&!host.halted&&!host.service&&
        (!(regs.spcflags&SPCFLAG_MMURESTART)||host.completed_irq_ready)&&
        (level>unsigned(regs.intmask)||level==7);
}
bool inject_interrupt(int vector, unsigned level) {
    if (vector<0||vector>255||level<1||level>7) throw std::invalid_argument("invalid architectural interrupt request");
    if (!interrupt_ready(level)) return false;
    enter_exception(vector,true,level);
    return true;
}
namespace {
void private_service(ServiceKind kind) {
    if(host.service_resuming)return;
    invalidate_completed_frames();
    host.service=Service{kind,regs.opcode,regs.pc,regs.regs[15]+4,regs.s,regs.t1};
    stop(Exit::HostService,0,regs.pc,2,"ARAnyM private instruction awaits external host service");
}
}
void m68k_natfeat_id() { private_service(ServiceKind::NatFeatId); }
void m68k_natfeat_call() { private_service(ServiceKind::NatFeatCall); }
void m68k_emulop(uae_u32) { private_service(ServiceKind::EmulOp); }
void m68k_emulop_return() {
    private_service(ServiceKind::EmulOpReturn);
    stop(Exit::HostReturn,0,regs.pc,2,"ARAnyM nested EmulOp return");
}
Result complete_service() {
    if(!host.service)throw std::invalid_argument("no pending private host service");
    const auto service=*host.service;
    if(host.capability_latched||host.access_fault_latched||host.halted||(service.kind!=ServiceKind::LegacyFpu&&regs.pc!=service.pc))
        return {Exit::Unsupported,0,0,regs.instruction_pc,regs.pc,0,0,"cannot complete changed/unsafe private service context"};
    struct Resume {
        Resume(){host.service_resuming=host.in_instruction=true;}
        ~Resume(){host.service_resuming=host.in_instruction=false;host.service.reset();}
    } guard;
    try {
        // Private instructions resume at their unchanged side-effect-free helper.
        // The external FPU body already ran once and must never be replayed.
        if(service.kind!=ServiceKind::LegacyFpu)private_handlers[service.opcode](service.opcode);
        if((regs.spcflags&SPCFLAG_DOTRACE)||service.trace)Exception(9);
        return {Exit::Budget,0,0,regs.instruction_pc,regs.pc};
    } catch(const Stop &event) {
        if(event.result.exit==Exit::DoubleFault)host.halted=true;
        if(event.result.exit==Exit::MemoryFault)host.access_fault_latched=true;
        if(event.result.exit==Exit::Unsupported)host.capability_latched=true;
        return event.result;
    }
}
State architectural_state() {
    MakeSR();
    auto state = regs;
    if (!state.s) state.usp = state.regs[15];
    else if (state.m) state.msp = state.regs[15];
    else state.isp = state.regs[15];
    return state;
}
Result run(std::uint64_t budget) {
    std::uint64_t instructions = 0;
    host.completed_rte_event=false;
    try {
        if (host.halted) return {Exit::DoubleFault,0,0,regs.instruction_pc,regs.pc,0,0,"halted until reset"};
        if (host.capability_latched) return {Exit::Unsupported,0,0,regs.instruction_pc,regs.pc,0,0,"unsupported capability requires reset before continuation"};
        if (host.access_fault_latched) return {Exit::Unsupported,0,2,regs.instruction_pc,regs.pc,0,0,"access-fault restart requires reset; continuation not integrated"};
        if(host.service)return {Exit::HostService,0,0,regs.instruction_pc,regs.pc,0,0,"private service must complete before fetching another instruction"};
        if (regs.stopped) return {Exit::Stopped, 0};
        while (instructions < budget) {
            // A format7 RTE defers IRQ recognition until the restarted
            // instruction is entered, matching upstream MMURESTART handling.
            regs.spcflags&=~SPCFLAG_MMURESTART;
            host.completed_irq_ready=false;
            regs.instruction_pc = regs.pc;
            regs.trace_pc = regs.pc;
            host.start_pc=regs.pc;host.bus_fault.reset();host.mmu_fault.reset();host.using_restart=false;
            host.rte_header_pc.reset();host.rte_completion.reset();
            host.completed_rte_event=false;
            host.initial_ccr=(unsigned(regs.x)<<4)|(unsigned(regs.n)<<3)|(unsigned(regs.z)<<2)|(unsigned(regs.v)<<1)|unsigned(regs.c);
            host.initial_t1=regs.t1;
            mmu_restart=true;
            if (regs.pc & 1) {
                exception3_read_prefetch(0, regs.pc);
                continue;
            }
            regs.opcode = read(AccessKind::Fetch, regs.pc, 2);
            ++instructions;
            const bool modern_fpu=host.modern_fpu_operations&&legacy_fpu_opcodes[regs.opcode];
            if(!modern_fpu&&host.legacy_fpu_operations&&legacy_fpu_opcodes[regs.opcode]) {
                if(regs.t0||regs.t1||(regs.spcflags&(SPCFLAG_TRACE|SPCFLAG_DOTRACE)))
                    stop(Exit::Unsupported,0,regs.pc,2,"traced legacy FPU instruction boundary not integrated");
                private_service(ServiceKind::LegacyFpu);
            }
            if(!host.modern_fpu_operations&&requires_lc040_probes[regs.opcode]&&!host.lc040_probes)
                stop(Exit::Unsupported,0,regs.pc,2,"explicit LC040 probe profile required for recognized FPU opcodes");
            host.using_restart=host.fault_recovery&&restart_handlers[regs.opcode];
            const auto handler = host.using_restart?restart_handlers[regs.opcode]:handlers[regs.opcode];
            const auto private_handler=host.private_operations?private_handlers[regs.opcode]:nullptr;
            const bool mmu_control=(regs.opcode&0xf000)==0xf000&&
                ((regs.opcode&0x0fe0)==0x0500||(regs.opcode&0x0fd8)==0x0548);
            if (!modern_fpu&&!mmu_control&&!handler&&!private_handler) {
                if (!legal[regs.opcode]) { op_illg(regs.opcode); continue; }
                stop(Exit::Unsupported, 0, regs.pc, 2, "legal instruction excluded by current helper/profile inventory");
            }
            const auto trace_before = regs.t1;
            const auto frames_before=host.exception_count;
            struct Instruction {
                Instruction() { host.in_instruction=true; }
                ~Instruction() { host.in_instruction=host.rte_internal_load=false;host.rte_completion.reset(); }
            } instruction;
            if(modern_fpu) {
                if(!execute_modern_fpu(regs.opcode))
                    stop(Exit::Unsupported,0,regs.pc,2,"FPU instruction is outside the connected 68040 hardware subset");
            } else if(mmu_control) {
                if(!execute_mmu_control(regs.opcode))stop(Exit::UnsupportedMmu,0,regs.pc,2,"unrecognized 68040 MMU control instruction");
            } else if(private_handler)private_handler(regs.opcode);
            else handler(regs.opcode);
            if(host.deferred_bitfield) {
                // Only actual return from the pinned write-only tail proves
                // completion. The callback's original producer snapshot is
                // retained unchanged in bus_fault.
                const auto tail=*host.deferred_bitfield;host.deferred_bitfield.reset();
                const auto&fault=tail.primary;const auto&req=fault.request;
                const auto physical=*fault.evidence->cpu_physical_address;
                const auto sizebits=req.size==1?0x20:req.size==2?0x40:0;
                uae_u32 lanes=0;
                for(unsigned i=0;i<req.size;++i)lanes|=((fault.data>>(8*(req.size-1-i)))&255)<<(24-8*((physical+i)&3));
                regs.mmu_fault_addr=req.address;regs.mmu_effective_addr=0;regs.mmu_ssw=uae_u16(sizebits|req.function_code);
                regs.wb2_status=regs.wb3_status=0;regs.wb2_address=regs.wb3_data=0;
                if(tail.iu_write) {
                    const auto&pending=*tail.iu_write;const auto&queued=pending.request;
                    regs.wb3_status=uae_u8(0x80|(queued.size==1?0x20:queued.size==2?0x40:0)|queued.function_code);
                    regs.wb3_data=pending.data;
                }
                if(host.initial_t1||(regs.spcflags&SPCFLAG_DOTRACE)) {
                    regs.mmu_ssw|=0x2000;regs.mmu_effective_addr=host.start_pc;
                }
                for(auto&word:mmu040_move16)word=0;
                mmu_restart=false;regs.instruction_pc=regs.pc;host.recovery_pc=regs.pc;
                struct Delivery {
                    Delivery(PhysicalWriteback wb){host.delivering_fault=true;host.physical_writeback=wb;}
                    ~Delivery(){host.physical_writeback.reset();host.delivering_fault=false;}
                } delivery({uae_u16(0x80|sizebits|req.function_code),physical,lanes,tail.iu_write});
                CompletedDelivery completed(true); // Actual pinned body returned above.
                Exception(2);
                return {Exit::Budget,instructions,2,host.start_pc,regs.pc,req.address,req.size,"delivered completed bitfield tail with physical WB1 and optional unissued IU WB3"};
            }
            host.rte_internal_load=false; // Subsequent trace delivery is outside RTE's payload load.
            if(host.using_restart&&regs.opcode==0x4e73)complete_rte(frames_before);
            if(host.rte_trace_pending) {
                const auto traced_pc=host.rte_trace_pc;host.rte_trace_pending=false;
                // A synchronous RTE fault (e.g. odd return PC) has priority.
                // Otherwise normal Exception9 builds the manual's format2
                // at old-format7-SP+48, with the already-restored SR/PC/banks.
                if(host.exception_count==frames_before){
                    regs.trace_pc=traced_pc;Exception(9);
                    // CT's original instruction and pending trace are now
                    // complete. A pending IRQ enters before the first trace
                    // handler opcode; genuine read/CM restart holdoff remains.
                    regs.spcflags&=~SPCFLAG_MMURESTART;
                }
            }
            if (regs.spcflags & SPCFLAG_DOTRACE) Exception(9);
            else if (trace_before && host.exception_count==frames_before) { regs.spcflags |= SPCFLAG_DOTRACE; Exception(9); }
            if (regs.stopped) return {Exit::Stopped, instructions, 0, regs.instruction_pc, regs.pc};
        }
        return {Exit::Budget, instructions, 0, regs.instruction_pc, regs.pc};
    } catch (const Stop &event) {
        // Real MOVES helpers clear their transient flag only on normal return.
        // A host unwind must never leak alternate FC into the next boundary.
        ismoves_nommu=false;
        if(host.deferred_bitfield) {
            // A future/unexpected tail path must not acquire more bus effects
            // or turn an unfinished instruction into a completed frame.
            const auto primary=host.deferred_bitfield->primary;host.deferred_bitfield.reset();host.bus_fault=primary;
            host.service.reset();host.access_fault_latched=true;
            return {Exit::MemoryFault,instructions,2,host.start_pc,regs.pc,primary.request.address,primary.request.size,
                "bitfield write tail did not complete within its proved bounds; reset required"};
        }
        const auto terminal=[&](const Stop &stopped) {
            if(stopped.result.exit==Exit::MemoryFault)host.access_fault_latched=true;
            if(stopped.result.exit==Exit::DoubleFault)host.halted=true;
            if(stopped.result.exit==Exit::Unsupported)host.capability_latched=true;
            auto result=stopped.result;result.instructions=instructions;return result;
        };
        if(event.result.exit==Exit::MemoryFault&&host.mmu_fault&&host.fault_recovery&&host.deliver_exceptions) {
            const auto fault=*host.mmu_fault;host.mmu_fault.reset();const auto&req=fault.request;
            const auto sizebits=req.size==1?0x20:req.size==2?0x40:req.size==16?0x60:0;
            regs.mmu_fault_addr=req.address;regs.mmu_effective_addr=0;
            regs.mmu_ssw=uae_u16(0x400|sizebits|req.function_code|(req.kind==AccessKind::Write?0:0x100));
            regs.wb2_status=0;regs.wb2_address=0;regs.wb3_status=req.kind==AccessKind::Write?uae_u8(0x80|sizebits|req.function_code):0;
            regs.wb3_data=fault.data;for(auto&word:mmu040_move16)word=0;
            if(mmu040_movem){regs.mmu_ssw|=0x1000;regs.mmu_effective_addr=mmu040_movem_ea;mmu040_movem=0;}
            const auto ccr=host.initial_ccr;regs.x=ccr&16;regs.n=ccr&8;regs.z=ccr&4;regs.v=ccr&2;regs.c=ccr&1;
            regs.pc=regs.instruction_pc=host.start_pc;if(host.using_restart)cpu_restore_fixup();host.recovery_pc=host.start_pc;
            struct Delivery {Delivery(){host.delivering_fault=true;}~Delivery(){host.delivering_fault=false;}} delivery;
            try {
                Exception(2);
                return Result{Exit::Budget,instructions,2,host.start_pc,regs.pc,req.address,req.size,"delivered 68040 ATC translation/protection format7"};
            } catch(const Stop&nested){return terminal(nested);}
        }
        if(event.result.exit==Exit::MemoryFault&&host.bus_fault&&host.bus_fault->rte_internal_load) {
            auto halted=event;halted.result.exit=Exit::DoubleFault;
            halted.result.detail="physical bus error while RTE loads internal state; halted until reset";
            return terminal(halted);
        }
        if(event.result.exit==Exit::MemoryFault&&host.bus_fault&&host.bus_fault->detailed_profile) {
            const auto fault=*host.bus_fault;const auto&req=fault.request;
            const bool read_space=(req.kind==AccessKind::Read&&(req.function_code==1||req.function_code==5))||
                (req.kind==AccessKind::Fetch&&(req.function_code==2||req.function_code==6));
            if(host.physical_read_exceptions&&host.fault_recovery&&host.deliver_exceptions&&fault.restartable_read&&
               fault.completion==BusCompletion::FaultBeforeTransfer&&fault.evidence&&
               fault.evidence->origin==FaultOrigin::ExternalBus&&fault.evidence->whole_scalar_no_commit&&
               fault.evidence->cpu_physical_address&&read_space&&(req.size==1||req.size==2||req.size==4)&&
               !req.transfer_type&&!host.physical_writeback&&!host.service_resuming&&!regs.m&&
               !host.initial_t1&&!regs.t0&&!regs.t1&&!(regs.spcflags&(SPCFLAG_TRACE|SPCFLAG_DOTRACE))) {
                // Synchronous empty-cache profile: this read has no pending
                // writebacks. Restore only the genuine restart/fixup state;
                // the guest handler decides whether to repair and retry.
                regs.mmu_fault_addr=req.address;regs.mmu_effective_addr=0;
                regs.mmu_ssw=uae_u16(0x100|(req.size==1?0x20:req.size==2?0x40:0)|req.function_code);
                regs.wb2_status=regs.wb3_status=0;regs.wb2_address=regs.wb3_data=0;
                for(auto&word:mmu040_move16)word=0;
                if(mmu040_movem) {
                    regs.mmu_ssw|=0x1000;regs.mmu_effective_addr=mmu040_movem_ea;mmu040_movem=0;
                }
                const auto ccr=host.initial_ccr;
                regs.x=ccr&16;regs.n=ccr&8;regs.z=ccr&4;regs.v=ccr&2;regs.c=ccr&1;
                regs.pc=regs.instruction_pc=host.start_pc;
                cpu_restore_fixup();host.recovery_pc=regs.instruction_pc;
                struct Delivery {
                    Delivery(){host.delivering_fault=true;}
                    ~Delivery(){host.delivering_fault=false;}
                } delivery;
                try {
                    Exception(2);
                    return {Exit::Budget,instructions,2,host.start_pc,regs.pc,req.address,req.size,"delivered certified physical read/fetch format7"};
                } catch(const Stop&nested){return terminal(nested);}
            }
            if(host.physical_write_exceptions&&host.fault_recovery&&host.deliver_exceptions&&
               fault.in_instruction&&fault.completed_producer&&req.kind==AccessKind::Write&&
               fault.completion==BusCompletion::FaultBeforeTransfer&&fault.evidence&&
               fault.evidence->origin==FaultOrigin::ExternalBus&&fault.evidence->whole_scalar_no_commit&&
               fault.evidence->cpu_physical_address&&
               (req.size==1||req.size==2||req.size==4)&&(req.function_code==1||req.function_code==5)&&
               !req.transfer_type&&!regs.m&&!mmu040_movem&&!host.initial_t1&&!regs.t0&&!regs.t1&&
               !(regs.spcflags&(SPCFLAG_TRACE|SPCFLAG_DOTRACE))) {
                // The pinned completed producers each have one final scalar
                // write site. Completion is the CPU snapshot before callback,
                // never a host claim. This is not a multi-entry write queue.
                const auto physical=*fault.evidence->cpu_physical_address;
                const auto sizebits=req.size==1?0x20:req.size==2?0x40:0;
                uae_u32 lanes=0;
                for(unsigned i=0;i<req.size;++i) {
                    const auto byte=(fault.data>>(8*(req.size-1-i)))&255;
                    lanes|=byte<<(24-8*((physical+i)&3));
                }
                regs.mmu_fault_addr=req.address;regs.mmu_effective_addr=0;
                regs.mmu_ssw=uae_u16(sizebits|req.function_code);
                regs.wb2_status=regs.wb3_status=0;regs.wb2_address=regs.wb3_data=0;
                for(auto&word:mmu040_move16)word=0;
                host.recovery_pc=regs.instruction_pc;
                struct Delivery {
                    Delivery(PhysicalWriteback wb){host.delivering_fault=true;host.physical_writeback=wb;}
                    ~Delivery(){host.physical_writeback.reset();host.delivering_fault=false;}
                } delivery({uae_u16(0x80|sizebits|req.function_code),physical,lanes});
                CompletedDelivery completed(fault.completed_producer);
                try {
                    Exception(2);
                    return {Exit::Budget,instructions,2,host.start_pc,regs.pc,req.address,req.size,"delivered explicit completed-scalar physical WB1 format7"};
                } catch(const Stop&nested){return terminal(nested);}
            }
            // Per-request origin evidence must never silently fall back to
            // the legacy WB3 profile. Architectural delivery is a separate
            // opt-in above; all other detailed failures stop here.
            auto pending=event;
            pending.result.detail="detailed bus fault recorded; architectural continuation not integrated";
            return terminal(pending);
        }
        // Narrow firmware probe profile: these genuine pinned read-only bodies
        // perform the operand read before any flags/registers/PC change. There
        // are no pending writes in this synchronous empty-cache interpreter.
        // Full continuation/RTE7 remains blocked, including traced accesses.
        if(event.result.exit==Exit::MemoryFault&&!host.fault_recovery&&host.read_probe_exceptions&&host.deliver_exceptions&&host.bus_fault) {
            const auto fault=*host.bus_fault;const auto &req=fault.request;
            const bool tst_byte=(fault.opcode&0xfff8)==0x4a10&&req.size==1&&req.address==regs.regs[8+(fault.opcode&7)];
            // The observed SFP-004 port is wholly unmapped in the Atari
            // backend, so its word access fails before any device subread.
            // Other word-fault/partial-transfer contracts are not inferred.
            const bool move_word_abs=(fault.opcode&0xf1ff)==0x3038&&req.size==2&&req.address==0xfffffa44;
            if(fault.in_instruction&&(tst_byte||move_word_abs)&&req.kind==AccessKind::Read&&
               !req.alternate_function_code&&!req.transfer_type&&
               regs.pc==regs.instruction_pc&&!regs.t0&&!regs.t1&&!(regs.spcflags&(SPCFLAG_TRACE|SPCFLAG_DOTRACE))) {
                regs.mmu_fault_addr=req.address;regs.mmu_effective_addr=0;regs.mmu_ssw=uae_u16(0x0100|(tst_byte?0x20:0x40)|req.function_code);
                regs.wb2_status=regs.wb3_status=0;regs.wb2_address=regs.wb3_data=0;
                for(auto &word:mmu040_move16)word=0;
                host.read_probe_next_pc=regs.instruction_pc+(tst_byte?2:4);
                struct Delivery {Delivery(){host.delivering_read_probe=true;}~Delivery(){host.delivering_read_probe=false;}} delivery;
                try {
                    Exception(2);
                    return {Exit::Budget,instructions,2,event.result.instruction_pc,regs.pc,req.address,req.size,"delivered bounded read-only probe format7; RTE7 continuation unsupported"};
                } catch(const Stop &nested){return terminal(nested);}
            }
        }
        if(event.result.exit==Exit::MemoryFault&&host.fault_recovery&&host.deliver_exceptions&&host.bus_fault) {
            const auto fault=*host.bus_fault;const auto &req=fault.request;
            const auto family=fault.opcode&0xffc0,ea=fault.opcode&0x3f;
            const bool bitfield_writer=(family==0xeac0||family==0xecc0||family==0xeec0||family==0xefc0)&&
                ((ea>=0x10&&ea<=0x17)||(ea>=0x28&&ea<=0x39));
            if(fault.in_instruction&&host.using_restart&&req.kind==AccessKind::Write&&
               fault.completion==BusCompletion::FaultBeforeTransfer&&bitfield_writer) {
                // These imported helpers can write a prefix before failure.
                // Generic WB3 + original-PC retry can apply a bit operation
                // twice. Stop until full write completion is integrated; do
                // not invent another writeback or touch the failed operand.
                auto contained=event;
                contained.result.detail="bitfield write continuation is unsupported; unsafe WB3/retry withheld";
                return terminal(contained);
            }
            // The old bool callback, partial transfers and unintegrated bodies
            // retain the terminal contract. Never silently re-execute a bus op.
            if(fault.completion==BusCompletion::FaultBeforeTransfer&&
               (fault.in_instruction?host.using_restart:req.kind==AccessKind::Fetch)) {
                const bool writing=req.kind==AccessKind::Write;
                regs.mmu_fault_addr=req.address;regs.mmu_effective_addr=0;
                regs.mmu_ssw=uae_u16((req.size==1?0x20:req.size==2?0x40:0)|
                    (writing?0:0x100)|(req.transfer_type<<3)|req.function_code);
                regs.wb2_status=0;regs.wb2_address=0;
                regs.wb3_status=writing?uae_u8(0x80|(regs.mmu_ssw&0x7f)):0;
                regs.wb3_data=writing?fault.data:0;
                for(auto &word:mmu040_move16)word=0;
                if(mmu040_movem) {
                    regs.mmu_ssw|=0x1000;regs.mmu_effective_addr=mmu040_movem_ea;mmu040_movem=0;
                } else if(writing&&!mmu_restart&&(host.initial_t1||(regs.spcflags&SPCFLAG_DOTRACE)||
                    (regs.t0&&(fault.opcode&0xff00)==0x0e00&&(fault.opcode&0xc0)!=0xc0))) {
                    // This instruction completed; its queued write blocks a
                    // pending trace until guest WB3 handling and real RTE.
                    // All admitted MOVES bodies check T0 after their store;
                    // a failed completed store must retain that trace here.
                    // Size-field11 (CAS) is excluded from this MOVES class.
                    regs.mmu_ssw|=0x2000;regs.mmu_effective_addr=host.start_pc;
                }
                // Pinned newcpu.cpp:m68k_run_mmu040 restores flags/PC only
                // while mmu_restart remains true. Completed writes instead
                // stack the body's advanced instruction_pc and WB3 data.
                if(mmu_restart) {
                    const auto ccr=host.initial_ccr;
                    regs.x=ccr&16;regs.n=ccr&8;regs.z=ccr&4;regs.v=ccr&2;regs.c=ccr&1;
                    regs.pc=regs.instruction_pc=host.start_pc;
                }
                cpu_restore_fixup();host.recovery_pc=regs.instruction_pc;
                struct Delivery {Delivery(){host.delivering_fault=true;}~Delivery(){host.delivering_fault=false;}} delivery;
                CompletedDelivery completed(writing&&fault.completed_producer&&!(regs.mmu_ssw&0x1000));
                try {
                    Exception(2);
                    return {Exit::Budget,instructions,2,host.start_pc,regs.pc,req.address,req.size,"delivered precise physical-bus format7 access error"};
                } catch(const Stop &nested){return terminal(nested);}
            }
        }
        return terminal(event);
    }
}
const char *exit_name(Exit exit) {
    switch (exit) {
    case Exit::Budget: return "budget"; case Exit::Stopped: return "stopped";
    case Exit::GuestException: return "guest_exception"; case Exit::MemoryFault: return "memory_fault";
    case Exit::Unsupported: return "unsupported"; case Exit::UnsupportedMmu: return "unsupported_mmu";
    case Exit::Reset: return "reset"; case Exit::DoubleFault: return "double_fault";
    case Exit::HostService:return "host_service";case Exit::HostReturn:return "host_return";
    }
    return "invalid";
}
}
