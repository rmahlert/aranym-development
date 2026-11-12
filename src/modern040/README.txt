Experimental modern040 core and upstream x64 JIT

This directory supplies the pinned generated integer source and its required
type header. SOURCE.json binds them to the independently qualified core/API
sources under tests/cpu-core-upgrade/runner. Bodies/helpers come from WinUAE
d82bc254. The native frontend selects the corresponding restart-aware 040
integer bodies over an explicit physical bus. Paged MMU translation is separate.
Original ARAnyM IEEE FPU service remains the backend.
This is not an assertion that every modern WinUAE CPU/MMU/FPU feature is ported.

Build the target library from source, without an Amiga chipset dependency:
  python3 tools/build-modern040-library.py --output /ABS/modern-core-build
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
The normal opt-in build enables the original IEEE FPU service automatically.
It supports the currently qualified Linux/MinGW x86-64 JIT/no-MMU profile;
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

Preparatory single-operand policies also model unary, quick and immediate
memory permissions: CLR is write-only on 040, TST/CMPI are read-only and
the arithmetic/logical updates require both permissions. Immediate words
precede EA extensions, including the PC-relative base. These policies alone
do not admit any new native RMW instruction; their separate unit tests are
not substitutes for connected native ALU, fault and read-sensitive device
qualification. Locked operations, MOVEM and control flow remain excluded.

Native Windows/HighWire acceptance, broader memory
translation, modern MMU/FPU engines, ARM/macOS and fair benchmarks retain
their own gates. Do not replace an installed/default emulator based only on
the experimental option building successfully.

Windows installer builds may explicitly use --enable-modern040-upgrade to
reuse the standard aranym profile. This requires modern040-core and rejects
custom profiles/non-Windows hosts. The normal experimental default remains
isolated. An upgrade must preserve existing configs, NVRAM and drive data.
