/* SPDX-License-Identifier: GPL-2.0-or-later */
#pragma once
#include <cstdint>
namespace modern040 {
enum class MmuWalkFault { None,RootInvalid,PointerInvalid,PageInvalid,DoubleIndirect,
                          Supervisor,WriteProtect,TableBus };
struct MmuWalkResult {
    bool valid=false;
    std::uint32_t physical=0,status=0;
    MmuWalkFault fault=MmuWalkFault::None;
    std::uint32_t descriptor_address=0;
};
/* Physical descriptor callback. write=false reads value; write=true stores it.
 * It is always a supervisor CPU-space table access and never recursively
 * translated. False reports a physical bus failure at this exact descriptor. */
using MmuTableAccess=bool(*)(void*,std::uint32_t address,bool write,std::uint32_t&value);
MmuWalkResult mmu_walk_040(std::uint32_t logical,bool supervisor,bool write,bool page8k,
                           std::uint32_t urp,std::uint32_t srp,MmuTableAccess,void*);
}
