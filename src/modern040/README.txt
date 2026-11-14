Experimental modern040 core and upstream x64 JIT

This directory supplies the pinned generated integer source and its required
type header. SOURCE.json binds them to the independently qualified core/API
sources under tests/cpu-core-upgrade/runner. Bodies/helpers come from WinUAE
d82bc254. The native frontend selects the corresponding restart-aware 040
integer bodies over an explicit physical bus. The same archive owns the 68040
page-table walker/ATCs and canonical 96-bit FPU state. FPU arithmetic,
including software completion of 68881 math operations, uses the pinned WinUAE
SoftFloat backend; enabled arithmetic conditions deliver vectors 48--54.

Build the target library from source, without an Amiga chipset dependency:
  python3 tools/build-modern040-library.py --output /ABS/modern-core-build \
    --upstream /ABS/WinUAE-at-d82bc254
For Windows supply --cxx x86_64-w64-mingw32-g++ --ar x86_64-w64-mingw32-ar.
The resulting provenance records exact compiler/commands/source/object hashes.

Then use an ordinary out-of-tree ARAnyM configure/make:
  /SOURCE/configure --disable-opengl --enable-jit-compiler --disable-jit-fpu
    --enable-addressing=normal --enable-modern040-core
    --with-modern040-library=/ABS/modern-core-build/libmodern040.a
  make -j6
Line breaks above are explanatory. Use the existing SDL/JPEG/SLIRP target
dependency flags and --host=x86_64-w64-mingw32 when cross-compiling.

This option is off by default. Its default profile is
aranym-modern040-experimental, separate from normal ARAnyM. An alternative
relative --with-modern040-profile must end in an aranym-modern040- name;
absolute paths, parent traversal and legacy profile names are refused.
The normal opt-in build selects the modern SoftFloat FPU frontend. Set
ARANYM_MODERN040_LEGACY_FPU only for an explicit legacy comparison.
It supports the qualified Linux/MinGW x86-64 JIT profile; legacy ARAnyM
FULLMMU remains disabled because the modern core owns guest paging;
unsupported combinations fail at configure time. The limited checked-memory
helper pilot remains off unless explicitly selected for its separate tests.
Its qualified scalar EAs are (An) and (d16,An), MOVE B/W/L Dn reads/writes,
and MOVEA W/L reads including base/destination aliasing. It performs checked
transfers once and stops on failure; it does not implement architectural
fault recovery. Updating, indexed, indirect and multi-transfer EAs fall back.

Physical-bus access-error recovery now distinguishes a failed transfer before
any target access from a partial or unknown device failure. Qualified bodies
deliver format7 with the correct restart/completion PC, FC/size, WB3 data and
MOVEM continuation EA. The guest handler owns pending writeback; RTE does not
silently repeat stores. Read retries occur only when the guest returns to the
faulted instruction. Post-RTE IRQ recognition waits for that instruction.
Unknown/partial transfers, locked RMW/MOVE16 faults and FPU/trace continuation
still fail explicitly; do not assume a legacy bool callback certifies atomicity.
The original standalone probe-only profile retains its separate firmware
behavior; the restart-aware frontend uses the genuine faulted PC for reads.
Production execution retains only the latest exception event and a total count;
it does not accumulate every VDI/GEMDOS trap in an unbounded history vector.
Isolated tests can explicitly retain full or bounded histories.

JIT-enabled native builds now use the guarded RAM MOVE path by default. Set
ARANYM_UPSTREAM_JIT_RAM_MOVE_OFF=1 only for an explicit comparison control. It
supports B/W/L loads, stores and memory copies using (An), (An)+, -(An) and
(d16,An), and MOVEA W/L loads. A pure pre-instruction guard checks both operands
and auto-update aliases before any transfer. Unsafe/device addresses leave the
native block with complete incoming state and execute the real interpreter
body once. Validated RAM uses existing endian-aware accessors without a
per-transfer exception wrapper. This uses normal JIT epilogues, not a host
unwind through generated code. Incoming CCR remains live at fault boundaries.
The older CHECKED_MOVE pilot remains distinct and default-off.

