# Intel macOS qualification

`.github/workflows/macos-intel.yml` builds and executes the x86-64 application
on GitHub's native `macos-15-intel` runner. It connects the modern040 CPU,
integer JIT and guarded x87 FPU JIT, then packages SLIRP and the complete
non-system runtime dependency closure in a relocatable app.

The x86 JIT's low-address helper references require a non-PIE executable with
a small page-zero reservation and low text segment. The signed app uses
`allow-unsigned-executable-memory` for the Intel JIT's writable executable
cache. The Apple Silicon app retains its separate `allow-jit` entitlement.

Qualification includes relocated startup/signature checks, a real Cocoa
EmuTOS boot and clean guest shutdown, 208 exact FPU profiles, CPU interrupt and
trace regressions, hardware-probe recovery, and DHCP/DNS/UDP/TCP/HTTP with the
actual guest JIT on both supported SLIRP subnets. The default hardware suite
does not qualify cached native faults without NOP boundaries; see its README.

The app is ad-hoc signed and not notarized. Its minimum macOS version is the
maximum recorded by the application and every bundled dylib, with 15.0 as
the current testing baseline. JITFPU remains opt-in; enabled arithmetic
exception/FPSP continuation is unsupported. Desktop/GBE acceptance on a
physical tester's Mac remains separate from the automated guest checks.
