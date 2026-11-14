/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "mmu-walk.hpp"
#include <array>
#include <iostream>
#include <stdexcept>
#include <vector>
using namespace modern040;
namespace {
unsigned checks=0;void need(bool ok,const char*why){++checks;if(!ok)throw std::runtime_error(why);}
struct Memory {
    std::array<std::uint32_t,4096> words{};std::vector<std::pair<std::uint32_t,bool>> accesses;std::uint32_t fail=~0u;
    static bool access(void*opaque,std::uint32_t address,bool write,std::uint32_t&value){auto&m=*static_cast<Memory*>(opaque);m.accesses.push_back({address,write});if(address==m.fail)return false;if(address&3||address/4>=m.words.size())return false;if(write)m.words[address/4]=value;else value=m.words[address/4];return true;}
    void put(std::uint32_t address,std::uint32_t value){words[address/4]=value;}
};
struct Tables {Memory memory;std::uint32_t logical=0x12345678,root=0x400,pointer=0x800,page=0xc00,physical=0x5000;
    Tables(bool page8k=false){const auto r=root|((logical>>23)&0x1fc),p=pointer|((logical>>16)&0x1fc),q=page|(page8k?((logical>>11)&0x7c):((logical>>10)&0xfc));memory.put(r,pointer|2);memory.put(p,page|2);memory.put(q,physical|1);}
};
void normal(){Tables t;auto r=mmu_walk_040(t.logical,false,false,false,t.root,t.root,Memory::access,&t.memory);
    need(r.valid&&r.physical==0x5678,"4K walk combines page base and logical offset");
    need((t.memory.words[(t.root|((t.logical>>23)&0x1fc))/4]&8)!=0,"root descriptor USED is written");
    need((t.memory.words[(t.pointer|((t.logical>>16)&0x1fc))/4]&8)!=0,"pointer descriptor USED is written");
    const auto page_address=t.page|((t.logical>>10)&0xfc);need((t.memory.words[page_address/4]&8)!=0&&(t.memory.words[page_address/4]&16)==0,"read sets page USED but not MODIFIED");
    t.memory.words[page_address/4]&=~24u;r=mmu_walk_040(t.logical,false,true,false,t.root,t.root,Memory::access,&t.memory);
    need(r.valid&&(t.memory.words[page_address/4]&24)==24,"first write atomically installs USED and MODIFIED");
}
void protection(){Tables t;const auto page=t.page|((t.logical>>10)&0xfc);t.memory.words[page/4]|=4;
    auto r=mmu_walk_040(t.logical,false,true,false,t.root,t.root,Memory::access,&t.memory);need(!r.valid&&r.fault==MmuWalkFault::WriteProtect,"inherited/page write protection rejects writes");
    t.memory.words[page/4]=(t.physical|1|0x80);r=mmu_walk_040(t.logical,false,false,false,t.root,t.root,Memory::access,&t.memory);need(!r.valid&&r.fault==MmuWalkFault::Supervisor,"supervisor page rejects user read");
    r=mmu_walk_040(t.logical,true,false,false,t.root,t.root,Memory::access,&t.memory);need(r.valid,"supervisor page accepts supervisor read");
}
void indirect_and_8k(){Tables t;const auto page=t.page|((t.logical>>10)&0xfc);t.memory.put(page,0x1002);t.memory.put(0x1000,t.physical|1);
    auto r=mmu_walk_040(t.logical,false,false,false,t.root,t.root,Memory::access,&t.memory);need(r.valid&&r.descriptor_address==0x1000,"one indirect descriptor is followed and reported");
    t.memory.put(0x1000,0x1402);r=mmu_walk_040(t.logical,false,false,false,t.root,t.root,Memory::access,&t.memory);need(r.fault==MmuWalkFault::DoubleIndirect,"second indirect descriptor is rejected");
    Tables u(true);r=mmu_walk_040(u.logical,false,false,true,u.root,u.root,Memory::access,&u.memory);need(r.valid&&r.physical==(u.physical|(u.logical&0x1fff)),"8K walk uses 7-bit table and 13-bit page offset");
}
void failures(){Tables t;const auto pointer=t.pointer|((t.logical>>16)&0x1fc);t.memory.put(pointer,0);auto r=mmu_walk_040(t.logical,false,false,false,t.root,t.root,Memory::access,&t.memory);need(r.fault==MmuWalkFault::PointerInvalid,"invalid pointer descriptor is distinguished");
    Tables u;u.memory.fail=u.page|((u.logical>>10)&0xfc);r=mmu_walk_040(u.logical,false,false,false,u.root,u.root,Memory::access,&u.memory);need(r.fault==MmuWalkFault::TableBus&&r.status==0x800,"descriptor bus fault is distinguished with MMUSR B");
}
}
int main(){try{normal();protection();indirect_and_8k();failures();std::cout<<"PASS: "<<checks<<" pinned 68040 page-walk assertions\n";return 0;}catch(const std::exception&e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}}
