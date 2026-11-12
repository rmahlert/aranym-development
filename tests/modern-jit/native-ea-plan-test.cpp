/* SPDX-License-Identifier: GPL-2.0-or-later
 * Manual-derived literal address/ordering vectors; no emulator helpers.
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
    std::vector<Request> permits;
    std::vector<std::uint32_t> peeks;
    std::map<std::uint32_t,std::uint32_t> pointers;
    std::uint32_t denied=0xdeadbeef;
    bool allowed(std::uint32_t a,unsigned s,bool w){permits.emplace_back(a,s,w);return a!=denied;}
    bool peek32(std::uint32_t a,std::uint32_t &v){peeks.push_back(a);auto p=pointers.find(a);if(p==pointers.end())return false;v=p->second;return true;}
    bool saw(std::uint32_t a,unsigned s,bool w){return std::find(permits.begin(),permits.end(),Request(a,s,w))!=permits.end();}
};
static unsigned checks=0;
void expect(bool yes,const char *why){++checks;if(!yes)throw std::runtime_error(why);}
Image image(std::initializer_list<std::uint16_t>w){Image i;i.pc=0x1000;i.count=w.size();std::copy(w.begin(),w.end(),i.words.begin());return i;}
int main(int argc,char **argv){try{
    const bool bad_pc=argc==2&&std::string_view(argv[1])=="--negative-pc-base";
    if(argc>1&&!bad_pc)return 2;
    std::array<std::uint32_t,16>r{};r[8]=0x20000;r[9]=0x40000;r[1]=0xfffffff9;r[15]=0x4000;
    const auto original=r;
    {Memory m;expect(modern040::native_ea::allowed(image({0x2038,0x8000}),r,m),"absolute short");expect(m.saw(0xffff8000,4,false)&&m.peeks.empty(),"absolute short is signed, no operand read");}
    {Memory m;expect(modern040::native_ea::allowed(image({0x2039,0x1234,0x5678}),r,m),"absolute long");expect(m.saw(0x12345678,4,false),"absolute long full32");}
    {Memory m;expect(modern040::native_ea::allowed(image({0x203a,0xfffa}),r,m),"PC relative");expect(m.saw(bad_pc?0x0ffe:0x0ffc,4,false),"PC is extension-word address");}
    {Memory m;expect(modern040::native_ea::allowed(image({0x2030,0x14fd}),r,m),"brief scaled word index");expect(m.saw(0x1ffe1,4,false),"negative word index scaled four plus negative disp8");}
    {Memory m;expect(modern040::native_ea::allowed(image({0x203b,0x14fd}),r,m),"PC brief index");expect(m.saw(0x0fe3,4,false),"PC brief base");}
    {Memory m;m.pointers[0x1ffd4]=0x30000;expect(modern040::native_ea::allowed(image({0x2030,0x1b21,0xffe2}),r,m),"full preindex");expect(m.peeks==std::vector<std::uint32_t>{0x1ffd4}&&m.saw(0x30000,4,false),"preindexed pointer and null outer");}
    {Memory m;m.pointers[0x1ffd4]=0x30000;expect(modern040::native_ea::allowed(image({0x2030,0x1b23,0xffe2,0xffff,0xfff0}),r,m),"full preindex long outer");expect(m.saw(0x2fff0,4,false),"preindexed negative outer32");}
    {Memory m;m.pointers[0x1ffe2]=0x30000;expect(modern040::native_ea::allowed(image({0x2030,0x1b27,0xffe2,0xffff,0xfff0}),r,m),"full postindex");expect(m.peeks==std::vector<std::uint32_t>{0x1ffe2}&&m.saw(0x2ffe2,4,false),"postindexed pointer plus scaled minus7 and outer minus16");}
    {Memory m;expect(modern040::native_ea::allowed(image({0x2030,0x01f0,0x7fff,0xffff}),r,m),"base/index suppressed");expect(m.saw(0x7fffffff,4,false),"suppressed full32 base displacement");}
    {Memory m;expect(modern040::native_ea::allowed(image({0x1f1f}),r,m),"byte A7 updates");expect(m.saw(0x4000,1,false)&&m.saw(0x4000,1,true),"source A7 plus2 then aliased destination minus2");}
    {Memory m;expect(modern040::native_ea::allowed(image({0x30bc,0xffff}),r,m),"immediate store");expect(m.saw(0x20000,2,true)&&m.peeks.empty(),"immediate consumes one word and only validates write");}
    {Memory m;m.denied=0x40000;expect(!modern040::native_ea::allowed(image({0x2290}),r,m),"destination denied before execution");expect(m.peeks.empty(),"neither operand is read on denied plain copy");}
    for(auto ext:{0x1900u,0x1914u,0x1918u,0x1954u,0x1955u,0x1956u,0x1957u}){Memory m;expect(!modern040::native_ea::allowed(image({0x2030,std::uint16_t(ext)}),r,m),"reserved full format deopts");expect(m.peeks.empty(),"reserved encoding never peeks");}
    {Memory m;m.pointers[0x20000]=0x30000;expect(modern040::native_ea::allowed(image({0x2030,0x1953,0xffff,0xfff0}),r,m),"index suppressed indirect long outer");expect(m.peeks==std::vector<std::uint32_t>{0x20000}&&m.saw(0x2fff0,4,false),"suppressed index is not added to pointer or result");}
    {Memory m;m.denied=0x1ffd4;expect(!modern040::native_ea::allowed(image({0x2030,0x1b21,0xffe2}),r,m),"pointer in device/denied region deopts");expect(m.peeks.empty(),"unpermitted pointer is never read");}
    {Memory m;m.pointers[0x1ffd4]=m.denied;expect(!modern040::native_ea::allowed(image({0x2030,0x1b21,0xffe2}),r,m),"pointer resolves to denied operand");expect(m.peeks.size()==1,"only checked RAM pointer was peeked");}
    {Memory m;expect(modern040::native_ea::allowed(image({0x41f9,0xdead,0xbeef}),r,m),"LEA to unmapped address is valid");expect(!m.saw(0xdeadbeef,4,false),"LEA does not validate nonexistent operand transfer");}
    {Memory m;m.pointers[0x1ffd4]=0xdeadbeef;expect(modern040::native_ea::allowed(image({0x41f0,0x1b21,0xffe2}),r,m),"indirect LEA");expect(m.peeks.size()==1&&!m.saw(0xdeadbeef,4,false),"LEA peeks pointer only");}
    {Memory m;expect(!modern040::native_ea::allowed(image({0x2039,0x1234}),r,m),"truncated instruction deopts");expect(m.peeks.empty(),"truncated absolute has no pointer read");}
    {Memory m;m.denied=0x1000;expect(!modern040::native_ea::allowed(image({0x2030,0x1b21,0xffe2}),r,m),"code permission denied");expect(m.peeks.empty(),"denied code never peeks pointer");}
    expect(r==original,"preflight leaves every register unchanged");
    std::cout<<"PASS "<<checks<<" native EA planning/address/permission/no-device-transfer assertions\n";
    return 0;
}catch(const std::exception&e){std::cerr<<"FAIL "<<e.what()<<"\n";return 1;}}
