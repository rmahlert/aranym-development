/* SPDX-License-Identifier: GPL-2.0-or-later
 * MC68040UM PDF78-79 independent matching oracle. Protected execution remains
 * fail-closed until precise access-fault/RMW/continuation contracts are ready.
 */
#include "core.hpp"
#include <iostream>
#include <stdexcept>
#include <string_view>
using namespace modern040;
namespace {
unsigned checks=0;bool corrupt=false;
void expect(bool pass,const char *message){++checks;if(!pass)throw std::runtime_error(message);}
void matcher() {
    reset();
    for(unsigned base:{0u,0x55u,0xaau,0xffu})for(unsigned mask=0;mask<256;++mask)
    for(unsigned top:{0u,1u,0x54u,0x55u,0xaau,0xabu,0xfeu,0xffu})
    for(unsigned sf=0;sf<4;++sf)for(bool enabled:{false,true})for(bool wp:{false,true})
    for(bool supervisor:{false,true})for(bool data:{false,true}) {
        const auto ttr=(base<<24)|(mask<<16)|(sf<<13)|(enabled?0x8000:0)|(wp?4:0);
        regs.itt0=regs.itt1=regs.dtt0=regs.dtt1=0;
        (data?regs.dtt0:regs.itt0)=ttr;mmu_tt_modified();
        const bool match=enabled&&(sf>=2||supervisor==(sf==1))&&(((top^base)&(~mask&255))==0);
        const auto expected=match?(wp?1:2):0;
        expect(mmu_match_ttr((top<<24)|0x123456,supervisor,data)==(corrupt?(expected^1):expected),"manual-derived TTR base/mask/supervisor/enable/write-status match");
        expect(mmu_match_ttr((top<<24)|0x123456,supervisor,!data)==0,"instruction and data transparent banks stay independent");
    }
    reset();regs.dtt0=0x00ffc004;regs.dtt1=0x00ffc000;mmu_tt_modified();
    expect(mmu_match_ttr(0x12345678,true,true)==1,"overlapping TT0 status wins over TT1");
    regs.dtt0=0x00ffc000;regs.dtt1=0x00ffc004;mmu_tt_modified();
    expect(mmu_match_ttr(0x12345678,true,true)==2,"TT0 unprotected match wins over TT1 protected match");
    for(unsigned mode=0;mode<4;++mode) {
        regs.dtt0=0x00ffc000|(mode<<5);regs.dtt1=0;mmu_tt_modified();mmu_cache_state=0;
        expect(mmu_match_ttr(0x1234,true,true)==2&&mmu_cache_state==(mode&2?0x10u:0xc3u|(mode&1?0x20u:0)),"real source computes transparent cache attributes without pretending a cache exists");
    }
}
void execution_scope() {
    for(unsigned control=4;control<8;++control) {
        reset();regs.regs[0]=0x00ffe000;regs.sr=0x271f;MakeFromSR();
        poke(0x100,2,0x4e7b);poke(0x102,2,control);poke(0x104,2,0x4e7a);poke(0x106,2,0x1000|control);poke(0x108,2,0x4e71);
        expect(run(3).exit==Exit::Budget&&regs.pc==0x10a&&regs.regs[1]==0x00ffe000,"real MOVEC enables/reads unprotected transparent region then executes next opcode");
        expect(architectural_state().sr==0x271f,"transparent control update preserves allCCR");
    }
    for(bool instruction:{false,true}) {
        reset();(instruction?regs.itt0:regs.dtt0)=0x00ffc004;mmu_tt_modified();poke(0x100,2,0x4e71);
        expect(run(1).exit==Exit::UnsupportedMmu&&run(1).instructions==0&&host.accesses.empty(),"protected TTR profile fails before any guest fetch or side-effecting access");
    }
    reset();expect(!mmu_ttr_enabled&&!mmu_ttr_enabled_ins&&!mmu_ttr_enabled_data,"reset clears derived transparent-region enable state");
}
}
int main(int argc,char **argv){try{if(argc>2||(argc==2&&std::string_view(argv[1])!="--negative-match"))throw std::runtime_error("bad mode");corrupt=argc==2;install_handlers();matcher();execution_scope();std::cout<<"PASS: "<<checks<<" real transparent-matcher/unprotected-profile assertions\n";return 0;}catch(const std::exception &e){std::cerr<<"FAIL: "<<e.what()<<'\n';return corrupt&&std::string_view(e.what())=="manual-derived TTR base/mask/supervisor/enable/write-status match"?1:2;}}
