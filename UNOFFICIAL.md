# Unofficial ARAnyM development

This branch contains community development work. It is not an official ARAnyM
release, and upstream ARAnyM maintainers have not approved these builds.
The original project is at <https://github.com/aranym/aranym>.

The owner has approved public development source and build diagnostics.
Builds remain experimental until their platform acceptance gates pass;
this workflow does not automatically publish community releases.

The `fpu-jit-x64-20261116` branch adds an experimental, opt-in x86-64 FPU JIT
profile for Windows and Linux. It preserves 80-bit registers across native and
SoftFloat execution, carries FPSR/FPIAR and null/idle state across MMU boundaries,
and guards unsupported modes/operands before native execution. See
[tests/fpu-jit/README.md](tests/fpu-jit/README.md). Enabled arithmetic exception
and full FPSP continuation remain explicitly unsupported; this is not full
68040 FPU pipeline qualification. Existing profiles retain `JITFPU = No`.

The Windows modernization and the Mac ARM64 port have different qualification
status. See each platform's evidence before assuming a feature works there.

The published Beta 1 Mac download is an **interpreter baseline** with SLIRP.
The separate **Unofficial ARM JIT Test 1** release includes a real complete
68k-to-ARM64 integer JIT on Apple Silicon and 64-bit Linux. Native run
[43797819194](https://github.com/rmahlert/aranym-development/actions/runs/43797819194)
passed all 15 paired guest profiles on Apple Silicon and Linux ARM64, including
FPU fallback, and packaged Mac/installed Debian Bookworm SLIRP checks with
translated guest execution witnesses. Its source is `6cdceb01`.

The current Mac package requires macOS 15 or newer because of its bundled
Homebrew dependencies. It is ad-hoc signed, not notarized.

The 32-bit Pi path restores the complete original ARAnyM ARM32 compiler in
an isolated architecture path. The same 15 CPU profiles pass under QEMU ARM
ISA execution. Its separate package workflow passed installed SLIRP
with translated guest execution in [run 43798991271](https://github.com/rmahlert/aranym-development/actions/runs/43798991271)
at source `6d78d7a4`; QEMU results are explicitly cross-emulated,
and do not claim physical Pi acceptance.

These ARM builds use the classic physical CPU core and IEEE FPU interpreter.
FPU JIT and the Windows modern040 paged-MMU integration are not enabled on ARM.
FreeMiNT/XaAES/fVDI, GFA Basic/GBE, real Pi display/BeePi integration and measured
performance still need community hardware testing. This testing prerelease
does not assert those workload acceptance gates have passed. See
[guest tests](tests/arm64-guest/README.md) for CPU qualification and
[platform tests](tests/arm64-platform/README.md) for the remaining workload gates.

Development artifacts identify their source commit and carry an exact source
archive, license notices and runtime dependency metadata. The bundled official
EmuTOS snapshot image is paired with its exact upstream source archive and build
recipe in `data/emutos-macos.json`. Preserve those
materials when sharing an artifact. No personal boot disk or installed guest
applications belong in the public repository or public CI artifacts.

Community beta releases should use GitHub prerelease status and include the
commit, checksum, enabled features, known limitations and actual hardware test
results. GFA Basic/GBE testing on Apple Silicon is still required before
claiming that workload works.

## Unofficial Beta 1 packaging targets

Application packages only: Windows x86-64 and macOS ARM64, followed by Linux
x86-64, then Raspberry Pi `armhf` (32-bit) and `arm64` (64-bit). No personal
drive image or guest applications are included. A target listed here is not a
claim that its build or acceptance tests have passed.

BeePi publishes separate images for Pi 3, Pi 4/400 and Pi 5/500 at
<https://sites.google.com/site/beebox68k/beepi>. That hardware list does not
identify the installed userspace architecture. Select the package using
`dpkg --print-architecture` on the actual BeePi installation, rather than the
Pi model or kernel architecture. Both package architectures are requested.
Linux x86-64 comes first; Raspberry Pi packaging follows.

BeePi relies on JIT performance. Both target packages must prove actual guest
JIT execution; instruction-emitter and interpreter checks alone are insufficient.
Select the correct OS ABI and compare the same guest configuration on physical
Pi hardware before claiming a measured speed improvement.
