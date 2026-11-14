/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "core.hpp"
#include "c-api.h"
#include "softfloat-adapter.hpp"
#include <iostream>
#include <stdexcept>
using namespace modern040;
namespace {
unsigned checks=0;
void need(bool value,const char*message){++checks;if(!value)throw std::runtime_error(message);}
void instruction(uae_u16 extra){poke(0x100,2,0xf200);poke(0x102,2,extra);}
void setup(){reset();install_handlers();host.modern_fpu_operations=true;host.log_accesses=true;}
void register_arithmetic(){
    setup();regs.fp[0]={0x3fff0000,0xc0000000,0};regs.fp[1]={0x40000000,0x90000000,0};
    instruction(uae_u16((1u<<10)|0x22));
    auto result=run(1);
    need(result.exit==Exit::Budget&&result.instructions==1&&regs.pc==0x104,"guest FADD completes at one instruction boundary");
    need(regs.fp[0]==Extended96{0x40000000,0xf0000000,0},"guest FADD installs exact 3.75 extended result");
    need(regs.fpiar==0x100&&regs.fpu_state==1,"guest arithmetic records instruction address and active FPU state");
    need((regs.fpsr&0x0f00ffffu)==0,"exact finite result clears condition/status/accrued fields");
    need(host.accesses.size()==2&&host.accesses[1].kind==AccessKind::Fetch&&host.accesses[1].address==0x102,"frontend fetches exactly one extension word");

    setup();regs.fp[0]={0x3fff0000,0x80000000,0};regs.fp[1]={0,0,0};
    instruction(uae_u16((1u<<10)|0x20));result=run(1);
    need(result.exit==Exit::Budget&&regs.fp[0]==Extended96{0x7fff0000,0,0},"guest FDIV by zero installs pinned canonical infinity");
    need((regs.fpsr&0x02000410u)==0x02000410u,"guest FDIV installs infinity, DZ status and accrued DZ");
}
void ea_sources(){
    setup();regs.fp[0]={0x3fff0000,0x80000000,0};regs.regs[0]=2;
    instruction(uae_u16(0x4000|0x22));auto result=run(1);
    need(result.exit==Exit::Budget&&regs.fp[0]==Extended96{0x40000000,0xc0000000,0},"signed long Dn source converts and adds exactly");

    setup();regs.regs[8]=0x300;poke(0x300,4,0x40000000);poke(0x304,4,0xa0000000);poke(0x308,4,0);
    poke(0x100,2,0xf210);poke(0x102,2,uae_u16(0x4000|(2u<<10)|0x00));result=run(1);
    need(result.exit==Exit::Budget&&regs.fp[0]==Extended96{0x40000000,0xa0000000,0},"extended memory source FMOVE preserves all 96 architectural bits");
    need(host.accesses.size()==5&&host.accesses[2].kind==AccessKind::Read&&host.accesses[4].address==0x308,"extended source performs three ordered data reads");

    setup();regs.fp[0]={0x3fff0000,0x80000000,0};poke(0x100,2,0xf23c);
    poke(0x102,2,uae_u16(0x4000|(1u<<10)|0x22));poke(0x104,4,0x3fc00000);result=run(1);
    need(result.exit==Exit::Budget&&regs.pc==0x108&&regs.fp[0]==Extended96{0x40000000,0xa0000000,0},"immediate IEEE single source advances PC and converts 1.5 exactly");

    setup();regs.fp[0]={0x3fff0000,0x80000000,0};regs.regs[8]=0x320;
    poke(0x320,4,0x40000000);poke(0x324,4,0);poke(0x100,2,0xf218);
    poke(0x102,2,uae_u16(0x4000|(5u<<10)|0x22));result=run(1);
    need(result.exit==Exit::Budget&&regs.regs[8]==0x328,"double (A0)+ source applies exact architectural postincrement");
    need(regs.fp[0]==Extended96{0x40000000,0xc0000000,0},"IEEE double memory source converts and adds exactly");
}
void destinations(){
    setup();regs.fp[2]={0x40000000,0xc0000000,0};regs.regs[0]=0xaaaaaaaa;
    poke(0x100,2,0xf200);poke(0x102,2,uae_u16(0x6000|(1u<<10)|(2u<<7)));auto result=run(1);
    need(result.exit==Exit::Budget&&regs.regs[0]==0x40400000,"FMOVE.S FP2,D0 converts exact extended three to IEEE single");

    setup();regs.fp[2]={0x40000000,0xc0000000,0};regs.regs[8]=0x300;
    poke(0x100,2,0xf218);poke(0x102,2,uae_u16(0x6000|(5u<<10)|(2u<<7)));result=run(1);
    need(result.exit==Exit::Budget&&regs.regs[8]==0x308,"FMOVE.D FP2,(A0)+ applies eight-byte postincrement");
    need(read(AccessKind::Read,0x300,4)==0x40080000&&read(AccessKind::Read,0x304,4)==0,"FMOVE.D emits exact IEEE double words in big-endian order");

    setup();regs.fp[5]={0x3fff0000,0x80000000,0x12345678};regs.regs[8]=0x320;
    poke(0x100,2,0xf210);poke(0x102,2,uae_u16(0x6000|(2u<<10)|(5u<<7)));result=run(1);
    need(result.exit==Exit::Budget&&read(AccessKind::Read,0x320,4)==0x3fff0000&&read(AccessKind::Read,0x324,4)==0x80000000&&read(AccessKind::Read,0x328,4)==0x12345678,"FMOVE.X writes all three external-format words in order");

    setup();regs.fp[7]={0x40010000,0x87654321,0xabcdef01};regs.regs[8]=0x340;
    poke(0x100,2,0xf210);poke(0x102,2,0xe080);result=run(1);
    need(result.exit==Exit::Budget&&read(AccessKind::Read,0x340,4)==0xabcdef01&&read(AccessKind::Read,0x344,4)==0x87654321&&read(AccessKind::Read,0x348,4)==0x40010000,"68040 static-pred FMOVEM FP7,(A0) preserves documented reversed write order");

    setup();regs.regs[8]=0x340;poke(0x340,4,0x3fff0000);poke(0x344,4,0x80000000);poke(0x348,4,0x10203040);
    poke(0x100,2,0xf218);poke(0x102,2,0xd080);result=run(1);
    need(result.exit==Exit::Budget&&regs.regs[8]==0x34c&&regs.fp[0]==Extended96{0x3fff0000,0x80000000,0x10203040},"static-postincrement FMOVEM (A0)+,FP0 loads canonical extended value and advances by12");
}
void compare_and_guards(){
    setup();regs.fp[0]={0x3fff0000,0x80000000,0};regs.fp[1]={0x40000000,0x80000000,0};
    const auto before=regs.fp[0];instruction(uae_u16((1u<<10)|0x38));auto result=run(1);
    need(result.exit==Exit::Budget&&regs.fp[0]==before,"FCMP updates status without writing destination register");
    need((regs.fpsr&0x08000000u)!=0,"FCMP of one minus two sets negative condition");

    setup();regs.fp[1]={0,0,0};instruction(uae_u16((1u<<10)|0x0e));result=run(1);
    need(result.exit==Exit::Budget&&regs.fp[0]==Extended96{0,0,0},"FSIN software completion uses pinned SoftFloat instead of aborting the emulator");

    setup();regs.fp[0]={0x40010000,0xa0000000,0};regs.fp[1]={0x40000000,0xc0000000,0};
    instruction(uae_u16((1u<<10)|0x21));result=run(1);
    need(result.exit==Exit::Budget&&regs.fp[0]==Extended96{0x40000000,0x80000000,0},"FMOD software completion returns exact remainder");
    need((regs.fpsr&0x00ff0000u)==0x00010000u,"FMOD exports quotient magnitude and sign byte");

    setup();regs.fpcr=0x0400;regs.fp[0]={0x3fff0000,0x80000000,0};regs.fp[1]={0,0,0};
    const auto destination=regs.fp[0];instruction(uae_u16((1u<<10)|0x20));result=run(1);
    need(result.exit==Exit::GuestException&&result.vector==50,"enabled divide-by-zero delivers architected FPU vector50");
    need(regs.fp[0]==destination&&regs.fpiar==0x100,"enabled arithmetic exception preserves destination and records FPIAR");

    setup();regs.fpcr=0x8000;regs.fpsr=0x01000000;poke(0x100,2,0xf290);poke(0x102,2,4);result=run(1);
    need(result.exit==Exit::GuestException&&result.vector==48,"enabled BSUN condition delivers architected FPU vector48");
}
void control_registers(){
    setup();regs.regs[2]=0x1234;poke(0x100,2,0xf202);poke(0x102,2,0x9000);
    auto result=run(1);need(result.exit==Exit::Budget&&regs.fpcr==0x1234&&regs.pc==0x104,"FMOVE.L Dn,FPCR installs masked control word");
    regs.pc=regs.instruction_pc=0x100;regs.regs[2]=0;poke(0x102,2,0xb000);result=run(1);
    need(result.exit==Exit::Budget&&regs.regs[2]==0x1234,"FMOVE.L FPCR,Dn returns exact control word");

    setup();regs.fpsr=0x0a55c318;poke(0x100,2,0xf203);poke(0x102,2,0xa800);result=run(1);
    need(result.exit==Exit::Budget&&regs.regs[3]==0x0a55c318,"FMOVE.L FPSR,Dn preserves condition, quotient, status and accrued bytes");

    setup();regs.regs[9]=0xdeadbeef;poke(0x100,2,0xf209);poke(0x102,2,0x8400);result=run(1);
    need(result.exit==Exit::Budget&&regs.fpiar==0xdeadbeef,"FMOVE.L An,FPIAR supports the architected address-register form");
    regs.pc=regs.instruction_pc=0x100;regs.regs[9]=0;poke(0x102,2,0xa400);result=run(1);
    need(result.exit==Exit::Budget&&regs.regs[9]==0xdeadbeef,"FMOVE.L FPIAR,An round trips exactly");

    setup();regs.fpcr=0x1234;regs.fpsr=0x56789abc;regs.fpiar=0xdef01234;regs.regs[8]=0x30c;
    poke(0x100,2,0xf220);poke(0x102,2,0xbc00);result=run(1);
    need(result.exit==Exit::Budget&&regs.regs[8]==0x300,"FMOVEM control,-(A0) predecrements by all selected registers");
    need(read(AccessKind::Read,0x300,4)==0x1234&&read(AccessKind::Read,0x304,4)==0x56789abc&&read(AccessKind::Read,0x308,4)==0xdef01234,"control registers store in FPCR/FPSR/FPIAR order");
    regs.pc=regs.instruction_pc=0x100;regs.fpcr=regs.fpsr=regs.fpiar=0;regs.regs[8]=0x300;
    poke(0x100,2,0xf218);poke(0x102,2,0x9c00);result=run(1);
    need(result.exit==Exit::Budget&&regs.regs[8]==0x30c&&regs.fpcr==0x1234&&regs.fpsr==0x56789abc&&regs.fpiar==0xdef01234,"FMOVEM (A0)+,control restores all registers and postincrements");

    setup();poke(0x100,2,0xf23c);poke(0x102,2,0x9000);poke(0x104,4,0x87654321);result=run(1);
    need(result.exit==Exit::Budget&&regs.fpcr==0x4321&&regs.pc==0x108,"immediate control move consumes long and applies FPCR mask");
}
void branches(){
    setup();regs.fpsr=0x04000000;poke(0x100,2,0xf281);poke(0x102,2,6);
    auto result=run(1);need(result.exit==Exit::Budget&&regs.pc==0x108,"word FBEQ branches relative to the displacement word address");
    need(regs.fpiar==0x100,"taken floating branch records its instruction address");
    setup();regs.fpsr=0;poke(0x100,2,0xf281);poke(0x102,2,6);result=run(1);
    need(result.exit==Exit::Budget&&regs.pc==0x104,"untaken word FBEQ advances past its displacement");
    setup();regs.fpsr=0;poke(0x100,2,0xf2cf);poke(0x102,4,0x20);result=run(1);
    need(result.exit==Exit::Budget&&regs.pc==0x122,"long FBT uses signed 32-bit displacement and opcode+2 base");
    setup();regs.fpsr=0x01000000;poke(0x100,2,0xf290);poke(0x102,2,4);result=run(1);
    need(result.exit==Exit::Budget&&(regs.fpsr&0x00008080)==0x00008080,"signaling condition on NaN accrues BSUN when disabled");

    setup();regs.fpsr=0x04000000;regs.regs[2]=0x12345678;poke(0x100,2,0xf242);poke(0x102,2,1);result=run(1);
    need(result.exit==Exit::Budget&&regs.regs[2]==0x123456ff,"FSEQ writes only the low data-register byte");
    setup();regs.fpsr=0;regs.regs[8]=0x300;poke(0x100,2,0xf258);poke(0x102,2,15);result=run(1);
    need(result.exit==Exit::Budget&&regs.regs[8]==0x301&&read(AccessKind::Read,0x300,1)==0xff,"FST (A0)+ writes condition byte and increments once");

    setup();regs.regs[3]=2;poke(0x100,2,0xf24b);poke(0x102,2,0);poke(0x104,2,0xfffc);result=run(1);
    need(result.exit==Exit::Budget&&regs.pc==0x100&&(regs.regs[3]&0xffff)==1,"FDBF decrements and branches from displacement-word base");
    setup();regs.fpsr=0x04000000;regs.regs[3]=2;poke(0x100,2,0xf24b);poke(0x102,2,1);poke(0x104,2,0xfffc);result=run(1);
    need(result.exit==Exit::Budget&&regs.pc==0x106&&(regs.regs[3]&0xffff)==2,"true FDBcc condition neither decrements nor branches");

    setup();poke(0x100,2,0xf27c);poke(0x102,2,0);result=run(1);
    need(result.exit==Exit::Budget&&regs.pc==0x104,"false FTRAPcc completes without exception");
    setup();poke(0x100,2,0xf27c);poke(0x102,2,15);result=run(1);
    need(result.exit==Exit::GuestException&&result.vector==7,"true FTRAPcc raises architected trap vector7");
}
void save_restore(){
    setup();regs.fpu_state=1;regs.regs[8]=0x304;poke(0x100,2,0xf320);
    auto result=run(1);need(result.exit==Exit::Budget&&regs.regs[8]==0x300&&read(AccessKind::Read,0x300,4)==0x41000000,"68040 FSAVE -(A0) writes revision41 idle frame and predecrements");
    setup();regs.fpu_state=0;regs.regs[8]=0x300;poke(0x100,2,0xf310);result=run(1);
    need(result.exit==Exit::Budget&&read(AccessKind::Read,0x300,4)==0,"68040 FSAVE writes four-byte null frame");

    setup();regs.fpcr=0x1234;regs.fpsr=0x5678;regs.fpiar=0x9abc;regs.regs[8]=0x300;poke(0x300,4,0);poke(0x100,2,0xf358);result=run(1);
    need(result.exit==Exit::Budget&&regs.regs[8]==0x304&&regs.fpu_state==0,"FRESTORE (A0)+ consumes null frame and postincrements");
    need(regs.fpcr==0&&regs.fpsr==0&&regs.fpiar==0&&regs.fp[0]==Extended96{0x7fff0000,0xffffffff,0xffffffff},"null restore resets control and canonical FP register state");
    setup();regs.regs[8]=0x300;poke(0x300,4,0x41000000);poke(0x100,2,0xf350);result=run(1);
    need(result.exit==Exit::Budget&&regs.fpu_state==1,"FRESTORE accepts revision41 idle frame");

    setup();regs.sr=0;MakeFromSR();poke(0x100,2,0xf310);result=run(1);
    need(result.exit==Exit::GuestException&&result.vector==8,"FSAVE remains supervisor privileged in native frontend");
}
void public_selection(){
    const m040_options options{M040_ABI_VERSION,0,0,0};
    need(m040_reset(&options)==M040_OK&&m040_modern_fpu_operations(1)==M040_OK,"public API selects native modern FPU frontend");
    need(m040_legacy_fpu_operations(1)==M040_BAD_ARGUMENT,"legacy and modern FPU engines are mutually exclusive");
    need(m040_modern_fpu_operations(0)==M040_OK&&m040_legacy_fpu_operations(1)==M040_OK,"engine can be changed only through explicit selection");
    need(m040_modern_fpu_operations(1)==M040_BAD_ARGUMENT,"modern engine rejects simultaneous legacy service mode");
}
}
int main(){try{register_arithmetic();ea_sources();destinations();compare_and_guards();control_registers();branches();save_restore();public_selection();std::cout<<"PASS: "<<checks<<" guest 68040 FPU frontend assertions\n";return 0;}catch(const std::exception&e){std::cerr<<"FAIL: "<<e.what()<<'\n';return 1;}}
