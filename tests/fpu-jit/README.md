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
arithmetic and PI values, and repeats operations through cold/hot code.
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

Rebuild fixture hex with `build-fixtures.py` and an Atari MiNT compiler.
No personal boot image or installed guest applications are used in CI.

## Remaining limitations

Enabled arithmetic exceptions and complete FPSP pending-state/frame continuation
remain unsupported in the modern core. An independent negative guest requires
an explicit unsupported-capability exit for enabled divide-by-zero; it is not a
claim that guest FPU exception delivery works. The required pre/post-instruction
state and frames are described in [MC68040UM sections 8.4 and 9.7](https://www.nxp.com/docs/en/reference-manual/MC68040UM.pdf).
Do not remove this boundary or advertise full FPU pipeline coverage without its
own frame/FSAVE/FRESTORE/RTE qualification.

Packed-decimal and undefined FPU encodings are outside the connected SoftFloat
frontend. Native FPU JIT is not enabled on ARM. FreeMiNT/XaAES/GBE and real
application performance still need owner/community testing.
