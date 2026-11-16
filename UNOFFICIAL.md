# Unofficial ARAnyM builds and platform status

These are community testing builds of ARAnyM, not official upstream releases.
Original project: [ARAnyM website](https://aranym.github.io/) and
[upstream source](https://github.com/aranym/aranym).

## Current downloads

All packages install the application only. Keep your existing configuration,
firmware and boot drive. No personal hard disk image or guest applications are
included. Original Beta 1 downloads remain available as an earlier baseline.

| Platform | Testing release | CPU and FPU | Qualification |
| --- | --- | --- | --- |
| Windows x86-64 | [FPU JIT Test 1](https://github.com/rmahlert/aranym-development/releases/tag/v1.2.0-unofficial-fpu-jit-test1) | Modern040 / integer JIT; guarded FPU JIT is opt-in | Native Windows CPU, installation, preserving upgrade and SLIRP passed |
| Linux x86-64 / Ubuntu 24.04 | [FPU JIT Test 1](https://github.com/rmahlert/aranym-development/releases/tag/v1.2.0-unofficial-fpu-jit-test1) | Modern040 / integer JIT; guarded FPU JIT is opt-in | Native Linux CPU, installed Debian package and SLIRP passed |
| Apple Silicon / macOS 15+ | [ARM FPU JIT Test 1](https://github.com/rmahlert/aranym-development/releases/tag/v1.2.0-unofficial-arm-fpu-jit-test1) | ARM64 integer JIT; guarded native FPU is opt-in | Native Apple Silicon CPU/FPU and packaged application SLIRP passed |
| Raspberry Pi arm64 / Bookworm or newer | [ARM FPU JIT Test 1](https://github.com/rmahlert/aranym-development/releases/tag/v1.2.0-unofficial-arm-fpu-jit-test1) | ARM64 integer JIT; guarded native FPU is opt-in | Native Linux ARM64 CPU/FPU and installed Debian package SLIRP passed; physical Pi testing remains |
| Raspberry Pi armhf / Bookworm or newer | [ARM FPU JIT Test 1](https://github.com/rmahlert/aranym-development/releases/tag/v1.2.0-unofficial-arm-fpu-jit-test1) | ARM32 integer JIT; guarded native FPU is opt-in | CPU/FPU and installed-package SLIRP passed under QEMU; physical Pi testing remains |

The [README download table](https://github.com/rmahlert/aranym-development#latest-testing-downloads)
links directly to each installer/package. Every release provides corresponding
source and SHA256SUMS. The Mac app is ad-hoc signed, not notarized; its bundled
libraries require macOS 15 or newer. The Beta 1 Mac package is an earlier
interpreter baseline, separate from the current ARM JIT test package.

## Windows and Linux FPU JIT

FPU JIT Test 1 was built from `04aa43ed`. It is off by default. To try it:

```ini
[JIT]
JIT = Yes
JITSafe = Yes
JITFPU = Yes
```

Set `JITFPU = No` to retain the classic-compatible physical FPU path. Native
admission covers ordinary extended/nearest arithmetic and moves. Other rounding
modes, explicit single/double operations, transcendentals, quotient operations
and exceptional values use the pinned WinUAE SoftFloat frontend. Exact 80-bit
registers, FPSR/FPIAR and null/idle state are preserved across core boundaries.
Paging and tracing use the modern interpreter.

Enabled arithmetic exception/FPSP pending-state continuation, packed decimal
and undefined encodings remain unsupported. This is not complete FPU hardware
coverage. ARM has a separate guarded FPU adapter; the modern040 paged-MMU
core is not enabled there.
See the [FPU tests and limitations](https://github.com/rmahlert/aranym-development/blob/fpu-jit-x64-20261116/tests/fpu-jit/README.md).

## ARM FPU JIT

[ARM FPU JIT Test 1](https://github.com/rmahlert/aranym-development/releases/tag/v1.2.0-unofficial-arm-fpu-jit-test1)
packages use application source `37fb6f49`. Enable `JITFPU = Yes` and
`JITSafe = Yes` in the existing JIT configuration to try native arithmetic.
It remains off by default; `JITFPU = No` keeps the prior FPU interpreter path.

The adapter emits real ARM64 or ARM32/VFP register arithmetic, including
moves, square root, absolute value, negation, add/subtract/multiply/divide,
compare/test and 68040 single/double forms. Checks precede native effects;
other operands, rounding modes, enabled arithmetic exceptions and operations
outside admission use the existing interpreter. Precision remains the ARM
IEEE64 profile. This is not an 80-bit SoftFloat core, complete FPSP exception
continuation or a paged-MMU JIT. See [tests and limits](https://github.com/rmahlert/aranym-development/blob/arm-fpu-jit-20261116/tests/arm-fpu/README.md).

Native Apple Silicon and Linux ARM64 passed 221 raw output profiles across
interpretation, integer JIT and FPU JIT, plus 15 CPU boundary profiles and
SLIRP with FPU JIT enabled. The installed Bookworm Pi64 package passed on
native ARM64. The Pi32 package passed the same CPU/FPU/network gates under
QEMU; physical Pi/BeePi application testing remains required.

Final arithmetic timing brackets ten million independently counted iterations
with host monotonic timestamps. Diagnostic counters are disabled for timings;
a separate run witnesses over 40 million emitted FP operations.

| Native hosted system | FPU interpreter | FPU JIT | Ratio on this loop |
| --- | ---: | ---: | ---: |
| Apple Silicon | 0.578s | 0.433s | 1.33× |
| Linux ARM64 / Debian | 0.628s | 0.300s | 2.09× |

These results describe this arithmetic loop, not desktop or physical Pi speed.
The release validation archive includes raw results and updated oracle source;
application binaries and their corresponding source remain exactly `37fb6f49`.

## Published test evidence

- [ARM FPU native packages](https://github.com/rmahlert/aranym-development/actions/runs/43980153821): Apple Silicon, Linux ARM64 and installed Pi64 Debian package CPU/FPU/SLIRP gates passed.
- [ARM32 FPU package under QEMU](https://github.com/rmahlert/aranym-development/actions/runs/43980153675): CPU/FPU/installed-network gates passed; emulated ISA evidence only.
- Final host-timed oracle on exact packages: [Apple Silicon job](https://github.com/rmahlert/aranym-development/actions/runs/43981808138/job/133699910117) and [native Debian ARM64](https://github.com/rmahlert/aranym-development/actions/runs/43982219530).

- [Windows native qualification](https://github.com/rmahlert/aranym-development/actions/runs/43809291156): the exact downloaded installer passed 208 bit-exact FPU profiles, CPU boundary regressions, positive native FPU execution, clean installation, runtime payload verification, preserving upgrade and SLIRP on both subnets.
- [Linux x86-64 native qualification](https://github.com/rmahlert/aranym-development/actions/runs/43808156740): 208 bit-exact FPU profiles, CPU boundary regressions, positive native FPU execution, installed-package checks and SLIRP on both subnets passed.
- [Apple Silicon and Linux ARM64 native qualification](https://github.com/rmahlert/aranym-development/actions/runs/43797819194): 15 paired CPU profiles, mixed integer/FPU fallback and packaged Mac/installed Debian Bookworm SLIRP passed with translated guest execution. Application source: `6cdceb01`.
- [ARM32 qualification under QEMU](https://github.com/rmahlert/aranym-development/actions/runs/43798991271): the same 15 CPU profiles and installed-package SLIRP passed with translated guest execution. Application source: `6d78d7a4`. These are emulated-ISA results, not physical Pi acceptance.

The FPU release validation archive includes raw reports and qualification
checkout `b3427138`, which fixes Windows test-source line endings without
changing the application binary. Network checks exercise DHCP, DNS, ARP, UDP,
TCP/HTTP, receiver restart and shutdown with the receiver active.

The arithmetic loop medians were 17 / 295 / 23 guest timer ticks on Windows and
18 / 447 / 25 on Linux for classic IEEE / SoftFloat / guarded FPU JIT. The native
path beats SoftFloat on this loop but still trails classic IEEE. These results
do not establish a desktop, GBE or whole-system speed improvement.

## Application and hardware testing

FreeMiNT/XaAES/fVDI desktop work, GFA Basic/GBE, physical Pi display/BeePi
integration and real application performance still need community testing.
On BeePi, use `dpkg --print-architecture` to choose `arm64` or `armhf`; the Pi
model or kernel architecture alone does not identify the userspace ABI.
BeePi information: <https://sites.google.com/site/beebox68k/beepi>.

Keep source archives, license notices, firmware provenance and checksums with
redistributed packages. The bundled EmuTOS image is paired with its exact source
and recipe in `data/emutos-macos.json`.

[Report issues](https://github.com/rmahlert/aranym-development/issues/new) with
the platform, build, JIT settings, steps to reproduce and relevant console log.
For networking, specify SLIRP or TAP. Please keep private drive images out of
public issue reports.
