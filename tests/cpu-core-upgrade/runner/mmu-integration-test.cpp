/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "core.hpp"
#include "restart.hpp"
#include <iostream>
#include <stdexcept>
using namespace modern040;
namespace {
unsigned checks=0;void need(bool ok,const char*why){++checks;if(!ok)throw std::runtime_error(why);}
uae_u32 raw(uae_u32 address){uae_u32 value=0;for(unsigned i=0;i<4;++i)value=(value<<8)|host.ram[address+i];return value;}
struct Mapping {uae_u32 root=0x400,pointer=0x800,page=0xc00,physical=0x2000;
    void install(uae_u32 logical=0,bool wp=false,bool supervisor=false){poke(root|((logical>>23)&0x1fc),4,pointer|2);poke(pointer|((logical>>16)&0x1fc),4,page|2);poke(page|((logical>>10)&0xfc),4,physical|(wp?4:0)|(supervisor?0x80:0)|1);}
};
void setup(Mapping&m){reset();install_handlers();m.install();regs.srp=m.root;regs.urp=m.root;regs.tcr=0x8000;host.log_accesses=true;}
void fetch_data_write(){Mapping m;setup(m);poke(0x2100,2,0x7007);auto result=run(1);
    need(result.exit==Exit::Budget&&regs.regs[0]==7&&regs.pc==0x102,"translated instruction fetch executes genuine handler");
    need(!host.accesses.empty()&&host.accesses.back().kind==AccessKind::Fetch&&host.accesses.back().address==0x2100,"backend receives translated physical fetch address");

    setup(m);poke(0x2100,2,0x2010);poke(0x2180,4,0x12345678);regs.regs[8]=0x180;result=run(1);
    need(result.exit==Exit::Budget&&regs.regs[0]==0x12345678,"translated data read reaches mapped physical page");
    setup(m);poke(0x2100,2,0x2080);regs.regs[0]=0x89abcdef;regs.regs[8]=0x180;result=run(1);
    need(result.exit==Exit::Budget&&read(AccessKind::Read,0x180,4)==0x89abcdef,"translated guest write updates mapped physical page");
    const auto descriptor=m.page;need((raw(descriptor)&24)==24,"successful translated write records descriptor USED and MODIFIED");
}
void roots_and_faults(){Mapping m;setup(m);const auto page=m.page;poke(page,4,m.physical|5);poke(0x2100,2,0x2080);regs.regs[8]=0x180;
    auto result=run(1);need(result.exit==Exit::MemoryFault&&result.address==0x180&&regs.mmu_fault_addr==0x180,"write-protected page reports logical fault address");
    need((raw(page)&8)!=0&&(raw(page)&16)==0,"protection fault sets USED but not MODIFIED");

    reset();install_handlers();Mapping user;user.root=0x400;user.pointer=0x800;user.page=0xc00;user.physical=0x2000;user.install();
    Mapping super;super.root=0x1000;super.pointer=0x1400;super.page=0x1800;super.physical=0x3000;super.install();
    regs.urp=user.root;regs.srp=super.root;regs.sr=0;MakeFromSR();regs.tcr=0x8000;poke(0x2100,2,0x7009);poke(0x3100,2,0x7003);
    result=run(1);need(result.exit==Exit::Budget&&regs.regs[0]==9,"user fetch selects URP instead of SRP");
}
void transparent(){Mapping m;setup(m);regs.itt0=0x00ffc000;mmu_tt_modified();poke(0x100,2,0x7005);auto result=run(1);
    need(result.exit==Exit::Budget&&regs.regs[0]==5,"matching instruction TTR bypasses enabled page tables");
}
void atc(){Mapping m;setup(m);poke(0x2100,2,0x7001);poke(0x2102,2,0x4e71);poke(0x3102,2,0x7002);auto result=run(1);
    need(result.exit==Exit::Budget&&regs.regs[0]==1,"initial translation fills instruction ATC");
    poke(m.page,4,0x3001);result=run(1);
    need(result.exit==Exit::Budget&&regs.regs[0]==1,"ATC hit retains cached page after descriptor changes");
    regs.pc=regs.instruction_pc=0x102;regs.tcr=mmu_set_tc(regs.tcr);result=run(1);
    need(result.exit==Exit::Budget&&regs.regs[0]==2,"TC reconfiguration flushes ATC and observes changed descriptor");

    setup(m);poke(0x2100,2,0x7001);poke(0x2102,2,0xf518);poke(0x3104,2,0x7004);result=run(1);
    poke(m.page,4,0x3001);result=run(1);need(result.exit==Exit::Budget&&regs.pc==0x104,"guest PFLUSHA executes from the old cached mapping");
    result=run(1);need(result.exit==Exit::Budget&&regs.regs[0]==4,"guest PFLUSHA invalidates ATC before its successor fetch");

    setup(m);poke(0x2100,2,0xf568);regs.regs[8]=0x180;regs.dfc=1;result=run(1);
    need(result.exit==Exit::Budget&&(regs.mmusr&1)&&(regs.mmusr&0xfffff000)==0x2000,"guest PTESTR walks selected DFC space and reports physical page/status");
}
void format7_restart(){Mapping m;setup(m);install_restart_handlers();host.deliver_exceptions=host.fault_recovery=true;
    regs.regs[15]=regs.isp=0x900;regs.regs[8]=0x1180;poke(0x2100,2,0x2010);poke(0x2008,4,0x200);poke(0x2200,2,0xf518);poke(0x2202,2,0x4e73);
    auto result=run(1);need(result.exit==Exit::Budget&&result.vector==2&&regs.pc==0x200,"ATC page fault delivers guest format7 handler");
    need(regs.mmu_fault_addr==0x1180&&(regs.mmu_ssw&0x507)==0x505&&host.last_frame&&host.last_frame->format==7,"format7 records logical address plus ATC/RW/supervisor-data SSW");
    const auto frame_stack=regs.regs[15];poke(m.page+4,4,0x3001);poke(0x3180,4,0x55667788);
    result=run(1);need(result.exit==Exit::Budget&&regs.pc==0x202,"guest PFLUSHA removes the 68040 cached invalid ATC entry");
    result=run(1);need(result.exit==Exit::Budget&&regs.pc==0x100&&regs.regs[15]==0x900,"genuine restart-aware RTE consumes delivered format7 frame");
    need(frame_stack==0x8c4,"format7 frame occupies exact 60-byte 68040 layout");
    result=run(1);need(result.exit==Exit::Budget&&regs.regs[0]==0x55667788&&regs.pc==0x102,"restarted translated instruction completes after handler repairs page table");
}
void crossing(){Mapping m;setup(m);poke(m.page+4,4,0x4001);poke(0x2ffe,2,0x1122);poke(0x4000,2,0x3344);
    need(read(AccessKind::Read,0xffe,4)==0x11223344,"cross-page long read translates each page and rejoins big-endian bytes");
    write(0xffe,4,0xa1b2c3d4);need(host.ram[0x2ffe]==0xa1&&host.ram[0x2fff]==0xb2&&host.ram[0x4000]==0xc3&&host.ram[0x4001]==0xd4,"cross-page long write reaches both discontiguous physical pages in order");
}
}
int main(){try{fetch_data_write();roots_and_faults();transparent();atc();format7_restart();crossing();std::cout<<"PASS: "<<checks<<" integrated 68040 MMU fetch/data/fault assertions\n";return 0;}catch(const std::exception&e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}}
