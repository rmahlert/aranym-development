Experimental full upstream x86 JIT / real ARAnyM integration
==========================================================

This owned branch imports the COMPLETE pinned WinUAE d82bc254 x86 compiler,
cache/support engine and emitter. It reuses the already-proven complete Unix
WinUAE JIT baseline, rather than substituting a handwritten compiler. See
import-provenance.json for upstream/local hashes and baseline executable/harness
provenance. Original GPL notices remain intact; repository COPYING applies.
The full pregenerated compiler/tables are Makefile inputs, so a clean build
cannot silently replace them with the legacy local generator.

The combined profile uses real ARAnyM native register/PC/CCR state while executing
translated instructions, then crosses the existing modern040 C ABI only for
interpreter operations. Host-neutral state/bus/event adaptation is separate from
the x86 compiler/emitter; no ARM port has been attempted. Existing modern040
objects come from the separately qualified pinned interpreter, not a new CPU
implementation. The original IEEE FPU is an explicit optional service boundary.

Build in a separate owned Linux x86-64 tree (never installs or launches):

python3 tests/modern-jit/build.py --build-dir /path/owned/build \
    --profile /home/you/path/owned/profile --modern-core /path/copied/core-objects

The modern-core directory contains integer.o, core.o, c-api.o and the matching
nonsanitized runner-provenance.json from cpu-core-upgrade/runner/build.py. The
builder checks the d82 pin and records object, adapter and native-source hashes.
Omit --modern-core only for the initial native-interpreter isolation profile.
The builder records effective configure flags: the native configure logic selects
-O3 despite the initial CFLAGS request. There is no speed comparison here.

Native interface adaptations
----------------------------
* Each imported compiler entry normalizes native opcode fetch encoding exactly
  once; its generated source/destination extraction then uses canonical opcodes.
* Native regflags retain their actual full-EFLAGS layout. Upstream FLAGGEN
  bridges to it. A packed LAHF/SETO compatibility define is incorrect here.
* Native vm_protect returns zero on success, unlike UAE's boolean wrapper.
  Compiler functions use native void ABI; pointer-width MID types stay x86-64.
* Private calls, external exceptions, IRQs, reset and STOP remain real platform
  operations. Modern cache control explicitly invalidates native code; no second
  execution of a host service or emulated memory operation is invented.
* The modern bus translation unit uses existing explicit checked ARAnyM physical
  accessors and HW dispatch. It locally disables signal-only/page-shortcut
  variants before those static-inline headers. Fixed host mapping remains for
  native JIT PC pointers; modern bus failures do not rely on native signal frames.
* A native NOP flush boundary must publish a deferred constant PC pointer before
  discarding compiler state. A hot-NOP trace fixture caught that omitted store.
* T0/T1 state exits compiled code and uses the genuine modern interpreter. The
  trace fixture checks actual retired PC, format2, SR, A7 and RTE after hot code.
* The experimental Linux fixed allocator never replaces an occupied mapping.
  It uses MAP_FIXED_NOREPLACE when available, checks the returned address, and
  rejects a different hint result on older kernels. An intermittent pre-CPU heap
  assertion exposed the old replacing-MAP_FIXED hazard; the exact failed address
  was not inspected, so collision is an inference rather than a proven crash PC.
  Owned64MiB fixtures explicitly use FixedMemoryOffset60000000, below2GiB and
  above the low native heap range of this no-PIE profile.

Admitted translation profile and limits
--------------------------------------
The full compiler is built, but its native table admits register arithmetic,
logic, shifts, moves, conditions and simple branches. Memory operands, implicit
stack operations, traps, FPU and other families use genuine modern interpreter
handlers. Memory-JIT/MMIO fault restart is NOT qualified. Native fault_pc has an
actual mapping; UAE-only ir/opcode fields have no fabricated native equivalents.

This is one no-MMU 68040 Linux x86-64 no-PIE profile. Windows, macOS, ARM64,
general fault restart, broader source-core profiles and actual HighWire workloads
remain separate gates in beads shin-95jh. No installed binary, owner disk/config,
public branch or release is changed by these scripts.

