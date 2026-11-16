# ARM floating-point JIT testing

The ARM64 and ARM32 profiles emit native floating-point arithmetic for register
operations, including moves, square root, absolute value, negation, add,
subtract, multiply, divide, comparison and test. The 68040 single/double forms
use the same rounding sequence as the existing ARM IEEE interpreter.

Enable with `--enable-jit-fpu` when building the ARM guest JIT, then opt in:

```ini
[JIT]
JIT = Yes
JITSafe = Yes
JITFPU = Yes
```

The build defines `ARANYM_ARM_FPU_JIT`. It uses ARAnyM's actual FPU registers,
independent of the upstream compiler's different FPU state layout. Runtime
checks admit bounded finite operands and the default rounding mode before
native effects; other encodings, operand ranges, rounding modes and enabled
arithmetic exceptions take the existing interpreter path. ARM32 requires VFP
double support, as provided by the supported Raspberry Pi targets.

Precision remains the existing ARM IEEE64 register profile. This is not a new
80-bit SoftFloat core, a full 68040 exception/FPSP implementation or an ARM
paged-MMU JIT. Floating-point operations outside native admission still work
through the same interpreter as the previous ARM release.

`run.py` compares identical source-paired guests with interpretation, integer
JIT and FPU JIT. It checks 221 complete raw output profiles: 17 FPCR settings,
12 normal/special/extreme inputs and 17 constant-ROM modes. Every result,
register, status word and saved integer CCR is compared across cold/hot runs,
with independent exact arithmetic, FPIAR and compare/test status checks.
`ARANYM_ARM_FPU_DIAGNOSTICS=1` requires positive executed native FP operations
only in the enabled arm. The benchmark witnesses millions of emitted native
operations, then times three runs with counters disabled.

Native Apple Silicon and Linux ARM64 qualification must precede testing
publication. ARM32 QEMU qualification is explicitly labeled; physical Pi/BeePi
and FreeMiNT/XaAES/fVDI/GBE application testing remain separate requirements.
No personal guest drive is used in these tests.
