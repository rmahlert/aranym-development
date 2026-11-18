/* SPDX-License-Identifier: GPL-2.0-or-later
 * Mechanically bounded form of pinned WinUAE cpummu.cpp::mmu_fill_atc: the
 * 68040 root/pointer/page walk and descriptor U/M updates only.
 */
#include "mmu-walk.hpp"
namespace modern040 {
namespace {
constexpr std::uint32_t WP=4,USED=8,MODIFIED=16,SUPER=0x80;
constexpr std::uint32_t ROOT_MASK=0xfffffe00,PTR4_MASK=0xffffff00,PTR8_MASK=0xffffff80;
constexpr std::uint32_t PAGE4_MASK=0xfffff000,PAGE8_MASK=0xffffe000,INDIRECT_MASK=0xfffffffc;
constexpr std::uint32_t STATUS_MASK=0x7f5;
bool load(MmuTableAccess access,void*opaque,std::uint32_t address,std::uint32_t&value,MmuWalkResult&out) {
    if(access&&access(opaque,address,false,value))return true;
    out.fault=MmuWalkFault::TableBus;out.descriptor_address=address;out.status=0x800;return false;
}
bool store(MmuTableAccess access,void*opaque,std::uint32_t address,std::uint32_t value,MmuWalkResult&out) {
    if(access&&access(opaque,address,true,value))return true;
    out.fault=MmuWalkFault::TableBus;out.descriptor_address=address;out.status=0x800;return false;
}
}
MmuWalkResult mmu_walk_040(std::uint32_t logical,bool supervisor,bool writing,bool page8k,
                           std::uint32_t urp,std::uint32_t srp,MmuTableAccess access,void*opaque) {
    MmuWalkResult out;std::uint32_t descriptor=supervisor?srp:urp,wp=0,address=0;
    address=(descriptor&ROOT_MASK)|((logical>>23)&0x1fc);
    if(!load(access,opaque,address,descriptor,out))return out;
    if(!(descriptor&2)){out.fault=MmuWalkFault::RootInvalid;out.descriptor_address=address;return out;}
    wp|=descriptor;if(!(descriptor&USED)&&!store(access,opaque,address,descriptor|USED,out))return out;

    address=(descriptor&ROOT_MASK)|((logical>>16)&0x1fc);
    if(!load(access,opaque,address,descriptor,out))return out;
    if(!(descriptor&2)){out.fault=MmuWalkFault::PointerInvalid;out.descriptor_address=address;return out;}
    wp|=descriptor;if(!(descriptor&USED)&&!store(access,opaque,address,descriptor|USED,out))return out;

    address=(descriptor&(page8k?PTR8_MASK:PTR4_MASK))|
            (page8k?((logical>>11)&0x7c):((logical>>10)&0xfc));
    if(!load(access,opaque,address,descriptor,out))return out;
    if((descriptor&3)==2) {
        address=descriptor&INDIRECT_MASK;
        if(!load(access,opaque,address,descriptor,out))return out;
        if((descriptor&3)==2){out.fault=MmuWalkFault::DoubleIndirect;out.descriptor_address=address;return out;}
    }
    if(!(descriptor&1)){out.fault=MmuWalkFault::PageInvalid;out.descriptor_address=address;return out;}
    wp|=descriptor;
    if(writing) {
        if((wp&WP)||((descriptor&SUPER)&&!supervisor)) {
            if(!(descriptor&USED)){descriptor|=USED;if(!store(access,opaque,address,descriptor,out))return out;}
        } else if((descriptor&(USED|MODIFIED))!=(USED|MODIFIED)) {
            descriptor|=USED|MODIFIED;if(!store(access,opaque,address,descriptor,out))return out;
        }
    } else if(!(descriptor&USED)) {
        descriptor|=USED;if(!store(access,opaque,address,descriptor,out))return out;
    }
    descriptor|=wp&WP;out.descriptor_address=address;out.status=descriptor&STATUS_MASK;
    if(!supervisor&&(descriptor&SUPER)){out.fault=MmuWalkFault::Supervisor;return out;}
    if(writing&&(descriptor&WP)){out.fault=MmuWalkFault::WriteProtect;return out;}
    out.valid=true;const auto mask=page8k?PAGE8_MASK:PAGE4_MASK;
    out.physical=(descriptor&mask)|(logical&~mask);return out;
}
}
