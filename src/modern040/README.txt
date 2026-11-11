Experimental modern040 core and upstream x64 JIT

This directory supplies the pinned generated integer source and its required
type header. SOURCE.json binds them to the independently qualified core/API
sources under tests/cpu-core-upgrade/runner. Bodies/helpers come from WinUAE
d82bc254; explicit integration guards still exclude full guest MMU and general
access-error restart. Original ARAnyM IEEE FPU service remains the backend.
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

Native Windows/HighWire acceptance, general fault recovery, broader memory
translation, modern MMU/FPU engines, ARM/macOS and fair benchmarks retain
their own gates. Do not replace an installed/default emulator based only on
the experimental option building successfully.
