# ARM guest JIT correctness

This is native guest acceptance, separate from the standalone instruction
emitter test. Run the same owned 68k programs with JIT disabled and enabled.
The on arms must retire translated guest operations in TT-RAM, witnessed by
instructions emitted directly after successfully compiled guest operations.
The off arms must not report native guest execution. A fallback-only pass fails
the on gate. All witnesses leave guest flags and allocated registers intact.

Fifteen profiles include mixed integer/FPU fallback. The guests assert register/CCR and NatFeat boundaries, user/supervisor/master
exception stacks, real VBL/MFP interrupt frames and RTE, warmed self-modifying
code with CPUSHA/invalidation, and hot-block trace recovery. A deliberately
incorrect PC expectation must fail with its expected status and FAIL marker.

`fixtures/provenance.json` pairs each checked-in PRG hex with its compiler recipe
and exact C/assembly source hashes. Rebuild using
`tests/modern-jit/build-fixtures.py`; no personal disk or guest apps are used.

On Apple Silicon:

```
python3 tests/arm64-guest/run.py --app PATH/ARAnyM-Unofficial.app --output RESULTS
```

On native Linux ARM64:

```
python3 tests/arm64-guest/run.py --emulator PATH/aranym --firmware PATH/emutos-aranym.img --output RESULTS
```

Reports retain input hashes, config hashes, host status, exact guest marker and
native execution witnesses. Native Mac and Linux results remain distinct. The
Linux CI host proves ARM64 execution, not Raspberry Pi display/performance or
BeePi integration. GFA Basic/GBE and desktop testing are still required.

On native Linux ARM32, add `--architecture arm32`. For explicit cross-emulated
ARM32 debug/CI execution, also provide `--qemu PATH/qemu-arm-static`,
`--qemu-sysroot PATH` and `--qemu-library-path TARGET_LIBRARY_PATH`. Such reports
set `native=false` and `cross_emulated=true`; they do not establish Pi hardware
performance or display acceptance. The ARM32 and ARM64 counters count executed
translated guest operations, not cache dispatches or compilation requests.