Regression recipes
------------------
Build owned ordinary Atari programs (the installed cross-compiler may require
normal sandbox approval on hosts where its32-bit executable is restricted):

python3 tests/modern-jit/build-fixtures.py --out-dir /path/owned/guests

C uses -m68000 -msoft-float to select the correct no-FPU Mintlib CRT; assembly
uses -m68040. Existing BRIDGEX.PRG instead requires --legacy-fpu for its original
hard-FPU CRT. A timeout before main under an absent-FPU cookie does not establish
a CPU/JIT failure; those failed initial controls are retained in owned logs.

run-bootstrap.py creates a fresh owned no-disk config, disables networking/audio,
and records binary/ROM/guest/config/log hashes and host monotonic deadlines. Use
--jit --require-modern --require-translated-guest for positive combined JIT arms.
Use matching --expect-exit and --expect-marker for intentional guest failures.
Never run two fixture processes against the same configured profile at once.

Validated guest contracts include all32 CCR values, D/A preservation, user/ISP
A7, query-only JIT host callback, direct privilege exception, exact saved fault
PC/format0 and RTE; expanded NatFeat/user/master/privilege/illegal/trap checks;
real MFP TimerC/custom vector/STOP/ISP1/MSP0/RTE; warmed own-code patch, CPUSHA,
recompile and restore; and hot NOP/T1 trace/frame2/SR/A7/RTE. Wrong expected
PC/frame/patched-result controls are rejected by real guest code, not mistaken
for a host unsupported exit. Combined-stage-validation.json preserves results.

Manual trace evidence was retrieved through the requested TT68K MCP, manual only:
MC68040UM source8082d5f235c8fe79fed877a0, recorddd870a8495fb968795b93441eca8e49c,
PDF223-224 §8.2.6; independent format2 expectations also exist in the qualified
cpu-core-upgrade/runner/frame-test.cpp. No private RTL/mail source was accessed.

ARANYM_UPSTREAM_JIT_DIAGNOSTICS=1 enables bounded source-managed state logs and
an emitted execution witness. The witness uses upstream emitter helpers and
preserves host flags/stack without guest-memory reads or external inspection.
It separately counts actually executed translated instructions in TT-RAM, rather
than inferring execution from cached dispatches. Counters are32-bit and are for
bounded qualification, not long-running instrumentation or speed measurements.
FALLBACK_ONLY and GROUP variants prefixed ARANYM_UPSTREAM_JIT_ are harness-only
isolation controls. Normal diagnostic-off code has no execution-witness hooks.

Full Linux system fixture
-------------------------
run-system.py accepts ONLY the closed prepared c1 MiNT image hash
262bd6e8cad210b650e335e3c3724f28a201ca151fc6630f7649882ba607e8fb.
It copies the disk, three licensed host fonts and starting NVRAM into a new
owned arm, rewrites absolute config paths into that arm, and never boots the
source image. It enables the original IEEE FPU service, disables JIT FPU and
networking, and checks actual modern-core/fVDI/AES/GDOS/32-plane/three-font/ID163d/
metrics/extent/text-call/shutdown markers. JIT-on also requires actual translated
TT-RAM execution. Image/font binaries are not committed or redistributed.

The closed-fixture result is scoped to Linux font/AES services and clean guest
shutdown. It does not establish Windows or HighWire acceptance or overall speed.

Current checked-window paired result: both complete all font/AES/text/shutdown
markers with exit0, identical prepared input and unchanged NVRAM. JIT-on actually
executed88,936,946 translated instructions including85,002,907 in TT-RAM.
The occupied-page test links the exact native vm_alloc.o used by that binary;
it verifies rejection without changing bytes/protection and subsequent success
after releasing the reservation. Its source is allocation-test.cpp.

The original failed startup arm and a successful pre-guard retry remain explicit
in combined-stage-validation.json. They are not discarded or treated as proof
of a solved root cause. Subsequent allocator/property checks and paired runs
qualify the scoped checked-window profile only.
