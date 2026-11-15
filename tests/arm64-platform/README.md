# ARM64 platform validation

Run `bash tests/arm64-platform/run.sh --require-native` on Apple Silicon.
The same command must fail on an Intel host. Without `--require-native`, an
Intel host can check instruction encodings, but prints an explicit native skip.

The test uses the pinned Amiberry instruction emitter, executes generated
integer code, checks NZCV flags, 64-bit host pointers, forward/backward branches,
and rewrites the same executable address 4096 times to check cache invalidation.
On macOS it is ad-hoc signed with hardened runtime and `allow-jit`; it uses one
MAP_JIT region, per-thread write protection and `sys_icache_invalidate`.
It does not use the callback allowlist entitlement, which is incompatible with
`pthread_jit_write_protect_np`. See
[Apple's JIT porting documentation](https://developer.apple.com/documentation/apple-silicon/porting-just-in-time-compilers-to-apple-silicon).

This is a prerequisite for integrating the guest compiler. It proves neither
68k instruction translation nor FreeMiNT/XaAES/GBE compatibility. The initial
Mac application build remains an interpreter baseline, with JIT and SLIRP
explicitly disabled. The existing SLIRP integration is Windows-specific.

Before a community JIT beta:

1. Connect the AArch64 compiler/midlayer, register state, memory and dispatch.
2. Require real guest-PC native-execution witnesses for JIT tests; reject a
   passing result obtained entirely through interpreter fallback.
3. Compare JIT on/off guest registers, flags, exceptions, IRQs, self-modifying
   code, MMU behavior and FPU fallback using the same immutable test programs.
4. Boot a licensed disposable guest through EmuTOS, FreeMiNT and XaAES; test
   resolution, redraw, HostFS, networking and a clean guest shutdown. Keep
   personal boot images and installed applications out of public Actions.
5. Test the packaged application after relocation and without Homebrew library
   paths. Native package and interpreter smoke checks are separate from JIT.
6. Have lp run GFA Basic/GBE workloads, comparing output, stability and timing.
   Record the source commit, artifact SHA-256, config and test results. Publish
   a clearly labeled unofficial prerelease with corresponding source only
   after these gates pass. Signing/notarization is a separate distribution step.
