Isolated modern 68040 integer-handler harness
============================================

This target is separate from ARAnyM's production build. It does not build or
launch an emulator, change a guest image, or replace the production CPU core.
Host requirements: Python 3, Git, a C++20 compiler, and a writable build directory.

Pinned upstream source:
  https://github.com/tonioni/WinUAE.git
  d82bc2549f27943d11dc59482d5388daf1c0f337
  Commit date: 2026-11-08T18:30:15+02:00
  Fetched from current public master on 2026-11-08. This is a tested immutable
  snapshot, not a promise that upstream master remains at this commit.

Download separately, outside the tracked harness:
  git clone --filter=blob:none --sparse https://github.com/tonioni/WinUAE.git WinUAE
  git -C WinUAE sparse-checkout set include
  git -C WinUAE checkout --detach d82bc2549f27943d11dc59482d5388daf1c0f337

From the ARAnyM source root:
  python3 tests/cpu-core-upgrade/run.py --upstream /absolute/path/to/WinUAE
  ASAN_OPTIONS=detect_leaks=0 python3 tests/cpu-core-upgrade/run.py --upstream /absolute/path/to/WinUAE --sanitize --build-dir tests/cpu-core-upgrade/build-sanitize
  python3 tests/cpu-core-upgrade/run.py --upstream /absolute/path/to/WinUAE --benchmark
  python3 tests/cpu-core-upgrade/run.py --upstream /absolute/path/to/WinUAE --compare-old --benchmark --build-dir tests/cpu-core-upgrade/build-differential
  ASAN_OPTIONS=detect_leaks=0 python3 tests/cpu-core-upgrade/run.py --upstream /absolute/path/to/WinUAE --compare-old --sanitize --build-dir tests/cpu-core-upgrade/build-differential-sanitize

The sanitizer environment above disables LeakSanitizer only; the current sandbox
prevents its process inspection. AddressSanitizer and UndefinedBehaviorSanitizer
remain enabled, and sanitizer findings are fatal. Leak detection was not tested.
On a host that permits LeakSanitizer, omit the environment override.

Source provenance and model selection
-------------------------------------

run.py verifies upstream HEAD and clean generator inputs, compiles the unmodified
build68k/readcpu/gencpu from that pin, regenerates all tables/handler families and
extracts selected functions without editing their bodies. It uses the regenerated
op_smalltbl_41 dispatch table, which gencpu defines as fast direct 68040. This table
legitimately shares unchanged handlers with family 40 (the 68060 family); choosing
all suffix-40 or all suffix-41 functions blindly would select the wrong model.
The actual upstream decoder determines all concrete opcode-to-family mappings.

host/ contains only small generator portability shims (fixed-width types come
from the same upstream include/uae/types.h). Runtime adapter.hpp deliberately
implements a narrow test contract instead of including the large production
regstruct/memory/platform headers. Generated sources, binaries, and input hashes
are written under ignored build directories. provenance.json records the source
origin/pin/date, SHA-256 of every generator input, generated handler SHA-256,
chosen dispatch table/families, compiler version, host CPU and sanitizer mode.

Supported generated subset: MOVEQ.L, ADDQ.L to Dn, ADDX/SUBX B/W/L Dn,Dn,
MOVE.B (An)+/-(An) to Dn, MOVE.L (An) to Dn and Dn to (An), NOP,
JSR/JMP (An), RTS. There are 16 families / 2,770 concrete mapped opcodes.
Unselected instructions fail explicitly; they never silently execute as NOP.

Meaningful first-stage coverage
-------------------------------

14,769 independent expected-state comparisons cover:
 - MOVEQ all 256 immediate encodings and all eight destinations.
 - ADDQ all eight quick encodings/destinations and arithmetic boundary values.
 - ADDX/SUBX byte/word/long edge pairs with all 32 incoming CCR combinations,
   including carry/extend, overflow, upper-register preservation and sticky Z.
   Expected results use wide unsigned/signed arithmetic, not copied generated
   flag expressions. These are edge tests, not exhaustive 32-bit arithmetic.
 - A0-A7 byte pre/post access strides and read-fault register-update ordering.
 - Big-endian, unaligned ordinary RAM and exact ordered width-level accesses.
 - A synthetic read-sensitive MMIO register: two reads must remain two reads.
 - JSR/RTS return address/A7/trace and generated odd-target/fault exit paths.
 - Failed JSR stack writes and out-of-range/32-bit-wrap read rejection.
 - A separately identified synthetic NatFeat stub for stack/D0/privilege inputs.

Comparisons include every register/state field this adapter represents, all RAM
bytes, ordered fetch/read/write events, fault metadata, synthetic call records,
MMIO read count and trace callback count. Six deliberate snapshot corruptions
must return exit 1; the driver rejects unexpected acceptance or a crashed checker.

Important limits
----------------

