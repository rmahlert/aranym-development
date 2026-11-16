# Unofficial ARAnyM development

This branch contains community development work. It is not an official ARAnyM
release, and upstream ARAnyM maintainers have not approved these builds.
The original project is at <https://github.com/aranym/aranym>.

The owner has approved public development source and build diagnostics.
Builds remain experimental until their platform acceptance gates pass;
this workflow does not automatically publish community releases.

The Windows modernization and the Mac ARM64 port have different qualification
status. See each platform's evidence before assuming a feature works there.

The published Beta 1 Mac download is an **interpreter baseline** with SLIRP.
This integration branch is connecting the complete pinned ARM64 guest compiler
to ARAnyM. Its separate `ARM64 guest JIT integration` workflow requires actual
native guest execution witnesses and paired JIT on/off correctness tests on
Apple Silicon and Linux ARM64. Passing compiler or emitter checks alone does
not qualify a guest JIT build. No ARM JIT community release has been qualified.
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

## Unofficial Beta 1 packaging targets

Application packages only: Windows x86-64 and macOS ARM64, followed by Linux
x86-64, then Raspberry Pi `armhf` (32-bit) and `arm64` (64-bit). No personal
drive image or guest applications are included. A target listed here is not a
claim that its build or acceptance tests have passed.

BeePi publishes separate images for Pi 3, Pi 4/400 and Pi 5/500 at
<https://sites.google.com/site/beebox68k/beepi>. That hardware list does not
identify the installed userspace architecture. Select the package using
`dpkg --print-architecture` on the actual BeePi installation, rather than the
Pi model or kernel architecture. Both package architectures are requested.
Linux x86-64 comes first; Raspberry Pi packaging follows.

BeePi relies on JIT performance. The ARM64 guest JIT remains an explicit gate
for a performance replacement; the existing Mac interpreter acceptance and
ARM64 emitter tests do not establish a working ARM64 guest JIT on Raspberry Pi.
