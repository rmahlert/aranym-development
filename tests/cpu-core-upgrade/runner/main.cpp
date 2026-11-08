/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "core.hpp"
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>
#include <string>
using namespace modern040;
namespace {
std::uint64_t number(const char *text) {
    std::size_t consumed = 0;
    const std::string value(text);
    const auto result = std::stoull(value, &consumed, 0);
    if (value.empty() || value.front() == '-' || consumed != value.size()) throw std::runtime_error("invalid unsigned number");
    return result;
}
uae_u32 address(const char *text) {
    const auto value = number(text);
    if (value > std::numeric_limits<uae_u32>::max()) throw std::runtime_error("address exceeds 32-bit guest space");
    return uae_u32(value);
}
}
int main(int argc, char **argv) {
    try {
        bool demo = false, exceptions = false, lc040_probes = false;
        std::string image;
        std::uint64_t budget = 1000;
        uae_u32 load = 0x100, pc = 0x100;
        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i];
            if (arg == "--demo") demo = true;
            else if (arg == "--exceptions") exceptions = true;
            else if (arg == "--lc040-probes") lc040_probes = exceptions = true;
            else if (i+1 < argc && arg == "--image") image = argv[++i];
            else if (i+1 < argc && arg == "--budget") budget = number(argv[++i]);
            else if (i+1 < argc && arg == "--load") load = address(argv[++i]);
            else if (i+1 < argc && arg == "--pc") pc = address(argv[++i]);
            else throw std::runtime_error("usage: integer-runner (--demo | --image RAW) [--budget N] [--load ADDRESS] [--pc ADDRESS]");
        }
        if (demo == !image.empty() || budget > 1000000) throw std::runtime_error("choose demo or raw image; budget <=1000000");
        reset(); install_handlers(); regs.pc = regs.instruction_pc = pc;
        host.deliver_exceptions=exceptions;
        host.lc040_probes=lc040_probes;
        regs.regs[15] = regs.isp = 0xff00; regs.usp = 0x8000; regs.msp = 0xf000;
        if (demo) {
            const uae_u16 code[] = {0x7003,0x7200,0x5281,0x51c8,0xfffc,0x41f9,0,0x0400,0x2081,0x4e72,0x2700};
            for (unsigned i = 0; i < std::size(code); ++i) poke(load+2*i,2,code[i]);
        } else {
            std::ifstream input(image, std::ios::binary);
            if (!input) throw std::runtime_error("cannot read raw image");
            if (load >= host.ram.size()) throw std::runtime_error("load address outside flat RAM");
            std::vector<char> bytes(host.ram.size()-load+1);
            input.read(bytes.data(), std::streamsize(bytes.size()));
            const auto count = unsigned(input.gcount());
            if (input.bad() || count > host.ram.size()-load) throw std::runtime_error("raw image unreadable or exceeds flat RAM");
            for (unsigned i = 0; i < count; ++i) poke(load+i,1,uae_u8(bytes[i]));
        }
        const auto result = run(budget);
        const auto state = architectural_state();
        std::cout << "{\"exit\":\"" << exit_name(result.exit) << "\",\"instructions\":" << result.instructions
                  << ",\"vector\":" << result.vector << ",\"pc\":" << state.pc
                  << ",\"instruction_pc\":" << state.instruction_pc << ",\"sr\":" << state.sr
                  << ",\"usp\":" << state.usp << ",\"isp\":" << state.isp << ",\"msp\":" << state.msp
                  << ",\"vbr\":" << state.vbr << ",\"sfc\":" << state.sfc << ",\"dfc\":" << state.dfc
                  << ",\"cacr\":" << state.cacr << ",\"tcr\":" << state.tcr << ",\"mmusr\":" << state.mmusr
                  << ",\"urp\":" << state.urp << ",\"srp\":" << state.srp << ",\"itt0\":" << state.itt0
                  << ",\"itt1\":" << state.itt1 << ",\"dtt0\":" << state.dtt0 << ",\"dtt1\":" << state.dtt1
                  << ",\"address\":" << result.address << ",\"accesses\":" << host.accesses.size()
                  << ",\"frames\":" << host.frames.size()
                  << ",\"registers\":[";
        for (unsigned i = 0; i < 16; ++i) { if (i) std::cout << ','; std::cout << state.regs[i]; }
        std::cout << "]}\n";
        return result.exit == Exit::Budget || result.exit == Exit::Stopped ? 0 : 2;
    } catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
