PRIVATE EXPERIMENTAL ARAnyM modern040 desktop test
=================================================
Neither executable has been launched on Windows by this build session.
This is an opt-in test folder, not an installer or public release. It does not
replace installed/default ARAnyM. Source changes are on local cpu-core-harness;
WinUAE integer source pin d82bc2549f27943d11dc59482d5388daf1c0f337. Complete build
recipe is tests/cpu-core-upgrade/platform/build-windows.py; build inputs,
compiler/options, profile/config hashes are in BUILD-IDENTITY.json. Every runtime
file/DLL has an exact SHA256 in SHA256-MANIFEST.json. Source/license authorship
is preserved; see licenses. No guest image, ROM, fonts or user config included.

What this tests
Modern generated 68040 integer interpreter + ORIGINAL ARAnyM IEEE FPU backend.
It is not a current WinUAE FPU or native JIT port. JIT/JITFPU/MMU are disabled;
general access-fault restart/RTE7/writeback, tracedFPU/arithmetic-trap modes and
nested guest reentry fail closed. No performance improvement has been claimed.
Linux qualification passed actual ROM bootstrap, original FPU math/rounding/
BE-memory/FSAVE/FTRAP-RTE, user/master/NatFeat/EmulOp frames, live TimerC custom
IRQ/STOP/banked RTE, and owned MiNT/fVDI/XaAES AES-shell/real shutdown. Windows
compiler/ABI/runtime execution and actual applications remain untested gates.

Before first Windows test (coordinate advance notice with the owner)
1. Close ARAnyM and use a CLOSED DISPOSABLE COPY of guest disk/HostFS and ROM.
   Never use the active owner image, ordinary default config or shared mounted
   disk in both processes. Start with known64MiB FastRAM, JIT=No, JITFPU=No;
   hold ROM/guest fVDI/fonts/resolution/video and boot selections constant.
   Initial qualification uses Linux-accepted fVDI7a17. A different current guest
   pair is an explicitly separate acceptance gate, not an implicit replacement.
2. Make an explicit test config with ABSOLUTE paths to those owned copies.
   Do not reuse normal FastRAM2000/JITYes settings for first qualification.
   Both EXEs compile normal addressing, NFJPEG and SLIRP; initially keep network
   startup disabled as in Linux acceptance. Test networking after CPU desktop.
3. Launch ONLY through a command supplying that explicit test config:
   run-modern-core.cmd "D:\owned-core-test\modern.cfg" >modern.log 2>&1
   run-original-baseline.cmd "D:\owned-core-test\baseline.cfg" >baseline.log 2>&1
   The modern wrapper explicitly sets ARANYM_MODERN040_LEGACY_FPU=1; the baseline
   clears it. The wrappers refuse an omitted config. Run serially, never both
   against one image. Separate cloned image copies from the same closed source
   make paired acceptance reproducible. Restore matching state before each run.

Profile isolation
Actual generated config headers/preprocessor AND linked PE profile strings were
checked. Config/NVRAM folders are:
 modern:   %USERPROFILE%\aranym-modern040-experimental
 baseline: %USERPROFILE%\aranym-modern040-baseline
They do not use %USERPROFILE%\aranym\nvram. HOME is not changed. Use fresh owned
profiles for the pair and record identical initial NVRAM if comparing results.
An explicit -c selects the test config; profile-owned NVRAM is still saved on
exit. Supplied wrappers perform no migration/config edits/image copying.

Capture acceptance before any benchmark
Record manifest/exe/DLL hashes, exact config/ROM/input image hash, guest fVDI pair,
boot selections and host. Confirm desktop, mouse, simple GEM drawing/font/text,
ordinary HighWire page/open/file-selector flow, then real GEMDOS shutdown exit0.
Capture unexpected MODERN040_UNSUPPORTED line and full log; do not suppress it or
work around unsupported restart by replaying accesses. Baseline is the matched
original core from the SAME source/config/features, not an installed JIT build.

Performance comes after correctness: use same benchmark binary/version/hash,
restored guest state/config, host, CPU/RAM/JIT/FPU, resolution/depth/fVDI/fonts/
video backend; repeat and separate CPU/memory/graphics. GEMBench or owned Kronos
may be used without redistribution. This first bridge copies full integer state
per instruction and checks events each instruction; it is a correctness
prototype, not a speed-tuned system or a claim about future x86/ARM64 JIT gains.
