Experimental full upstream x86 JIT / real ARAnyM integration
==========================================================

This owned branch imports the COMPLETE pinned WinUAE d82bc254 x86 compiler,
cache/support engine and emitter. It reuses the already-proven complete Unix
WinUAE JIT baseline, rather than substituting a handwritten compiler. See
import-provenance.json for upstream file hashes, local adaptation hashes and
baseline executable/harness provenance. Original GPL notices remain intact;
COPYING in the repository applies. The pregenerated compiler/tables are now
inputs to the x86 native Makefile, so legacy gencomp cannot silently replace
them on a subsequent clean build.

Build ONLY this experimental branch in a separate owned Linux x86-64 tree:

python3 tests/modern-jit/build.py --build-dir /path/owned/build \
    --profile /home/you/path/owned/profile

The builder never installs or launches anything. Its marked configuration is
no-PIE, fixed-address native JIT, original IEEE interpreter FPU, no JIT FPU,
no MMU, no OpenGL. Configure currently selects -O3 and its normal native flags;
upstream-jit-build.json records actual binary/source hashes and requested flags.
This is a native ARAnyM-interpreter fallback integration. The separate modern040
interpreter ABI has NOT been linked to this JIT in this stage.

The compiler table admits register arithmetic, logic, shifts, moves, conditions,
and simple branches. Memory operands, implicit stack operations, traps, FPU and
other families execute their real native interpreter handlers. Upstream address
unsafe markers remain conservative. No MMIO safety, general restart, desktop,
Windows, macOS, ARM64, or performance acceptance follows from this stage.

Native interface adaptations
----------------------------
* The pinned pregenerated compiler has canonical destination field extraction
  plus conditionally emitted source extraction. Each imported compiler function
  receives one normalized canonical opcode; source extraction selects the same
  encoding. Native runtime/interpreter fetch encoding is otherwise unchanged.
* Native regflags retain their real full-EFLAGS layout. FLAGGEN bridges the
  emitter to it. Defining packed SAHF/SETO compatibility here would corrupt the
  CCR contract; the first integration experiment caught and removed that error.
* Native vm_protect returns zero on success; UAE's wrapper returns boolean.
  Actual normal executable-cache protections are unchanged.
* Native flush hooks accept upstream reason arguments. Compiler function returns
  follow native void ABI. Pointer-width MID interfaces use native x86-64 types.
* Native fault_pc is synchronized at interpreter fallback; UAE-only ir/opcode
  fields have no fabricated native equivalents. This does not qualify JIT memory
  faults, which stay outside the admitted instruction profile.
* Native allocation uses existing low-address vm_acquire. Upstream's PIE/near
  allocator and UAE natmem R15 base are not asserted for this limited profile.

Actual runtime stage
--------------------
run-bootstrap.py creates a new owned no-disk config, disables networking/audio,
and logs ROM/PRG/binary/config hashes, host monotonic deadline and exit marker.
It does not edit installed binaries or user images. Pass --jit to enable JIT.
Run only sequentially with the configured owned profile.

The exact existing EmuTOS ROM b723d04a... and NOFP.PRG ccf994b0... were reused.
Native-stage-validation.json records the checks:
* JIT disabled: boot -> unchanged guest -> NatFeat shutdown, exit0.
* Full new native cache/dispatcher, all interpreter helpers: same completion;
  9,266 cached native dispatches and zero compiled instructions.
* Admitted register/control compilation enabled: same completion; 632 emitted
  translated instructions and 9,451 cached native dispatches.

Counts are emitted-instruction counts and dispatch observations, not an exact
executed-instruction count or speed measurement. The same ROM's normal FPU,
illegal-instruction and absent-RAM read probes occurred. Earlier failed staged
experiments are retained in the external owned runtime directory. The identical
full-system success after the explicit encoding/flags corrections is a useful
integration check; it is not yet a complete architectural differential gate.

ARANYM_UPSTREAM_JIT_DIAGNOSTICS=1 enables bounded source-managed state logs
without inspecting other processes or reading MMIO. FALLBACK_ONLY and GROUP
variants (prefixed ARANYM_UPSTREAM_JIT_) select isolation controls. They are
harness controls, not product settings. No release/default setting is changed.

Remaining required qualification is tracked in beads shin-95jh, including
modern interpreter fallback, architectural state/negative controls, memory and
callbacks/events/invalidation gates, and actual Atari desktop/application checks.
