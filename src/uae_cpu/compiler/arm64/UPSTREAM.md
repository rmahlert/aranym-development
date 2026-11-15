# AArch64 emitter provenance

`codegen_arm64.h` is an unchanged import from
[Amiberry](https://github.com/BlitterStudio/amiberry), commit
`2cff10571b25a5f138a4771c6694d2ed08dbcdb5`, path
`src/jit/arm/codegen_arm64.h`.

SHA-256: `9d4c8a47689ac6b4b7ab34285d0482edc6be9798192655a473be044b4a1ca4e5`.

The source retains its UAE4ARM, UAE and CCG copyright notices and its
GPL version 2 or later license notice. See the source header and COPYING.

This import currently supports only the standalone platform validation test.
It is not connected to ARAnyM's guest compiler, register allocator, memory
accesses, exception handling or dispatch loop. Passing the platform test does
not establish 68k JIT correctness or performance.
