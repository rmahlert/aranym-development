Isolated modern040 integer library/runner — stage 1
==================================================

From the ARAnyM source root (C++20, Python3, Git and ar required):
python3 tests/cpu-core-upgrade/runner/build.py --upstream /absolute/path/to/pinned/WinUAE
ASAN_OPTIONS=detect_leaks=0 python3 tests/cpu-core-upgrade/runner/build.py --upstream /absolute/path/to/pinned/WinUAE --sanitize --build-dir tests/cpu-core-upgrade/build-runner-sanitize

The builder first regenerates and checks the previous modern/legacy differential
harness. --reuse-generated is available for local iteration only with matching
generator input/body hashes. Pin remains d82bc2549f27943d11dc59482d5388daf1c0f337.
Additional portable header: git -C WinUAE sparse-checkout set include od-unix/machdep

Outputs: libmodern040-integer.a, runner-tests, integer-runner and a provenance/
coverage inventory under an ignored build directory. No production target,
configure setting, installed emulator or guest image is changed.

The fast040 table now binds 1,635 unchanged generated families /45,186 opcode
encodings. Another232 families are explicitly excluded with missing-helper names
in runner-provenance.json. Compiled coverage does not mean every encoding has
been validated. Real pinned helpers include MakeSR/MakeFromSR_x and its wrappers,
MOVEC model/mask handling, brief/full020 effective address calculation, portable
cctrue and T0 tracing. Architectural state represents D/A, PC, all SR fields,
USP/ISP/MSP and040 control registers. Exported architectural_state() synchronizes
the active bank with A7; raw bank storage follows the upstream lazy-bank contract.
The effective integer PC is an explicit host adapter for upstream native pointers.

Run examples:
tests/cpu-core-upgrade/build-runner/integer-runner --demo --budget 100
tests/cpu-core-upgrade/build-runner/integer-runner --image program.bin --load 0x100 --pc 0x100 --budget 1000

Raw input is read-only, bounded to flat64KiB RAM. CLI budget is at most1,000,000
decoded instruction attempts. An attempt that raises an exception is counted;
an unfetched stopped/unsupported-MMU continuation is not. The demo uses actual
MOVEQ/ADDQ/DBF/LEA/store/STOP and reaches STOP after13 attempts with D1=4 and
RAM400=4. STOP retains its PC until explicit resume advances past its immediate.

Stage1 has terminal typed guest-exception/memory-fault exits, not guest frame
delivery. Legal excluded opcodes fail explicitly rather than execute as NOP.
Illegal/A-line/privileged instructions report their architectural vector; FPU is
explicitly unsupported. Enabling TCR/TTR translation stops before further fetch.
No FPU/MMU/JIT, caches/timing, Atari bus/events/IRQ, NatFeat/EmulOp or guest boot
is established. Cache-control registers are stored but hardware cache behavior
is absent. Stage2 must implement real frames/restart/probe recovery, with manual-
derived assertions, before firmware/Atari boot claims.

312,178 stage1 assertions pass normally and with ASan/UBSan. Coverage includes all
65,536 SR encodings and bank transitions; legal/illegal/control-register masks
and privilege failures; independent ADDX/SUBX B/W/L boundary/stickyZ oracles with
all32 CCR inputs through the NEW runner dispatch; A7 byte/fault and JSR/RTS access
order; all16 Scc conditions; brief/full/indirect indexed addressing; T1 deferral;
STOP/budget and memory/unsupported exits. Prior differential remains independent
and passes14,769 expectations per backend/14,761 strict matches/eight exact audits.

Source/generated function bodies and selected upstream helpers are extracted
mechanically without replacement instruction code. Provenance includes source
pins/hashes, helper hashes and exclusion inventory. GPL-2.0-or-later under the
repository COPYING; preserve upstream notices and attribution.