2026-11-12 scalar admission stage: upstream register/immediate MULU/MULS,
flags-dead MULL and EXG are now admitted. The upstream flags-producing MULL
table is null, so live-flag cases deliberately execute the genuine interpreter.
Owned immutable product/register oracles cover 284 forms, all EXG register pairs
including A7, all CCR inputs, 32/64-bit multiply/source aliases and immediates.
Linux and the actual Windows PE under Wine pass 210944 checks per run, with
75776 mandatory exact-PC native cases and 29696 deliberate live-MULL fallbacks
in each JIT arm. Wrong-result and missing-native-witness controls are rejected.
Paired owned MiNT/fVDI/XaAES/font/text/shutdown arms pass too. This is a saved
development stage, not completion of memory/control/MMU/FPU or a new RC.

The next development gate, ARANYM_UPSTREAM_JIT_EXTENDED_EA=1, admits broader
MOVE/MOVEA addressing and LEA. It remains off by default. A preflight planner
checks immutable instruction words, extension permissions, source-before-
destination address updates and both final transfer spans. Full-format
indirection can peek independently permitted ordinary RAM pointers; it never
reads a device pointer or operand. Unsafe forms use the genuine interpreter.
The original simple-MOVE guard remains the fast path. This gate retains the
TT-RAM code restriction and does not add RMW, MOVEM, stack/control, MMU or FPU
translations. Reserved full-index encodings deliberately deopt.

The owned expanded oracle has 869 literal MOVE/MOVEA/LEA forms, 55616 checks
per run and 27232 mandatory exact-PC native cases in a JIT arm. It checks
every GPR, CCR, all 256 data bytes and all 16 pointer cells, including A7 byte
updates, source/destination/index aliases and scaled-index wrap. Another 576
cases warm the same instruction with RAM before changing its address to a
read-sensitive device: the device call must execute once in the interpreter
with no increment of that instruction's native counter. Wrong results,
missing native witnesses, an inactive development gate and a fabricated
passing check count each have separate negative controls. Actual cohort
results and binary/source hashes belong to the qualification manifests;
these counts describe the corpus, not native laptop acceptance.

The separate development gate ARANYM_UPSTREAM_JIT_RMW=1 admits unary, quick
and immediate memory ALU forms through the shared planner. It remains off by
default. CLR is write-only on 040, TST/CMPI are read-only and arithmetic/logical
updates require both permissions before an operand transfer. Immediate words
precede EA extensions, including the PC-relative base. The independent host-
integer oracles each have 918 forms, 78912 checks per run, 35136 required exact-PC
native cases and 4320 RAM-warmed device deoptimizations per JIT arm. Separate
flags-live and flags-dead guests require exact compiler-variant witnesses;
the latter overwrites all target flags with native register instructions.
They check
all GPRs, CCRs, RAM bytes and pointer cells, including A7 updates. A separate
64-case actual guest format7 corpus covers a completed read plus writeback/
discard and read-fault/RTE/retry, all CCRs and exact bus counts. Wrong results,
writeback data, native witnesses, inactive gates and a fabricated PASS count
have deliberate negative controls. Qualification manifests bind actual runs
and binaries; the permission unit tests alone do not certify native behavior.
Locked operations, ADDX/SUBX memory forms, MOVEM and control flow remain excluded.

An external fallback which leaves MMURESTART pending also sets the native
JIT_END_COMPILE flag. Cached block links must return to the dispatcher before
entering the genuine restarted body; checking only the outer dispatch loop
does not protect a directly linked successor after a compiled format7 RTE.

