/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "softfloat-adapter.hpp"
#include <cstdlib>
#include <iomanip>
#include <iostream>
namespace {
unsigned checks=0;
void need(bool ok,const char*message){++checks;if(!ok){std::cerr<<"FAIL SoftFloat: "<<message<<'\n';std::exit(2);}}
}
int main(){
    using namespace modern040;
    const Extended96 one{0x3fff0000,0x80000000,0};
    const Extended96 one_half{0x3fff0000,0xc0000000,0};
    const Extended96 two_quarter{0x40000000,0x90000000,0};
    const Extended96 zero{0,0,0};
    need(softfloat_binary(FpuBinary::Add,one_half,two_quarter,0).words==Extended96{0x40000000,0xf0000000,0},"1.5 + 2.25 = 3.75 exactly");
    need(softfloat_binary(FpuBinary::Sub,two_quarter,one_half,0).words==Extended96{0x3ffe0000,0xc0000000,0},"2.25 - 1.5 = 0.75 exactly");
    need(softfloat_binary(FpuBinary::Mul,one_half,two_quarter,0).words==Extended96{0x40000000,0xd8000000,0},"1.5 * 2.25 = 3.375 exactly");
    need(softfloat_sqrt(one,0).words==one,"sqrt(1) is exact");
    const auto divided=softfloat_binary(FpuBinary::Div,one,zero,0);
    if(!(divided.words[0]==0x7fff0000&&divided.words[1]==0&&divided.words[2]==0))
        std::cerr<<"division result "<<std::hex<<divided.words[0]<<':'<<divided.words[1]<<':'<<divided.words[2]<<" status="<<divided.status<<std::dec<<'\n';
    need(divided.words[0]==0x7fff0000&&divided.words[1]==0&&divided.words[2]==0,"division by zero produces WinUAE canonical infinity");
    need((divided.status&0x00000400)!=0,"division by zero reports FPSR DZ");
    const Extended96 payload_nan{0x7fff0000,0xc1234567,0x89abcdef};
    const auto propagated=softfloat_binary(FpuBinary::Add,payload_nan,one,0);
    need((propagated.words[0]&0x7fff0000)==0x7fff0000&&
         ((propagated.words[1]&0x7fffffff)||propagated.words[2]),"NaN remains NaN through arithmetic");
    std::cout<<"PASS: "<<checks<<" pinned WinUAE SoftFloat arithmetic/status assertions\n";
}
