# Unofficial ARAnyM development

This branch contains community development work. It is not an official ARAnyM
release, and upstream ARAnyM maintainers have not approved these builds.
The original project is at <https://github.com/aranym/aranym>.

The owner has approved public development source and build diagnostics.
Builds remain experimental until their platform acceptance gates pass;
this workflow does not automatically publish community releases.

The Windows modernization and the Mac ARM64 port have different qualification
status. See each platform's evidence before assuming a feature works there.

The Mac workflow currently validates an imported AArch64 instruction emitter
and prepares a native **interpreter baseline**. The 68k-to-AArch64 JIT is not
connected yet. The Mac baseline also has SLIRP disabled pending its platform
integration. A green workflow does not certify a guest desktop or a JIT beta.
The remaining acceptance gates are in
[tests/arm64-platform/README.md](tests/arm64-platform/README.md).

Development artifacts identify their source commit and carry an exact source
archive, license notices and runtime dependency metadata. The bundled official
EmuTOS snapshot image is paired with its exact upstream source archive and build
recipe in `data/emutos-macos.json`. Preserve those
materials when sharing an artifact. No personal boot disk or installed guest
applications belong in the public repository or public CI artifacts.

Community beta releases should use GitHub prerelease status and include the
commit, checksum, enabled features, known limitations and actual hardware test
results. GFA Basic/GBE testing on Apple Silicon is still required before
claiming that workload works.