Preparatory MOVEM permissions use a separate movem_allowed entry, leaving
the MOVE/LEA/RMW planner dispatch unchanged. It checks a nonzero register
mask's complete word/long span, the lower predecrement span, direction and
the mask-before-PC-extension ordering. Zero masks retain interpreter behavior
without adding a speculative indirect-pointer read. This is permission
planning only: no native MOVEM admission or register-transfer/fault/device
qualification is implied. The independent data oracle must check the 040's
base-in-mask value (initial minus one operand size on predecrement stores)
and postincrement-base override, as well as word sign extension and order.

ARANYM_UPSTREAM_JIT_MOVEM=1 is an isolated development experiment, default-off.
It uses the complete-span planner and existing checked helpers; an unsafe or
empty-mask instruction executes the real interpreter. The independent alias
oracle uses individual qualified MOVEs for setup/capture, so a MOVEM defect
cannot cancel inside its own input/output machinery. Its 384 cases exposed
an imported predecrement base-in-mask defect on Linux and Windows PE: a base
stored after another register used the cumulative EA instead of initial-size.
Only four W/L FF/NF generated functions are corrected, with identical FF/NF
bodies verified before and after. The exact preimage, reproducible correction
and current hash are in MOVEM-PREDECREMENT-CORRECTION.json and import-provenance.
The vendor pin remains unchanged. Corrected alias/CCR/RAM checks do not certify
the wider mask/EA, device-order, partial-transfer or MOVEM restart contracts.

The separate CM restart correction consumes the indexed instruction extension
stream while reusing the stacked calculated operand EA. The original six W/L
indexed/PC-indexed MOVEM bodies skipped the stream on CM continuation, so the
next opcode fetch landed in an EA extension. MOVEM-RESTART-CORRECTION.json pins
the exact preimage and six local changes; extract-modern040-restart.py applies
them reproducibly without changing the upstream pin. The helper fetches only
instruction words and never reevaluates an index or operand pointer. Dedicated
PC/fetch-count/saved-EA controls and guest format7/RTE tests cover this contract.
The test-only ordered bus fixture records attempted operand transfers and an
explicit BEFORE_TRANSFER failure. A partial-store discard test does not certify
write restart, arbitrary device failure atomicity, paged MMU or FPU continuation.

Native Windows/HighWire acceptance, ARM/macOS and fair benchmarks retain
their own gates. Do not replace an installed/default emulator based only on
the cross-build succeeding.

Windows installer builds may explicitly use --enable-modern040-upgrade to
reuse the standard aranym profile. This requires modern040-core and rejects
custom profiles/non-Windows hosts. The normal experimental default remains
isolated. An upgrade must preserve existing configs, NVRAM and drive data.

RC3 update1 qualifies the existing guarded extended EA, ordinary memory ALU,
MOVEM, read-only ROM-code and narrow control-flow paths together. The preserving
Windows upgrade build enables these five families by default; the development
profile described above keeps its original opt-in defaults. Each environment
gate accepts an explicit 0 to disable that family for paired/negative tests.
CONTROL additionally accepts the original individual selectors or all/1.
Cached-import and the standalone abs.w trial remain off in every build.

The x64 native entry now initializes and reserves R15 with the actual current
MEMBaseDiff, preserving Win64 callee-save behavior. Four guarded upstream
control emitters are admitted: RTS, JSR(A0), JSR abs.l and JMP(A0). Stack spans
must be permitted ordinary RAM and targets even, backed and readable; unsafe
forms deopt before a stack write. RTE/TRAP retain the interpreter boundary.
ROM admission uses a checked 64-bit code span; short end-of-ROM images deopt.
Instruction permissions remain separate from operand permissions, so allowing
ROM code never permits a device operand or an extra device pointer read.

The qualification record must bind the exact candidate to core/C-ABI checks,
native/Windows planner controls, full guest state/flags/device/fault/trace/IRQ/
FPU corpora, both closed MiNT/fVDI/font/shutdown JIT modes, and installer data
preservation. Windows PE execution under Wine is distinct from native laptop
acceptance. Paged MMU and the modern FPU are included in the target archive.
ARM and unknown partial-device continuation remain separate. The native
display capture remains independent.
