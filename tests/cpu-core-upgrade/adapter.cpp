/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "adapter.hpp"
#include <algorithm>
#include <limits>
#include <tuple>

Registers regs;
Snapshot machine;
std::array<Handler, 65536> handlers{};
namespace {
struct StopAccess {};
std::vector<std::tuple<uae_u32, uae_u32, Kind>> denied;
constexpr uae_u32 mmio = 0xff00;

[[noreturn]] void fault(int vector, Kind kind, uae_u32 address, unsigned size) {
    machine.fault = Fault{vector, kind, address, size, regs.instruction_pc, regs.pc};
    throw StopAccess{};
}
bool valid(Kind kind, uae_u32 address, unsigned size) {
    if (address >= machine.ram.size() || size > machine.ram.size() - address) return false;
    for (const auto &[first, last, blocked] : denied) {
        if (kind == blocked && address <= last && address + size - 1 >= first) return false;
    }
    // One read-sensitive synthetic register; instruction execution is forbidden
    // there. All other addresses are ordinary RAM, including unaligned data.
    if (address <= mmio && address + size > mmio && (address != mmio || size != 4 || kind != Kind::Read))
        return false;
    return true;
}
uae_u32 mask(unsigned size) {
    return size == 4 ? std::numeric_limits<uae_u32>::max() : (uae_u32{1} << (8 * size)) - 1;
}
// Explicit synthetic NatFeat seam, not WinUAE code or ARAnyM's implementation.
// Return-address skipping, argument load, privilege input and D0 result are
// observable. These two test feature IDs implement no real host services.
void natfeat(uae_u16 opcode) {
    const auto stack = regs.r[15] + 4;
    const auto argument = bus_read(Kind::Read, stack, 4);
    machine.calls.push_back({opcode, stack, argument, regs.s});
    if (opcode == 0x7301 && !regs.s) fault(8, Kind::Fetch, regs.pc, 2);
    regs.r[0] = opcode == 0x7300 ? 0x4e460001 : argument ^ 0x12345678;
    regs.pc += 2;
}
}

uae_u32 bus_read(Kind kind, uae_u32 address, unsigned size) {
    if (!valid(kind, address, size)) {
        machine.accesses.push_back({kind, address, size, 0, false});
        fault(2, kind, address, size);
    }
    uae_u32 value = 0;
    if (address == mmio) {
        value = machine.side_effect_reads++;
    } else {
        for (unsigned i = 0; i < size; ++i) value = (value << 8) | machine.ram[address + i];
    }
    machine.accesses.push_back({kind, address, size, value});
    return value;
}
void bus_write(uae_u32 address, unsigned size, uae_u32 value) {
    value &= mask(size);
    if (!valid(Kind::Write, address, size)) {
        machine.accesses.push_back({Kind::Write, address, size, value, false});
        fault(2, Kind::Write, address, size);
    }
    machine.accesses.push_back({Kind::Write, address, size, value});
    poke(address, size, value);
}
void exception3_read_prefetch(uae_u32, uae_u32 address) {
    fault(3, Kind::Fetch, address, 2);
}
void exception3_read_prefetch_only(uae_u32 opcode, uae_u32 address) {
    exception3_read_prefetch(opcode, address);
}
void reset_machine() {
    regs = Registers{};
    machine = Snapshot{};
    denied.clear();
}
void poke(uae_u32 address, unsigned size, uae_u32 value) {
    if (address >= machine.ram.size() || size > machine.ram.size() - address)
        throw std::out_of_range("test RAM initialization");
    for (unsigned i = size; i > 0; --i) {
        machine.ram[address + i - 1] = value & 0xff;
        value >>= 8;
    }
}
void deny_access(uae_u32 first, uae_u32 last, Kind kind) { denied.emplace_back(first, last, kind); }
bool step() {
    if (machine.fault) throw std::logic_error("step after terminal synthetic fault");
    regs.instruction_pc = regs.pc;
    try {
        if (regs.pc & 1) fault(3, Kind::Fetch, regs.pc, 2);
        const auto opcode = bus_read(Kind::Fetch, regs.pc, 2);
        if (opcode == 0x7300 || opcode == 0x7301) natfeat(opcode);
        else {
            const auto handler = handlers[opcode];
            if (!handler) throw std::runtime_error("unsupported instruction " + std::to_string(opcode));
            handler(opcode);
        }
        return true;
    } catch (const StopAccess &) {
        return false;
    }
}
Snapshot snapshot() {
    auto result = machine;
    result.registers = regs;
    return result;
}
std::string difference(const Snapshot &expected, const Snapshot &actual) {
    if (expected.registers.r != actual.registers.r) return "D/A registers";
    if (expected.registers.pc != actual.registers.pc ||
        expected.registers.instruction_pc != actual.registers.instruction_pc) return "PC/instruction PC";
    if (!(expected.registers == actual.registers)) return "CCR/mode/trace state";
    if (expected.ram != actual.ram) return "RAM bytes";
    if (expected.accesses != actual.accesses) return "ordered bus accesses";
    if (expected.fault != actual.fault) return "fault metadata";
    if (expected.calls != actual.calls) return "synthetic NatFeat calls";
    if (expected.side_effect_reads != actual.side_effect_reads) return "MMIO read exposure";
    if (expected.trace_checks != actual.trace_checks) return "trace callbacks";
    return {};
}
