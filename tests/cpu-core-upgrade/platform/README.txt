Experimental native ARAnyM modern040 test target
==============================================
This opt-in test build links the isolated namespaced interpreter to the REAL
Atari frontend. Normal builds do not define EXPERIMENTAL_MODERN040 and retain
existing CPU behavior. No production configure setting, installation or public
release is added. Do not select JIT, MMU or signal/direct addressing for this
experiment. The bridge rejects those combinations at compile time.

Build (host C++20/Python3/Git/ar/autoconf/SDL2 development prerequisites):
python3 tests/cpu-core-upgrade/platform/build.py --upstream /absolute/path/WinUAE
The pinned source remains d82bc2549f27943d11dc59482d5388daf1c0f337. Default build
and profile paths are siblings of this source checkout, build-experimental and
profile-experimental. The latter must have a simple path beneath the user's home;
ARANYMHOME points only at that owned profile. HOME itself is not changed.

The script runs generator/core tests first, builds legacy generator/archive,
compiles the adapter against ORIGINAL ARAnyM headers and plain c-api.h, appends
namespaced modern objects to the dedicated test archive and links the frontend.
The original global regs struct is never cast to a modern type. --reuse-core
requires exact tracked source hashes and a nonsanitized provenance marker.
An existing configure tree needs a matching owned configuration marker or the
explicit --adopt-configured option, which checks the exact experimental flags.
Changing configure flags requires a fresh build directory, preventing stale
object reuse. The script does not install or launch the resulting executable.
modern040-build.json records binary/source/provenance hashes and configuration.

Actual backend contracts implemented:
- Full integer/SR/banked/control snapshots at instruction boundaries. This initial
  prototype copies per instruction for observability, so it is NOT a speed result.
- Original phys_get/put byte/word/long paths, including Atari ROM/RAM/MMIO and
  accepted RAM-window fixes. EffectiveFC selects access privilege. Original
  ARAM_PAGE_CHECK shortcuts are invalidated per callback so a cached decision
  cannot bypass the next access's FC/permissions. Failed native memory exceptions
  translate to typed modern bus faults; no repeated MMIO or fake restart.
- Original AtariReset(): hardware, NatFeats and input reset, preserving CPU state.
  The genuine generated RESET body performs privilege check and PC increment.
- Original m68k_natfeat_id/call/EmulOp host functions at a suspended private-body
  boundary. Their host exceptions cancel private completion and enter real modern
  frames. Generic external access/address exceptions are rejected without metadata.
- Original timer/event/STOP scheduler. Explicit modern IRQ entry preserves level
  and custom SCC/MFP vector identity; no inference from vector numbers. Modern
  trace owns trace flags, avoiding a second old-core trace delivery.

Current limits fail closed with MODERN040_UNSUPPORTED and controlled host exit3:
generic format7/accessfault restart/writeback, remaining helpers/MMU,
protected transparent-region enable, and nested guest EmulOp execution.
This is an experimental attempt target, not full040/LC040 compatibility. Actual
NF/IRQ/desktop behavior is not inferred merely because its callback compiles.

Controlled native Linux headless attempts, 2026-11-08:
Source includes accepted private baseline b6735e93 via merge62b5df6a, preserving
RAM-window/shutdown changes; original production checkout is untouched.
1. Owned EmuTOS1.4 ROM +NOETH.PRG,64MiB FastRAM, JIT-off, network/audio disabled,
   SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy:13attempts then unsupported RESET
   atPC00e0008c, clean exit3. No GUI/Windows launch and no disk image attached.
2. After genuine RESET/AtariReset hookup:3662attempts then unsupported transparent
   register enable, MOVEC D0,ITT0 at00e00594, nextPC00e00598; clean exit3.
   No false boot-success claim. Both logs are retained in the owned boot-smoke
   evidence directory outside tracked source; both profiles save only owned NVRAM.
3. Genuine transparent matcher/unprotected profile now passes that register
   update;3670attempts reach excluded cache opcodef4d8 at00e005b4, controlled
   exit3. Paged MMU and enabled protected regions stay blocked before fetch.
   See runner/ttr-validation.txt for1048593 matching/dispatch assertions and
   exact manual/source scope; this result is not a completed firmware boot.
4. Empty-cache maintenance semantics pass that instruction.7407attempts then
   stop on physical byte-read fault050fffff, TST.B(A0) at00e00cba, just beyond
   configured64MiB FastRAM. No fault restart/repeated reads are silently added.
   Read-probe frame delivery/discard recovery remains the next gate.
