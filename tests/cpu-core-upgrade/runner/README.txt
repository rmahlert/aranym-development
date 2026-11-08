Isolated modern040 integer library/runner
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

The fast040 table now binds 1,651 unchanged generated families /45,384 opcode
encodings. Another216 families are explicitly excluded with missing-helper names
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

Default mode has terminal typed guest-exception/memory-fault exits. --exceptions
enables tested real format0/1/2 entry and actual generated RTE recovery. Legal
excluded opcodes fail explicitly rather than execute as NOP.
Illegal/A-line/privileged instructions report their architectural vector; FPU is
explicitly unsupported. Enabling TCR/TTR translation stops before further fetch.
No FPU/MMU/JIT, caches/timing, Atari bus/events/IRQ, NatFeat/EmulOp or guest boot
is established. --lc040-probes explicitly enables the tested absent-FPU probe
subset, including genuine format4 recovery; active FPU operations stay excluded.
Cache-control registers are stored but hardware cache behavior
is absent. Access-error continuation/writebacks and Atari integration
remain explicit gates before firmware/Atari boot claims.

312,178 stage1 assertions pass normally and with ASan/UBSan. Coverage includes all
65,536 SR encodings and bank transitions; legal/illegal/control-register masks
and privilege failures; independent ADDX/SUBX B/W/L boundary/stickyZ oracles with
all32 CCR inputs through the NEW runner dispatch; A7 byte/fault and JSR/RTS access
order; all16 Scc conditions; brief/full/indirect indexed addressing; T1 deferral;
STOP/budget and memory/unsupported exits. Prior differential remains independent
and passes14,769 expectations per backend/14,761 strict matches/eight exact audits.

Frame stage adds222 manual-derived assertions and two negative frame controls.
Synchronous master exceptions retainM. Explicit interrupt metadata controls the
MSP format0 +ISP format1 sequence even for custom vectors, and real RTE restores
the selected bank. Real guest ADDQ-to-stacked-PC/RTE handlers recover from
illegal/privilege/A-line/F-line faults. T1/STOP trace entry and deferred trace on
RTE are checked. Frame7 layout is tested with synthetic inputs only; real access
fault continuation remains BLOCKED. RTE cannot silently skip unsupported FPU or
access-error frames, and partially committed access faults cannot resume.
See frame-validation.txt for scope, manual MCP citations and remaining contracts.

LC040 stage adds685 assertions for recognized FSAVE/FRESTORE/FBcc absent-FPU
paths, legal effective addresses, privilege ordering, format4 nextPC/EA/faultPC,
actual RTE4 and an EmuTOS-shaped probe/recovery sequence. Two negative controls
reject wrong frame format and an added operand read after absence. Both normal
and ASan/UBSan builds pass. See lc040-validation.txt for the exact subset.

Source/generated function bodies and selected upstream helpers are extracted
mechanically without replacement instruction code. Provenance includes source
pins/hashes, helper hashes and exclusion inventory. GPL-2.0-or-later under the
repository COPYING; preserve upstream notices and attribution.
