# Experimental x86-64 FPU JIT

This profile connects the complete original ARAnyM x87 compiler and its register
allocator to the imported x86 JIT and modern040 state boundary. Native admission
is deliberately guarded: normal extended/nearest arithmetic, square root,
absolute value, negation, comparison, test, register moves, integer inputs and
exact extended stores. Other rounding modes, explicit single/double arithmetic,
transcendentals, quotient operations, exceptional encodings and extreme operands
use the pinned WinUAE SoftFloat frontend. CPU paging and tracing retain the
modern interpreter path.

The allocator now spills/reloads ten-byte payloads instead of treating ARAnyM's
extended registers as eight-byte doubles. Exact canonical FPU register data,
FPCR, FPSR, FPIAR and null/idle state cross the modern-core boundary. Separate
condition-code storage preserves guest-written FPSR combinations. Checks run
before native side effects; fallback leaves generated code through its normal
epilogue. Guest integer flags and host stack balance are preserved.

## Build and enable

Use the normal modern040 Windows or Linux x86-64 build, replacing
`--disable-jit-fpu` with `--enable-jit-fpu`. Its core library must be rebuilt from
the matching source because the FPU activity APIs are additive C-ABI entries.
The build defines `MODERN040_FPU_JIT` only for this connected profile.

The feature remains opt-in and works with `JITSafe = Yes`:

```
[JIT]
JIT = Yes
JITSafe = Yes
JITFPU = Yes
```

With `JITFPU = No`, physical execution retains the classic-compatible FPU path.
The reference test explicitly sets `ARANYM_FPU_SOFTFLOAT_REFERENCE=1` to compare
native translation with the same precise software frontend. This is a test
control, not a requirement for ordinary use.

## Qualification

```
python3 tests/fpu-jit/run.py --emulator PATH/aranym --firmware PATH/emutos-aranym.img --output RESULTS --cpu-regression --benchmark
```

The same immutable Atari program runs with FPU translation off and on. It checks
192 arithmetic/mode/special-value combinations and 16 constant-ROM profiles,
compares every raw register/status output word, checks independent exact integer
arithmetic and PI values, and repeats operations through cold/hot code. An
additional 768 FCMP profiles cover six finite operand pairs, four FPCR modes
and every integer CCR combination, including comparisons such as `2^1024`
against `1` that must not report arithmetic inexactness. FPSR, FPIAR and both
FP registers are compared word for word, with independent relation and
register-preservation checks.
It also crosses an MMU permission-mode boundary 128 times and checks FPU
null/idle frames and arbitrary guest-written FPSR bits. The additional CPU
profiles cover master/user stacks, real VBL/MFP interrupts, self-modifying code
and trace recovery.

Source/fixture hashes and compiler provenance are checked. Diagnostics require
positive executed guest FPU counts in the enabled arm, and zero in the reference
arm. These counters are emitted after successful native FP translation; a
fallback-only pass does not qualify. The arithmetic benchmark additionally
proves execution of millions of native FP operations, then times three identical
runs per backend with all diagnostic counters disabled. Classic IEEE, SoftFloat
and native results stay separate; this is not a whole-system speed claim.

Rebuild the original fixture hex with `build-fixtures.py` and the comparison
fixture with `build-compare.py`, using an Atari MiNT compiler.
No personal boot image or installed guest applications are used in CI.

### Counted host-time benchmark

On a native Linux, Windows or Intel Mac host, run:

```
python3 tests/fpu-jit/benchmark.py --emulator PATH/aranym --firmware PATH/emutos-aranym.img --output COUNTED-RESULTS
```

Add `--compare-with PATH/previous-aranym` to measure a previous executable in
the same run. The immutable guest completes ten million register-arithmetic
iterations and independently counts them in an FP register. Both the final
arithmetic result and the count must match. A separate diagnostic run proves
that at least 49 million FP operations execute natively; all timed runs disable
those counters. The harness records three interleaved samples per backend,
host monotonic elapsed time between the guest's start/result messages and
the exact executable, firmware and guest hashes. Rebuild this separate fixture
with `build-guard-benchmark.py`; its source hashes and compiler provenance are
in `fixtures/guard-benchmark-provenance.json`.

The scalar guard/status reads preserve the existing admission rules while
avoiding temporary arrays on each native FP instruction. On one Linux x86-64
host, the counted loop's median changed from 1.2681 s to 0.9415 s (1.35 times
faster). The same run measured 0.9074 s for classic IEEE and 14.9407 s for the
pinned SoftFloat reference. This is one arithmetic loop, not a desktop speed
claim. A separate 80-by-60 POV-Ray scene retained identical pixels before and
after the change; its rendering time showed no improvement.

## Remaining limitations

Enabled arithmetic exceptions and complete FPSP pending-state/frame continuation
remain unsupported in the modern core. An independent negative guest requires
an explicit unsupported-capability exit for enabled divide-by-zero; it is not a
claim that guest FPU exception delivery works. The required pre/post-instruction
state and frames are described in [MC68040UM sections 8.4 and 9.7](https://www.nxp.com/docs/en/reference-manual/MC68040UM.pdf).
Do not remove this boundary or advertise full FPU pipeline coverage without its
own frame/FSAVE/FRESTORE/RTE qualification.

Packed-decimal and undefined FPU encodings are outside the connected SoftFloat
frontend. ARM FPU translation has its own qualification in `tests/arm-fpu`. FreeMiNT/XaAES/GBE and real
application performance still need owner/community testing.

## Test 1 results

Application source `04aa43ed` passed native Linux [run 43808156740](https://github.com/rmahlert/aranym-development/actions/runs/43808156740)
and the exact Windows preserving installer passed native [run 43809291156](https://github.com/rmahlert/aranym-development/actions/runs/43809291156).
Both cover 208 exact profiles, CPU boundaries and SLIRP. Windows also verifies
clean installation, runtime payload hashes and preservation during upgrade.
Qualification checkout `b3427138` fixes LF source checkout on Windows; it does
not change the application binary.

Three counter-free samples of the same exact arithmetic loop gave medians:

| Native host | Classic IEEE | SoftFloat | Guarded FPU JIT |
| --- | ---: | ---: | ---: |
| Windows x86-64 | 17 | 295 | 23 |
| Linux x86-64 | 18 | 447 | 25 |

Values are guest 200Hz ticks; lower is faster. The native path beats SoftFloat
on this loop but remains slower than classic IEEE. These are not desktop/GBE
performance results. Packages, corresponding source, raw validation and
checksums are on the [unofficial testing release](https://github.com/rmahlert/aranym-development/releases/tag/v1.2.0-unofficial-fpu-jit-test1).
