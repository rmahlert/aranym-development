/* SPDX-License-Identifier: GPL-2.0-or-later
 * Handler-only microbenchmark. No instruction fetch, bus logging, snapshots,
 * JIT or Atari machine. It measures this adapter/compiler/handler combination.
 */
#include "adapter.hpp"
#include <chrono>
#include <iostream>
#include <string>

static uae_u32 work(unsigned iterations) {
    reset_machine();
    regs.r[0] = 1;
    regs.r[1] = 0x12345678;
    const auto addq = handlers[0x5281]; // ADDQ.L #1,D1
    const auto addx = handlers[0xd380]; // ADDX.L D0,D1
    if (!addq || !addx) throw std::runtime_error("benchmark handlers unavailable");
    for (unsigned i = 0; i < iterations; ++i) {
        addq(0x5281);
        addx(0xd380);
    }
    const auto expected = 0x12345678 + 2u * iterations;
    if (regs.r[1] != expected || regs.r[0] != 1 || regs.pc != 0x100 + 4u * iterations ||
        regs.x || regs.c || regs.v || regs.z || regs.n || !machine.accesses.empty())
        throw std::runtime_error("benchmark checksum/state mismatch");
    return regs.r[1] ^ regs.r[0] ^ regs.pc;
}

int main(int argc, char **argv) {
    try {
        const auto iterations = argc == 2 ? std::stoul(argv[1]) : 20000000ul;
        if (argc > 2 || iterations < 100000 || iterations > 100000000)
            throw std::runtime_error("iterations must be 100000..100000000");
        install_generated_handlers();
        work(100000); // Explicit untimed warm-up.
        std::cout << "{\"scope\":\"handler-only; no fetch/bus/JIT/emulator\","
                     "\"clock\":\"std::chrono::steady_clock\",\"warmup_iterations\":100000,"
                  << "\"iterations_per_sample\":" << iterations
                  << ",\"handlers_per_iteration\":2,\"samples\":[";
        for (unsigned sample = 0; sample < 7; ++sample) {
            const auto begin = std::chrono::steady_clock::now();
            const auto checksum = work(unsigned(iterations));
            const auto end = std::chrono::steady_clock::now();
            const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - begin).count();
            if (ns <= 0) throw std::runtime_error("unusable monotonic timer resolution");
            if (sample) std::cout << ',';
            std::cout << "{\"elapsed_ns\":" << ns << ",\"checksum\":" << checksum << '}';
        }
        std::cout << "]}\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
