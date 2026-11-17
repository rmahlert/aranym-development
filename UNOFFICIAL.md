# Unofficial ARAnyM builds and platform status

These are community testing builds, not official upstream releases.
Original project: [ARAnyM website](https://aranym.github.io/) and
[upstream source](https://github.com/aranym/aranym).

## Current downloads

[Unofficial FPU JIT Test 2](https://github.com/rmahlert/aranym-development/releases/tag/v1.2.0-unofficial-fpu-jit-test2)
uses application source `3c138e48840eb60d12d3b2394625f7443868308d` on every
platform. The packages install the application only. Keep your configuration
and boot drive; no guest hard-disk image or installed guest system is included.

| Platform | CPU and FPU | Qualification |
| --- | --- | --- |
| Windows x86-64 | Modern040 / integer JIT; guarded native FPU with SoftFloat fallback | Exact installer, clean install, runtime DLLs, preserving reinstall, CPU/FPU, strict fault frames and SLIRP passed natively |
| Linux x86-64 / Ubuntu 24.04 | Modern040 / integer JIT; guarded native FPU with SoftFloat fallback | Native CPU/FPU, strict fault frames, installed Debian package and SLIRP passed |
| Intel Mac / macOS 15+ | Modern040 / integer JIT; guarded native FPU with SoftFloat fallback | Native x86-64 build, relocated Cocoa boot, CPU/FPU, strict fault frames and packaged SLIRP passed |
| Apple Silicon / macOS 15+ | ARM64 integer JIT; guarded native FPU with interpreter fallback | Native Apple Silicon CPU/FPU and packaged application SLIRP passed |
| Raspberry Pi arm64 / Bookworm or newer | ARM64 integer JIT; guarded native FPU with interpreter fallback | Native Linux ARM64 CPU/FPU and installed Bookworm package SLIRP passed; physical Pi testing remains |
| Raspberry Pi armhf / Bookworm or newer | ARM32 integer JIT; guarded native FPU with interpreter fallback | CPU/FPU, strict fault frames and installed-package SLIRP passed under QEMU; physical Pi testing remains |

The [README download table](README.md#latest-testing-downloads) links directly
to each installer/package. The release includes matching source, validation
reports and SHA256SUMS. Older test builds remain available for comparison.

Both Mac apps are ad-hoc signed, not notarized. Bundled libraries require
macOS 15 or newer, including the Intel build. Linux x86-64 targets Ubuntu
24.04-era libraries; the Pi packages target Debian Bookworm. On BeePi, use
`dpkg --print-architecture` to choose `arm64` or `armhf`; the Pi model or
kernel architecture alone does not identify the userspace ABI.

## Enabling FPU JIT

FPU JIT remains off by default. To try native arithmetic:

```ini
[JIT]
JIT = Yes
JITSafe = Yes
JITFPU = Yes
```

Set `JITFPU = No` to return to the prior FPU path.

On x86-64, native admission covers ordinary extended/nearest arithmetic and
moves. Other rounding modes, explicit single/double operations, transcendentals,
quotient operations and exceptional values use the pinned WinUAE SoftFloat
frontend. Exact 80-bit registers, FPSR/FPIAR and null/idle state are preserved
across core boundaries. Paging and tracing use the modern interpreter. Test 2
reduces guard overhead without widening native admission and corrects finite
FCMP operations that could incorrectly add an inexact exception flag.
See [x86-64 tests and limits](tests/fpu-jit/README.md).

ARM emits ARM64 or ARM32/VFP arithmetic for admitted moves, square root,
absolute value, negation, add/subtract/multiply/divide, compare/test and 68040
single/double forms. Test 2 checks operands directly in generated code and
reuses them, retaining the previous admission rules and a selectable C guard
for comparison. Other operands, rounding modes, enabled arithmetic exceptions
and operations outside admission use the existing interpreter. Precision
remains the existing ARM IEEE64 profile; ARM does not include the modern040
paged-MMU core or an 80-bit SoftFloat adapter.
See [ARM tests and limits](tests/arm-fpu/README.md).

## Performance measurements

Timings bracket ten million independently counted guest arithmetic iterations
with host monotonic timestamps. Diagnostic counters are off during timing;
a separate run proves 49,999,940 native FP operations. Each row is a separate
host. These are register-arithmetic results, not desktop or application speed.

| Native hosted system | Classic IEEE | Pinned SoftFloat | Native FPU JIT | Classic/native ratio |
| --- | ---: | ---: | ---: | ---: |
| Windows x86-64 | 1.719 s | 20.375 s | 1.375 s | 1.25× |
| Linux x86-64 | 2.209 s | 77.487 s | 1.717 s | 1.29× |
| Intel Mac | 2.583 s | 141.872 s | 1.272 s | 2.03× |

A separate same-host x64 comparison of the previous and reduced-overhead native
guards measured 1.268 s versus 0.942 s (1.35×) on this loop. A small POV-Ray
render produced identical pixels before and after that change, without a clear
render-time improvement. This is not evidence of faster whole-application work.

ARM uses balanced, interleaved comparisons of the previous C guard and the new
inline guard on the same binary. The validation reports retain each pair,
integer controls and host drift. On native Linux ARM64, median times were
0.306 s (C guard) and 0.192 s (inline); the median paired improvement was
1.60×, with integer controls varying by 0.43% from minimum to maximum.
Apple Silicon measured 0.347 s and 0.220 s, with a median paired improvement
of 1.59×, but its controls varied by 31.7%. Every pair favored the inline
guard; those noisier Mac samples support a narrower conclusion than a fixed
application speedup. ARM32/QEMU timings establish no physical Pi speed claim.

Community Raspberry Pi 5 testing of the earlier integer JIT reported a Kronos
2.50 CPU score of 6,238 versus 1,567 for its stored “Rpi5 Std FVDI” reference
(about 3.98×). This is a setup-dependent comparison of that CPU benchmark.

## Published test evidence

- [Windows native installer](https://github.com/rmahlert/aranym-development/actions/runs/44154649321): exact installer hash and installed binary, eight runtime DLL load checks, 208 bit-exact FPU profiles, 768 FCMP cases, five CPU profiles, four strict hardware profiles, counted native execution, both SLIRP subnets and preserving reinstall.
- [Linux x86-64 native](https://github.com/rmahlert/aranym-development/actions/runs/44152646112): CPU/FPU/comparison tests, counted timing, strict fault frames, installed Debian package and SLIRP. The exact stripped package also passed local CPU/FPU and strict fault-frame rechecks.
- [Intel Mac native](https://github.com/rmahlert/aranym-development/actions/runs/44152640478): dependency closure/signing/relocation, Cocoa guest boot, 208 FPU profiles, 768 FCMP cases, CPU regressions, strict fault frames, counted timing and SLIRP.
- [Apple Silicon and Linux ARM64 native](https://github.com/rmahlert/aranym-development/actions/runs/44152641276): 266 raw FPU cases across three execution modes and both guards, 15 CPU boundary profiles, interleaved guard timing and packaged/installed SLIRP. The Bookworm Pi64 package is qualified on native ARM64.
- [ARM32 under QEMU](https://github.com/rmahlert/aranym-development/actions/runs/44152641331): the same FPU corpus with both guards, CPU boundary profiles, strict fault frames and installed SLIRP. The exact stripped package also passed CPU and strict-frame rechecks. These are emulated-ISA results.

Network tests cover DHCP, DNS, ARP, UDP, TCP/HTTP, receiver restart and shutdown
with the receiver active, on the default and a custom SLIRP subnet. They run
with integer and FPU JIT enabled.

## Compatibility and remaining limits

- JIT interpreter dispatch now records the fault PC before fetching. The x64
  and ARM32 signal paths preserve original fault address aliases and data
  function codes. Strict tests check all data/address registers, SR/CCR, frame
  PC, fault address and SSW, including repeated faults through different aliases.
- **Cached native hardware faults without NOP boundaries are not generally
  precise.** The passing strict profiles do not cover that mode. A broader
  recovery prototype passed correctness checks but substantially slowed RAM
  reads and is excluded. Upstream #116 remains open; booting GBE 3.74 to its
  dialog does not qualify the original GBE 3.76 editor workflow.
- **Enabled arithmetic exception/FPSP pending-state continuation remains
  unsupported.** Packed decimal and undefined encodings are also incomplete.
- The original upstream #18 software-fdlibm/cache reproduction did not fail in
  current interpreter, cache-zero or JIT tests. A small POV-Ray scene was stable
  across cache settings within each FPU frontend. Classic IEEE and pinned
  SoftFloat produced different images; that difference remains under
  investigation. This does not establish that every reported #18 case is fixed.
- FreeMiNT/XaAES/fVDI desktop work, GFA Basic/GBE editor use, physical Pi display
  and BeePi integration still require community testing. ARM64 qualification
  does not include the x64/ARM32 strict hardware-frame gate.

Keep source archives, license notices, firmware provenance and checksums with
redistributed packages. The bundled EmuTOS image is paired with exact source
and recipe in `data/emutos-macos.json`.

[Report issues](https://github.com/rmahlert/aranym-development/issues/new) with
the platform, build, JIT settings, reproduction steps and relevant console log.
For networking, specify SLIRP or TAP. Keep personal drive images out of public
issue reports. BeePi information: <https://sites.google.com/site/beebox68k/beepi>.