This is not the complete modern core or a working Atari system. It checks the
selected real generated instruction bodies under a small, declared host contract.
Only D/A registers, effective PC, instruction_pc, XNZVC, S and T0 are represented;
other SR bits, banked stacks, caches, prefetch and complete exception state are not.
Effective PC replaces the upstream native instruction-pointer representation.
The RTS helper preserves upstream read/set-PC/instruction_pc/A7 ordering. A fault
record exposes both instruction_pc and current PC; neither is silently substituted
for the other when generated code or helpers change them.

The bus is flat 64 KiB big-endian RAM plus one synthetic register. Events represent
handler access width and order, not physical bus cycles. Failed writes are atomic
in this test bus. Denied ranges and C++ terminal fault records are synthetic seams:
there are no real exception frames, restart/RTE, MMU, address translation, interrupts
or platform signals. The NatFeat test service is synthetic and does not establish
ARAnyM callback compatibility. EmulOp/nested guest calls, FPU, JIT, full SR/memory
banking, IRQ/event integration and real application boot are unsupported.

--compare-old extracts matching generator/decoder/header inputs with git show
from c762046250551840d9c0f32c607713f71a05afa3, compiles its unmodified generator,
and uses its actual resolved cpufunctbl. It verifies that its selected families
cover exactly the same 2,770 opcodes. Bodies are extracted without alteration;
legacy.hpp explicitly binds its memory/helpers and maps the fault_pc bookkeeping
slot to the snapshot's instruction_pc slot. This is an adapter view, not proof
that the production fields can be renamed or substituted.

For each instruction checkpoint the checker runs modern and old handlers on
identical guest bytes, registers, RAM, existing bus events and access-denial rules.
Both must satisfy independent full snapshots. It restores modern state after
checking the old result so the next checkpoint continues the same known program.
14,761 checkpoints require strict full-snapshot equality; eight have individually
specified, source-audited old/new outcomes. Four strict cases exercise the shared
synthetic NatFeat stub rather than different generated CPU instructions. No
blanket normalization or timing waiver hides the differences. See the exact
audit in differential-validation-2026-11-08.txt and integration-boundary.txt.
An additional negative run corrupts PC after actual legacy execution and must
be rejected by the complete differential path, not only the snapshot comparator.

This compares the bounded generated subset under matched adapters, not two full
cores or their real interpreter/event loops. Legacy fixup metadata is present
for generated code, but exception rollback/frames remain unimplemented. The old
core is a regression reference, not an ISA correctness oracle. Differences on
fault paths need full frame/restart validation before either behavior is chosen
for integration.

Timing and future system benchmarks
----------------------------------

--benchmark builds a separate binary using the same generated ADDQ.L/ADDX.L
register handlers and compiler flags. It performs 100,000 warm-up iterations and
seven samples of 20,000,000 iterations (two handler calls per iteration), with
std::chrono::steady_clock and checked final state/checksum. Samples include one
test-state initialization and exclude instruction fetch, decoding, bus logging
and snapshot comparisons. This is handler-only throughput, not interpreter-loop,
JIT, emulator, boot, disk, desktop or HighWire performance. No speedup percentage
is justified without equivalent old-core work under identical conditions.
benchmark.json records raw samples, checksums, full pin/compiler/host provenance
and actual compile flags. A dated initial result is retained alongside this file.

With --compare-old, the benchmark warms up each backend, alternates order for
seven pairs, and calls both native handler ABIs through the same templated loop.
The legacy correctness adapter's void-to-u32 wrapper is deliberately bypassed:
disassembly showed that wrapping ADDX could add an extra native call. Both hot
native instruction bodies must pass an objdump gate rejecting nested calls/jumps
before matched timing runs. The modern cycle return is unused; legacy uses its
actual void ABI. No extra wrapper is timed. Both use the same register/flag/PC
view, build flags, dispatch count, initialization and checksum expectations.
The plain-C flag path is selected for both; production assembly flag macros and
native PC pointers are not represented. GNU objdump is needed for this mode.
Raw paired results are in differential-benchmark-2026-11-08.json; this workload
showed essentially equal throughput and establishes no emulator performance gain.

Once a complete optional core can boot, compare GEMBench (or an available Atari
system benchmark) with the exact same version/hash, guest image/configuration,
boot selections, CPU/RAM/JIT/JITFPU settings, resolution/depth, fVDI/fonts and host
video backend. Use repeated same-host runs; report CPU/memory/graphics separately.
Hold the same controls for boot and HighWire interaction. This harness cannot run
GEMBench, and no proprietary benchmark assets are downloaded or redistributed.

Licenses/authorship
-------------------

Harness files are GPL-2.0-or-later, under the repository COPYING terms. Upstream
sources remain separate and unmodified. Generated derivatives retain a provenance
notice identifying the UAE generator and Bernd Schmidt; the original generator
copyright/license notices remain in the pinned upstream source. Do not substitute
or strip upstream attribution when expanding the imported subset.
