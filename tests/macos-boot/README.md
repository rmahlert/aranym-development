# Mac firmware and guest boot smoke

`run.py --app PATH --output DIRECTORY` requires native Apple Silicon. It copies
the app to a path containing spaces, preserves signed library links, creates a
disposable config and boots EmuTOS with the owned guest in `guest.S`. Success
requires the guest marker, native Cocoa video and actual host exit0 through
NF_SHUTDOWN. Timeout, a firmware panic, or a marker without shutdown fails.

`guest.hex` is assembled 68000 code from `guest.S`; no C runtime or guest disk
image is involved. Verify it and build the PRG with:

```
python3 tests/macos-boot/build-guest.py /tmp/BOOT.PRG --verify-assembler
```

This requires `m68k-atari-mint-as` and `m68k-atari-mint-objcopy`. CI can build
the tiny relocatable TOS PRG from the checked-in hex without a cross compiler.
The same program was reference-tested in the Linux interpreter. It runs through
real XBIOS Supexec, NF_STDERR and NF_SHUTDOWN. It does not qualify FreeMiNT,
XaAES, GBE, networking, FPU arithmetic or a translated guest instruction.

The older EmuTOS 1.4 release image panicked during RAM detection in the reference
profile, with JIT disabled in both MMU/non-MMU builds. The pinned newer upstream
snapshot boots and shuts down cleanly. `data/emutos-macos.json` records its exact
source archive, image hash, compiler and build command. The source archive
accompanies the app. Rebuild it by extracting that archive and running the
recorded command with the MiNT cross compiler; no local source changes are used.
The image build date reflects when the ROM was compiled.
