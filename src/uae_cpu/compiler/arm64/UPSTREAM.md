# AArch64 backend provenance

`codegen_arm64.h` is an unchanged import from
[Amiberry](https://github.com/BlitterStudio/amiberry), commit
`2cff10571b25a5f138a4771c6694d2ed08dbcdb5`, path
`src/jit/arm/codegen_arm64.h`.

SHA-256: `9d4c8a47689ac6b4b7ab34285d0482edc6be9798192655a473be044b4a1ca4e5`.

The source retains its UAE4ARM, UAE and CCG copyright notices and its
GPL version 2 or later license notice. See the source header and COPYING.

The integration branch additionally imports the complete ARM64 guest compiler,
register allocator, native midlayers, dispatch/cache engine and generated tables
from the same commit. `SOURCE.json` records every upstream path and hash and the
hash of its locally adapted form. Original notices are retained.

`aranym_arm64.cpp`, `aranym_arm64.h` and `native_flags.h` adapt the real ARAnyM
register state, NZCV/CCR representation, physical memory helpers, native dispatch,
interrupts and longjmp exception boundary. Generated memory operations use the
checked native bank callbacks. Floating-point instructions retain the native
interpreter; FPU JIT and the paged modern040 core are not yet enabled here.

Build explicitly with `--enable-jit-compiler --enable-arm64-jit --disable-jit-fpu
--enable-addressing=normal`. macOS packaging requires `--jit-build-dir` and adds
the allow-jit entitlement. This remains experimental until native guest
correctness, exceptions, IRQs, invalidation and desktop workloads pass. A
successful emitter or compiler build alone does not establish guest JIT support.
