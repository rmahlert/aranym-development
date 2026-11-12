/* SPDX-License-Identifier: GPL-2.0-or-later
 * Literal span, ordering and permission vectors, not a register-transfer model.
 */
#include "../../src/modern040/native-ea-plan.hpp"
#include <algorithm>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string_view>
#include <tuple>
#include <vector>
using modern040::native_ea::Image;
using Request=std::tuple<std::uint32_t,unsigned,bool>;
struct Memory {
    std::vector<Request>permits;std::vector<std::uint32_t>peeks;
    std::map<std::uint32_t,std::uint32_t>pointers{{0x1ffd4,0x30000},{0xfd8,0x3abc0}};
    std::uint32_t denied_read=0xdeadbeef,denied_write=0xdeadbeef;
    bool allowed(std::uint32_t a,unsigned n,bool w){
        permits.emplace_back(a,n,w);const auto end=std::uint64_t(a)+n;
        if(end>0x100000000ULL)return false;
        const auto denied=w?denied_write:denied_read;return denied<a||denied>=end;
    }
    bool peek32(std::uint32_t a,std::uint32_t&v){peeks.push_back(a);auto p=pointers.find(a);if(p==pointers.end())return false;v=p->second;return true;}
    bool saw(std::uint32_t a,unsigned n,bool w){return std::find(permits.begin(),permits.end(),Request(a,n,w))!=permits.end();}
};
unsigned checks=0;
void expect(bool yes,const char*s){++checks;if(!yes)throw std::runtime_error(s);}
Image image(std::initializer_list<std::uint16_t>w){Image i;i.pc=0x1000;i.count=w.size();std::copy(w.begin(),w.end(),i.words.begin());return i;}
struct Case{Image code;std::uint32_t address;unsigned span;bool write;unsigned pointers;};
int main(int argc,char**argv){try{
    const std::string_view option=argc==2?argv[1]:"";
    if(argc>2||(!option.empty()&&option!="--negative-span"&&option!="--negative-pc-base"&&option!="--negative-last-permission"))return 2;
    std::array<std::uint32_t,16>r{};r[8]=0x20000;r[1]=0xfffffff9;r[15]=0x4000;const auto before=r;
    const std::vector<Case>cases{
        {image({0x4890,0xffff}),0x20000,32,true,0},
        {image({0x48d0,0x8001}),0x20000,8,true,0},
        {image({0x48e7,0xffff}),0x3fc0,64,true,0},
        {image({0x48a7,0x8421}),0x3ff8,8,true,0},
        {image({0x4cd8,0xffff}),0x20000,64,false,0},
        {image({0x4c98,0x0101}),0x20000,4,false,0},
        {image({0x4cba,3,0xfffa}),0xffe,4,false,0},
        {image({0x48f0,0x0301,0x1b21,0xffe2}),0x30000,12,true,1},
        {image({0x4cfb,0x0301,0x1b21,0xffe2}),0x3abc0,12,false,1},
        {image({0x48f8,0x8001,0x8000}),0xffff8000,8,true,0},
        {image({0x4cf9,0xffff,0x1234,0x5678}),0x12345678,64,false,0}
    };
    for(const auto&c:cases){Memory m;
        expect(modern040::native_ea::movem_allowed(c.code,r,m),"MOVEM literal full-span preflight");
        const auto span=option=="--negative-span"&&c.span==32?16u:c.span;
        const auto address=option=="--negative-pc-base"&&c.address==0xffe?0xffcu:c.address;
        expect(m.saw(address,span,c.write),"MOVEM validates full population-count span with mask before PC extension");
        expect(!m.saw(c.address,c.span,!c.write),"MOVEM checks only actual transfer direction");
        expect(m.peeks.size()==c.pointers,"MOVEM peeks only checked indirect RAM pointers");
    }
    for(auto op:{0x4880u,0x48c0u,0x4888u,0x48d8u,0x4ca0u,0x48bau,0x48bbu,0x4cbcu})
        expect(!modern040::native_ea::multiple_memory_policy(op).size,"invalid MOVEM directions/register/PC-destination forms excluded");
    {Memory m;m.denied_write=0x3fff;
        expect(modern040::native_ea::movem_allowed(image({0x48e7,0xffff}),r,m)==(option=="--negative-last-permission"),"last byte of MOVEM store span denied before first transfer");
        expect(m.peeks.empty(),"denied simple MOVEM store performs no memory read");}
    {Memory m;m.denied_read=0x2003f;
        expect(!modern040::native_ea::movem_allowed(image({0x4cd8,0xffff}),r,m),"last byte of MOVEM load span denied before first transfer");
        expect(m.peeks.empty(),"denied simple MOVEM load performs no operand read");}
    {Memory m;m.denied_write=0x20000;
        expect(!modern040::native_ea::movem_allowed(image({0x48d0,0}),r,m),"zero mask retains interpreter behavior");
        expect(!m.saw(0x20000,4,true)&&!m.saw(0x20000,0,true),"zero mask does not invent operand permission query");}
    {Memory m;m.denied_write=0x30000;
        expect(!modern040::native_ea::movem_allowed(image({0x48f0,0,0x1b21,0xffe2}),r,m),"zero indirect mask retains interpreter behavior");
        expect(m.peeks.empty()&&!m.saw(0x30000,4,true),"zero mask never adds a speculative pointer read");}
    {Memory m;m.denied_read=0x1ffd7;
        expect(!modern040::native_ea::movem_allowed(image({0x48f0,0,0x1b21,0xffe2}),r,m),"zero mask cannot bypass pointer permission");
        expect(m.peeks.empty(),"denied indirect pointer is never read even for empty mask");}
    {Memory m;expect(!modern040::native_ea::movem_allowed(image({0x48d0}),r,m),"truncated register mask rejected");
        expect(m.peeks.empty(),"missing mask does not evaluate any pointer");}
    {Memory m;r[15]=0x20;
        expect(!modern040::native_ea::movem_allowed(image({0x48e7,0xffff}),r,m),"wrapped full store span rejected");
        expect(m.saw(0xffffffe0,64,true),"predecrement span uses defined unsigned subtraction");r=before;}
    expect(r==before,"MOVEM planner leaves every caller register unchanged");
    std::cout<<"PASS "<<checks<<" MOVEM span/mask/EA/permission/no-transfer assertions\n";return 0;
}catch(const std::exception&e){std::cerr<<"FAIL "<<e.what()<<"\n";return 1;}}
