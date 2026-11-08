/* SPDX-License-Identifier: GPL-2.0-or-later
 * Independent architectural expectations for the declared instruction subset.
 * Every comparison includes all D/A registers, PC, CCR/mode, RAM, ordered bus
 * operations, fault metadata, synthetic calls and MMIO exposure counters.
 */
#include "adapter.hpp"
#include <cstdint>
#include <iostream>
#include <limits>
#include <string_view>

namespace {
unsigned checks = 0;
void require(bool condition, const std::string &message) {
    if (!condition) throw std::runtime_error(message);
}
void compare(const Snapshot &expected, const std::string &name) {
    ++checks;
    const auto delta = difference(expected, snapshot());
    require(delta.empty(), name + ": " + delta);
}
void word(uae_u32 address, uae_u16 opcode) { poke(address, 2, opcode); }
void expected_bytes(Snapshot &state, uae_u32 address, unsigned size, uae_u32 value) {
    for (unsigned i = size; i > 0; --i) {
        state.ram[address + i - 1] = value & 255;
        value >>= 8;
    }
}
void fetch(Snapshot &state, uae_u32 pc, uae_u16 opcode) {
    state.accesses.push_back({Kind::Fetch, pc, 2, opcode});
    state.registers.instruction_pc = pc;
}
void set_ccr(Registers &state, unsigned bits) {
    state.x = bits & 16; state.n = bits & 8; state.z = bits & 4;
    state.v = bits & 2; state.c = bits & 1;
}
void move_flags(Registers &state, uae_u32 value, unsigned bits) {
    const auto masked = std::uint64_t(value) & ((std::uint64_t{1} << bits) - 1);
    state.n = masked & (std::uint64_t{1} << (bits - 1));
    state.z = masked == 0;
    state.v = state.c = false;
}

void test_moveq() {
    for (unsigned dest = 0; dest < 8; ++dest) {
        for (unsigned immediate = 0; immediate < 256; ++immediate) {
            reset_machine();
            const auto opcode = uae_u16(0x7000 | (dest << 9) | immediate);
            for (unsigned i = 0; i < 16; ++i) regs.r[i] = 0xa5a50000 + i;
            set_ccr(regs, 31);
            word(0x100, opcode);
            auto expected = snapshot();
            const auto value = immediate < 128 ? immediate : 0xffffff00 | immediate;
            expected.registers.r[dest] = value;
            move_flags(expected.registers, value, 32);
            expected.registers.pc = 0x102;
            fetch(expected, 0x100, opcode);
            require(step(), "MOVEQ unexpectedly faulted");
            compare(expected, "MOVEQ all immediates/registers");
        }
    }
}

std::int64_t signed_value(uae_u32 value, unsigned width) {
    const auto modulus = std::uint64_t{1} << width;
    return value & (std::uint64_t{1} << (width - 1)) ?
        std::int64_t(value) - std::int64_t(modulus) : std::int64_t(value);
}
void test_extend_arithmetic() {
    for (unsigned width : {8u, 16u, 32u}) {
        const auto modulus = std::uint64_t{1} << width;
        const auto mask = uae_u32(modulus - 1);
        const auto sign = uae_u32(modulus >> 1);
        const std::array<uae_u32, 8> values = {0, 1, 2, sign - 1, sign, sign + 1, mask - 1, mask};
        for (bool subtract : {false, true}) for (const auto src : values)
        for (const auto dst : values) for (unsigned flags = 0; flags < 32; ++flags) {
            reset_machine();
            const auto size = width == 8 ? 0 : width == 16 ? 0x40 : 0x80;
            const auto opcode = uae_u16((subtract ? 0x9300 : 0xd300) | size);
            regs.r[0] = (0x76543210 & ~mask) | src;
            regs.r[1] = (0xfedcba98 & ~mask) | dst;
            set_ccr(regs, flags);
            word(0x100, opcode);
            auto expected = snapshot();
            const auto extend = std::uint64_t(expected.registers.x);
            const auto wide_src = std::uint64_t(src) + extend;
            const auto unsigned_result = subtract ? std::uint64_t(dst) - wide_src : std::uint64_t(dst) + wide_src;
            const auto result = uae_u32(unsigned_result & mask);
            const auto signed_src = signed_value(src, width);
            const auto signed_dst = signed_value(dst, width);
            const auto mathematical = subtract ? signed_dst - signed_src - std::int64_t(extend) :
                                                 signed_dst + signed_src + std::int64_t(extend);
            auto &r = expected.registers;
            r.r[1] = (r.r[1] & ~mask) | result;
            r.x = r.c = subtract ? wide_src > dst : unsigned_result >= modulus;
            r.z = r.z && result == 0;
            r.n = result & sign;
            r.v = mathematical < -std::int64_t(sign) || mathematical >= std::int64_t(sign);
            r.pc = 0x102;
            fetch(expected, 0x100, opcode);
            require(step(), "ADDX/SUBX unexpectedly faulted");
            compare(expected, "ADDX/SUBX independent wide-integer oracle");
        }
    }
}

void test_addq() {
    for (unsigned quick = 1; quick <= 8; ++quick) for (unsigned dest = 0; dest < 8; ++dest)
    for (uae_u32 value : {0u, 1u, 0x7fffffffu, 0x80000000u, 0xfffffff8u, 0xffffffffu}) {
        reset_machine();
        const auto opcode = uae_u16(0x5080 | ((quick & 7) << 9) | dest);
        regs.r[dest] = value;
        set_ccr(regs, 31);
        word(0x100, opcode);
        auto expected = snapshot();
        const auto total = std::uint64_t(value) + quick;
        const auto result = uae_u32(total);
        auto &r = expected.registers;
        r.r[dest] = result;
        r.x = r.c = total > 0xffffffffu;
        r.n = result & 0x80000000;
        r.z = result == 0;
        r.v = signed_value(value, 32) + quick > std::numeric_limits<std::int32_t>::max();
        r.pc = 0x102;
        fetch(expected, 0x100, opcode);
        require(step(), "ADDQ unexpectedly faulted");
        compare(expected, "ADDQ immediate encoding/registers/flags");
    }
}

void test_byte_addressing() {
    for (unsigned areg = 0; areg < 8; ++areg) for (bool predecrement : {false, true})
    for (bool fail_read : {false, true}) {
        reset_machine();
        const auto opcode = uae_u16((predecrement ? 0x1020 : 0x1018) | areg);
        const unsigned stride = areg == 7 ? 2 : 1;
        const unsigned address = predecrement ? 0x400 - stride : 0x400;
        regs.r[8 + areg] = 0x400;
        regs.r[0] = 0x12345678;
        set_ccr(regs, 31);
        word(0x100, opcode);
        poke(address, 1, 0x80);
        if (fail_read) deny_access(address, address, Kind::Read);
        auto expected = snapshot();
        fetch(expected, 0x100, opcode);
        expected.accesses.push_back({Kind::Read, address, 1, fail_read ? 0u : 0x80u, !fail_read});
        if (fail_read) {
            expected.fault = Fault{2, Kind::Read, address, 1, 0x100, 0x100};
        } else {
            expected.registers.r[0] = 0x12345680;
            expected.registers.r[8 + areg] = predecrement ? address : address + stride;
            move_flags(expected.registers, 0x80, 8);
            expected.registers.pc = 0x102;
        }
        require(step() == !fail_read, "byte access fault outcome");
        compare(expected, "byte pre/post A0-A7 and fault ordering");
    }
}

void test_big_endian_and_mmio() {
    reset_machine();
    // MOVE.L (A0),D0; MOVE.L D0,(A1); MOVE.L (A2),D0; MOVE.L (A2),D1
    const std::array<uae_u16, 4> code = {0x2010, 0x2280, 0x2012, 0x2212};
    for (unsigned i = 0; i < code.size(); ++i) word(0x100 + 2 * i, code[i]);
    regs.r[8] = 0x401; // 040 ordinary unaligned data access is accepted by this bus.
    regs.r[9] = 0x501;
    regs.r[10] = 0xff00;
    regs.x = true;
    poke(0x401, 4, 0x89abcdef);
    auto expected = snapshot();
    for (unsigned i = 0; i < code.size(); ++i) {
        const auto pc = 0x100 + 2 * i;
        fetch(expected, pc, code[i]);
        if (i == 0) {
            expected.accesses.push_back({Kind::Read, 0x401, 4, 0x89abcdef});
            expected.registers.r[0] = 0x89abcdef;
            move_flags(expected.registers, 0x89abcdef, 32);
        } else if (i == 1) {
            expected.accesses.push_back({Kind::Write, 0x501, 4, 0x89abcdef});
            expected_bytes(expected, 0x501, 4, 0x89abcdef);
        } else {
            expected.accesses.push_back({Kind::Read, 0xff00, 4, i - 2});
            expected.registers.r[i - 2] = i - 2;
            move_flags(expected.registers, i - 2, 32);
            ++expected.side_effect_reads;
        }
        expected.registers.pc = pc + 2;
        require(step(), "memory/MMIO sequence faulted");
        compare(expected, "big-endian RAM and exact read-sensitive MMIO exposure");
    }
}

void test_calls_and_trace() {
    reset_machine();
    word(0x100, 0x4e90); // JSR (A0)
    word(0x200, 0x4e75); // RTS
    regs.r[8] = 0x200;
    regs.r[15] = 0x800;
    regs.t0 = true;
    set_ccr(regs, 31);
    auto expected = snapshot();
    fetch(expected, 0x100, 0x4e90);
    expected.accesses.push_back({Kind::Write, 0x7fc, 4, 0x102});
    expected_bytes(expected, 0x7fc, 4, 0x102);
    expected.registers.r[15] = 0x7fc;
    expected.registers.pc = 0x200;
    expected.trace_checks = 1;
    require(step(), "JSR faulted");
    compare(expected, "JSR exact return PC/stack/trace");
    fetch(expected, 0x200, 0x4e75);
    expected.accesses.push_back({Kind::Read, 0x7fc, 4, 0x102});
    expected.registers.r[15] = 0x800;
    expected.registers.pc = 0x102;
    expected.registers.instruction_pc = 0x102;
    expected.trace_checks = 2;
    require(step(), "RTS faulted");
    compare(expected, "RTS return PC/stack/trace");
}

void test_control_faults() {
    for (uae_u16 opcode : {0x4e90, 0x4ed0, 0x4e75}) {
        reset_machine();
        word(0x100, opcode);
        regs.r[8] = 0x201;
        regs.r[15] = 0x800;
        poke(0x800, 4, 0x201);
        auto expected = snapshot();
        fetch(expected, 0x100, opcode);
        if (opcode == 0x4e75) expected.accesses.push_back({Kind::Read, 0x800, 4, 0x201});
        const unsigned current_pc = opcode == 0x4e90 ? 0x201 : opcode == 0x4ed0 ? 0x102 : 0x100;
        expected.registers.pc = current_pc;
        const auto instruction_pc = opcode == 0x4e75 ? 0x201u : 0x100u;
        expected.registers.instruction_pc = instruction_pc;
        expected.fault = Fault{3, Kind::Fetch, 0x201, 2, instruction_pc, current_pc};
        require(!step(), "odd control target must fault");
        compare(expected, "generated JSR/JMP/RTS odd-target exception seam");
    }
    for (uae_u32 address : {0xffffu, 0xffffffffu}) {
        reset_machine();
        word(0x100, 0x2010);
        regs.r[8] = address;
        auto expected = snapshot();
        fetch(expected, 0x100, 0x2010);
        expected.accesses.push_back({Kind::Read, address, 4, 0, false});
        expected.fault = Fault{2, Kind::Read, address, 4, 0x100, 0x100};
        require(!step(), "out-of-range read must fault");
        compare(expected, "bounds/32-bit wrap rejection");
    }
    reset_machine();
    word(0x100, 0x4e90);
    regs.r[8] = 0x200;
    regs.r[15] = 0x800;
    deny_access(0x7fc, 0x7ff, Kind::Write);
    auto expected = snapshot();
    fetch(expected, 0x100, 0x4e90);
    expected.accesses.push_back({Kind::Write, 0x7fc, 4, 0x102, false});
    expected.registers.pc = 0x200;
    expected.fault = Fault{2, Kind::Write, 0x7fc, 4, 0x100, 0x200};
    require(!step(), "JSR stack write must fault");
    compare(expected, "JSR failed stack write does not decrement A7");
}

void test_natfeat_seam() {
    for (uae_u16 opcode : {0x7300, 0x7301}) for (bool supervisor : {false, true}) {
        reset_machine();
        word(0x100, opcode);
        regs.s = supervisor;
        regs.r[15] = 0x800;
        regs.r[1] = 0xdeadbeef;
        poke(0x800, 4, 0xcccccccc); // return address must be skipped.
        poke(0x804, 4, 0x11223344);
        auto expected = snapshot();
        fetch(expected, 0x100, opcode);
        expected.accesses.push_back({Kind::Read, 0x804, 4, 0x11223344});
        expected.calls.push_back({opcode, 0x804, 0x11223344, supervisor});
        const bool privileged_fault = opcode == 0x7301 && !supervisor;
        if (privileged_fault) expected.fault = Fault{8, Kind::Fetch, 0x100, 2, 0x100, 0x100};
        else {
            expected.registers.r[0] = opcode == 0x7300 ? 0x4e460001 : 0x0316653c;
            expected.registers.pc = 0x102;
        }
        require(step() != privileged_fault, "synthetic NatFeat privilege outcome");
        compare(expected, "synthetic NatFeat argument/D0/privilege seam only");
    }
}

int negative_check(std::string_view mutation) {
    reset_machine();
    word(0x100, 0x2010);
    regs.r[8] = 0xffffffff;
    require(!step(), "negative fixture must contain a fault");
    const auto expected = snapshot();
    auto corrupted = expected;
    if (mutation == "ccr") corrupted.registers.x = !corrupted.registers.x;
    else if (mutation == "pc") corrupted.registers.instruction_pc += 2;
    else if (mutation == "a7") corrupted.registers.r[15] += 4;
    else if (mutation == "memory") corrupted.ram[0x401] ^= 1;
    else if (mutation == "access") corrupted.accesses.pop_back();
    else if (mutation == "fault") corrupted.fault->size = 2;
    else throw std::runtime_error("unknown checker mutation");
    const auto delta = difference(expected, corrupted);
    if (delta.empty()) return 0; // Unexpected acceptance: driver must reject this.
    std::cout << "REJECTED deliberate " << mutation << " corruption: " << delta << '\n';
    return 1;
}
}

int main(int argc, char **argv) {
    try {
        install_generated_handlers();
        if (argc == 3 && std::string_view(argv[1]) == "--negative-check") return negative_check(argv[2]);
        require(argc == 1, "unknown arguments");
        test_moveq();
        test_extend_arithmetic();
        test_addq();
        test_byte_addressing();
        test_big_endian_and_mmio();
        test_calls_and_trace();
        test_control_faults();
        test_natfeat_seam();
        std::cout << "PASS: " << checks << " full architectural/bus snapshot comparisons\n";
        std::cout << "Scope: generated integer subset; synthetic host bus/fault/NatFeat seams; no MMU/FPU/JIT/frames\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 2;
    }
}
