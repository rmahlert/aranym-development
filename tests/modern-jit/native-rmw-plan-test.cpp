/* SPDX-License-Identifier: GPL-2.0-or-later
 * Literal 68040 permission/order vectors; no guest ALU implementation.
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
    std::map<std::uint32_t,std::uint32_t> pointers{{0x1ffd4,0x30000},{0xfda,0x3abc0}};
    std::uint32_t denied_read=0xdeadbeef,denied_write=0xdeadbeef;
    bool allowed(std::uint32_t a,unsigned s,bool w){permits.emplace_back(a,s,w);return a!=(w?denied_write:denied_read);}
    bool peek32(std::uint32_t a,std::uint32_t &v){peeks.push_back(a);auto p=pointers.find(a);if(p==pointers.end())return false;v=p->second;return true;}
    bool saw(std::uint32_t a,unsigned s,bool w){return std::find(permits.begin(),permits.end(),Request(a,s,w))!=permits.end();}
};
unsigned checks=0;
void expect(bool yes,const char *why){++checks;if(!yes)throw std::runtime_error(why);}
Image image(std::initializer_list<std::uint16_t>w){Image i;i.pc=0x1000;i.count=w.size();std::copy(w.begin(),w.end(),i.words.begin());return i;}
struct Case {
    Image code;std::uint32_t address;unsigned size;bool read,write;unsigned pointers;
};
int main(int argc,char**argv){try{
    const std::string_view mode=argc==2?argv[1]:"";
    if(argc>2||(!mode.empty()&&mode!="--negative-write"&&mode!="--negative-pc-base"&&mode!="--negative-clr-read"))return 2;
    std::array<std::uint32_t,16>regs{};regs[8]=0x20000;regs[1]=0xfffffff9;regs[15]=0x4000;
    const auto original=regs;
    const std::vector<Case>cases{
        {image({0x4418}),0x20000,1,true,true,0},               // NEG.B (A0)+
        {image({0x4460}),0x1fffe,2,true,true,0},               // NEG.W -(A0)
        {image({0x40a8,0xfffc}),0x1fffc,4,true,true,0},        // NEGX.L -4(A0)
        {image({0x4267}),0x3ffe,2,false,true,0},               // CLR.W -(A7)
        {image({0x4690}),0x20000,4,true,true,0},               // NOT.L (A0)
        {image({0x4a3a,0xfffa}),0xffc,1,true,false,0},         // TST.B -6(PC)
        {image({0x525f}),0x4000,2,true,true,0},                // ADDQ.W #1,(A7)+
        {image({0x51a8,0xfffc}),0x1fffc,4,true,true,0},        // SUBQ.L #8,-4(A0)
        {image({0x0010,0x0080}),0x20000,1,true,true,0},        // ORI.B #128,(A0)
        {image({0x0268,0xff00,4}),0x20004,2,true,true,0},      // ANDI.W #$ff00,4(A0)
        {image({0x0ab9,0x1122,0x3344,0x1234,0x5678}),0x12345678,4,true,true,0},
        {image({0x06b0,0,3,0x1b21,0xffe2}),0x30000,4,true,true,1},
        {image({0x0478,1,0x8000}),0xffff8000,2,true,true,0},   // SUBI.W #1,($8000).W
        {image({0x0c7a,1,0xfffa}),0xffe,2,true,false,0},       // CMPI.W immediate precedes PC extension
        {image({0x0cbb,0,3,0x1b21,0xffe2}),0x3abc0,4,true,false,1},
        {image({0xd090}),0x20000,4,true,false,0},               // ADD.L (A0),D0
        {image({0xd150}),0x20000,2,true,true,0},                // ADD.W D0,(A0)
        {image({0xb0a8,4}),0x20004,4,true,false,0},             // CMP.L 4(A0),D0
        {image({0xb190}),0x20000,4,true,true,0},                // EOR.L D0,(A0)
        {image({0xd1e8,4}),0x20004,4,true,false,0},             // ADDA.L 4(A0),A0
        {image({0xc0e8,4}),0x20004,2,true,false,0},             // MULU.W 4(A0),D0
        {image({0x0110}),0x20000,1,true,false,0},               // BTST D0,(A0)
        {image({0x0850,7}),0x20000,1,true,true,0},              // BCHG #7,(A0)
        {image({0xe3d0}),0x20000,2,true,true,0},                // LSL.W (A0)
        {image({0x56d0}),0x20000,1,false,true,0},               // SNE (A0)
        {image({0xe8d0,0x0008}),0x20000,1,true,false,0},        // BFTST (A0){0:8}
        {image({0xead0,0x07c0}),0x20003,5,true,true,0},         // BFCHG (A0){31:32}
        {image({0xefd0,0x0841}),0x1ffff,1,true,true,0}          // BFINS (A0){D1:1}, D1=-7
    };
    unsigned case_index=0;
    for(const auto &c:cases){
        Memory memory;
        if(!modern040::native_ea::allowed(c.code,regs,memory))
            throw std::runtime_error("literal memory opcode preflight case "+std::to_string(case_index));
        ++checks;
        const auto address=mode=="--negative-pc-base"&&c.address==0xffe?0xffcu:c.address;
        expect(memory.saw(address,c.size,false)==c.read,"read permission uses actual operand and extension-word PC base");
        expect(memory.saw(c.address,c.size,true)==c.write,"write permission matches instruction direction");
        expect(memory.peeks.size()==c.pointers,"only indirect RAM pointer reads are performed");
        ++case_index;
    }
    for(auto opcode:{0x4ad0u,0x003cu,0x007cu,0x5248u,0x4afcu,0x4890u,0x0ad0u,0x4ed0u,
                     0xd188u,0x81c0u,0x08c0u,0xe3c0u,0x56c0u})
        expect(!modern040::native_ea::single_memory_policy(opcode).size,"locked/control/MOVEM/CCR/register forms excluded");
    {Memory m;m.denied_write=0x20000;
        expect(modern040::native_ea::allowed(image({0x4410}),regs,m)==(mode=="--negative-write"),"RMW denied write rejects before operand read");
        expect(m.peeks.empty(),"denied RMW makes no operand or pointer read");}
    {Memory m;m.denied_read=0x3ffe;
        expect(modern040::native_ea::allowed(image({0x4267}),regs,m),"040 CLR does not require operand read permission");
        expect(m.saw(0x3ffe,2,false)==(mode=="--negative-clr-read"),"040 CLR is write-only, unlike 68000 dummy-read behavior");}
    {Memory m;m.denied_write=0x20000;
        expect(modern040::native_ea::allowed(image({0x4a90}),regs,m),"TST does not require write permission");
        expect(!m.saw(0x20000,4,true),"read-only test never checks operand write");}
    {Memory m;m.denied_read=0x1002;
        expect(!modern040::native_ea::allowed(image({0x06b0,0,3,0x1b21,0xffe2}),regs,m),"denied immediate word rejects");
        expect(m.peeks.empty(),"denied immediate prevents indirect pointer read");}
    {Memory m;expect(!modern040::native_ea::allowed(image({0x06b0,0}),regs,m),"truncated immediate rejects");
        expect(m.peeks.empty(),"truncated immediate cannot read a pointer");}
    expect(regs==original,"preflight leaves all guest registers unchanged");
    std::cout<<"PASS "<<checks<<" unary/immediate/quick memory permission/order assertions\n";return 0;
}catch(const std::exception&e){std::cerr<<"FAIL "<<e.what()<<"\n";return 1;}}