5. Narrow trace-free TST.B(An) physical-read faults now deliver genuineformat7
   for a guest discard-frame handler; generic RTE7/restart/writeback stays
   blocked. Actual RAMprobe logs00e00cba/050fffff ->00e00cc0, then real
   NF_BOOTSTRAP_ARGS/NF_BOOTSTRAP loads64994-byte NOETH.PRG at0100010c.
   The30s bounded run times out124 after a VDI640x4801plane banner, without
   expected NF_SHUTDOWN. This is partial progress, not successful completion;
   guest flow/exception diagnostics are the next gate. See read-probe-validation.
6. Diagnostics resolve the timeout: original hard-FPU CRT rejects the truthful
   absent-FPU cookie and exits before main. Separately named same-source NOFP
   built with -m68000 -msoft-float passes on BOTH old and modern cores with the
   identical PRG/hash/config: real bootstrap/guest STDERROR/NF_SHUTDOWN, exit0.
   This establishes controlled no-FPU ROM/guest bootstrap and shutdown, not a
   MiNT desktop or fullFPU/MMU result. See nofp-fixture.txt for exact evidence.
7. An owned clone of the parent's already-synced disposable MiNT test image
   (not an owner image/frozen source edit) reaches its SFP-004 absence probe.
   The bounded pure word-read/discard-handler extension handles that exact
   unmapped read. Actual fVDI7a17 installs and XaAES run_km returnsOK; execution
   then stops on unsupported SFP command-port WRITEfffffa42 at0140da1e.
   No general writeback/FPU emulation is inferred. Read-probe suite40993 PASS
   normal+ASan. Original working FPU backend is the next experimental adapter.

ROM SHA256 b723d04ae9912b8e5fdcb9bd2dffacb3d57968ab3e543b7ace5e687a04bef63b
PRG SHA256 d2dc467e81e5cb6833b88f676a914341b5b902a270fa264dca31be709936e092
These owned input assets are not committed or redistributed. To reproduce a run,
create an owned config with explicit input/HostFS paths, no IDE/disk/network,
and run from its workspace directory:
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy timeout -k5 30 /path/build/src/aranym -c /path/owned/config
Do not point the test at a user's active mounted image or installed profile.

12 additional normal+ASan/UBSan runner assertions validate platform RESET
privilege/callback/CPU preservation/failure and actual host-NatFeat-permission
exception cancellation, format0/faultPC and userRTE recovery. Existing312178core,
222frame,685LC,72815helper,503bus,90CABI,10248private assertions remain passing.
Transparent permissions and later actual NatFeat/boot paths remain next gates.

8. Explicit ARANYM_MODERN040_LEGACY_FPU=1 now delegates to original generated
   IEEE FPU bodies exactly once. Same actual FP fixture passes old+modern;
   original hard-FPU NOETH and unchanged LC040 NOFP both exit0. Own disposable
   MiNT image installs fVDI7a17, reaches XaAES/AES test shell and completes real
   GEMDOS shutdown with hostexit0. This is NOT a modern FPU/JIT/MMU claim.
   See legacy-fpu-validation.txt for actual state/endian/frame tests and limits.
9. Ordinary live MFP TimerC/customvector69 fixture now passes master STOP wake,
   ISP throwaway1+MSP frame0 and real ROM RTE bank restoration. Same fixture
   detects missing throwaway on old baseline; wrong-format control rejected.
   See mfp-irq-validation.txt; run-bootstrap.py records explicit owned no-disk
   fixture inputs/results. General nested EmulOp/fault restart remain blocked.
10. Original host EmulOp direct privilege exceptions now cancel completion just
    like delivered FPU exceptions. Ordinary user query+privileged console test
    passes everyCCR/register/frame/bank; wrong-PC checker rejected. The old
    baseline's separate post-exception PC+2 bug is tracked shin-9w5d.
    See emulop-validation.txt; nested LineA has no current caller and stays gated.
11. Private Windows paired cross-build/runtime now links and passes static
    profile/PE/import-closure checks; neither EXE launched or installed. Separate
    generated profile/NVRAM directories prevent default-profile overlap. See
    windows-validation.txt and windows-test-README.txt; root owns advance notice
    and closed-copy Windows desktop/application acceptance before benchmarking.
12. First peer Windows modern04210/currentddce boot stopped at the documented
    ordinary access-fault gate before desktop. Static bytes map to optional
    FreeType family-name hashing; root owns independent nullable-name repair.
    Exact same currentpair/kernel boot and shutdown on BOTH controlled Linux
    arms, but peer's restored original Windows arm/font evidence is still
    needed. Focused failed-MOVE/preceding-MOVEA/state-order checks pass; no
    policy/restart expansion. See peer-null-read-2026-11-08.txt.
