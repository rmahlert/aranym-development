<p align="center">
  <a href="https://aranym.github.io/">
    <img src="android/AndroidData/logo.png" alt="ARAnyM logo" width="320" height="240">
  </a>
</p>

# ARAnyM 1.2.0 — Unofficial development

Community development builds of **ARAnyM (Atari Running on Any Machine)**,
based on the original ARAnyM project. These are unofficial testing releases.
Credit for the original emulator belongs to the ARAnyM developers and contributors.

[Original ARAnyM website](https://aranym.github.io/) ·
[Original source](https://github.com/aranym/aranym) ·
[Original documentation](https://github.com/aranym/aranym/wiki)

## Recent updates

- **SLIRP networking:** added user-mode networking on Windows, macOS and Linux,
  with receive-thread, socket polling and shutdown fixes.
- **MiNTNet / NatFeat Ethernet:** corrected interrupt acknowledgement and receive
  handling for guest networking.
- **UAE 68040 / x86-64 JIT:** integrated newer, pinned WinUAE CPU/JIT code,
  paged-MMU support and SoftFloat FPU handling for Windows and Linux x86-64.
- **ARM JIT:** added ARM64 guest JIT for Apple Silicon and 64-bit Raspberry Pi,
  restored ARM32 JIT for 32-bit Pi, and added guarded native FPU arithmetic
  with interpreter fallback on both ARM architectures.
- **Display:** fixed Windows redraw and high-resolution desktop issues.
- **CPU performance and stability:** improved JIT warm-up, physical-memory
  execution, exception handling and NatFeat boundaries.
- **Windows installer:** preserves existing configurations and boot images,
  fixes runtime dependencies, and asks before replacing configured EmuTOS firmware.
- **Packages and testing:** added Mac, Linux and Raspberry Pi application packages,
  source-paired EmuTOS, checksums, and CPU/network regression checks.

The experimental Windows/Linux x86-64 FPU JIT profile now connects guarded
native arithmetic to the pinned SoftFloat fallback. Enable `JITFPU = Yes` in
`[JIT]` in an FPU JIT testing build; it remains opt-in. See
[its tests and limitations](https://github.com/rmahlert/aranym-development/releases/tag/v1.2.0-unofficial-fpu-jit-test2).
Native Windows/Linux CPU, installation and SLIRP checks have passed; full
FPU arithmetic exception/FPSP continuation remains unsupported.
The new ARM FPU JIT Test 1 builds also offer opt-in native arithmetic with
`JITFPU = Yes` and `JITSafe = Yes`, retaining the existing ARM IEEE64 precision
and interpreter fallback. ARM does not include the modern040 paged-MMU core.
FreeMiNT/XaAES/GBE and physical BeePi desktop testing is still needed.
See [platform status and test details](https://github.com/rmahlert/aranym-development/releases/tag/v1.2.0-unofficial-fpu-jit-test2).

## Latest testing downloads

Application installers/packages only; no guest hard disk image is included.

| Platform | Build | Direct download |
| --- | --- | --- |
| Windows x86-64 | Unofficial FPU JIT Test 1 | [Installer (.exe)](https://github.com/rmahlert/aranym-development/releases/download/v1.2.0-unofficial-fpu-jit-test1/aranym-1.2.0-unofficial-fpu-jit-test1-windows-x86_64-setup.exe) |
| Linux x86-64 — Ubuntu 24.04 | Unofficial FPU JIT Test 1 | [Debian package (.deb)](https://github.com/rmahlert/aranym-development/releases/download/v1.2.0-unofficial-fpu-jit-test1/aranym-1.2.0-unofficial-fpu-jit-test1-linux-amd64.deb) |
| Mac Apple Silicon — macOS 15+ | Unofficial ARM FPU JIT Test 1 | [App bundle (.tar.gz)](https://github.com/rmahlert/aranym-development/releases/download/v1.2.0-unofficial-arm-fpu-jit-test1/aranym-1.2.0-unofficial-arm-fpu-jit-test1-macos-arm64.tar.gz) |
| Raspberry Pi 64-bit — Bookworm or newer | Unofficial ARM FPU JIT Test 1 | [arm64 package (.deb)](https://github.com/rmahlert/aranym-development/releases/download/v1.2.0-unofficial-arm-fpu-jit-test1/aranym-1.2.0-unofficial-arm-fpu-jit-test1-rpi-arm64.deb) |
| Raspberry Pi 32-bit — Bookworm or newer | Unofficial ARM FPU JIT Test 1 | [armhf package (.deb)](https://github.com/rmahlert/aranym-development/releases/download/v1.2.0-unofficial-arm-fpu-jit-test1/aranym-1.2.0-unofficial-arm-fpu-jit-test1-rpi-armhf.deb) |

The FPU test build keeps FPU JIT off until you enable it. See its
[qualification results and known limitations](https://github.com/rmahlert/aranym-development/releases/tag/v1.2.0-unofficial-fpu-jit-test1).
ARM packages have separate [FPU qualification, timing and limits](https://github.com/rmahlert/aranym-development/releases/tag/v1.2.0-unofficial-arm-fpu-jit-test1).
The earlier [ARM integer JIT testing packages](https://github.com/rmahlert/aranym-development/releases/tag/v1.2.0-unofficial-arm-jit-test1) and [Beta 1 packages](https://github.com/rmahlert/aranym-development/releases/tag/v1.2.0-unofficial-beta1) remain available.

On Raspberry Pi/BeePi, use `dpkg --print-architecture` to choose `arm64` or `armhf`.
The 32-bit build has passed ARM execution tests under QEMU; physical Pi testing
remains required. The Mac app is ad-hoc signed, not notarized.

[All releases, matching source and checksums](https://github.com/rmahlert/aranym-development/releases).
Corresponding source is available for each current release:

- Windows/Linux FPU JIT Test 1: [source bundle](https://github.com/rmahlert/aranym-development/releases/download/v1.2.0-unofficial-fpu-jit-test1/aranym-1.2.0-unofficial-fpu-jit-test1-source.tar.gz) and [development branch](https://github.com/rmahlert/aranym-development/tree/fpu-jit-x64-20261116).
- Mac/Pi ARM FPU JIT Test 1: [source archive](https://github.com/rmahlert/aranym-development/releases/download/v1.2.0-unofficial-arm-fpu-jit-test1/aranym-1.2.0-unofficial-arm-fpu-jit-test1-source.tar.gz).

ARM FPU development continues on the [ARM FPU JIT branch](https://github.com/rmahlert/aranym-development/tree/arm-fpu-jit-20261116).
See [platform status, native test results and known limitations](https://github.com/rmahlert/aranym-development/releases/tag/v1.2.0-unofficial-fpu-jit-test2).

Community Raspberry Pi 5 testing of the integer JIT reported a Kronos 2.50 CPU
score of **6,238**, versus **1,567** for its stored “Rpi5 Std FVDI” reference
(about **3.98×**). This is a setup-dependent CPU benchmark comparison.

## Reporting issues

Please [report problems with these unofficial builds](https://github.com/rmahlert/aranym-development/issues/new)
in this repository. Include the build/version, host OS and architecture, steps
to reproduce, relevant configuration settings and console log. For display
problems, include a screenshot; for networking, say whether you use SLIRP or TAP.

## Original project information

The original project description, features, license and usage information are
preserved below, including its historical release information.

---

[![Build Status](https://travis-ci.org/aranym/aranym.svg?branch=master)](https://travis-ci.org/aranym/aranym)
[![Build status](https://ci.appveyor.com/api/projects/status/buvngw1mdtdo28ri/branch/master?svg=true)](https://ci.appveyor.com/project/th-otto/aranym/branch/master)

Latest snapshot: [![Download](https://api.bintray.com/packages/aranym/aranym-files/snapshots/images/download.svg) ](https://bintray.com/aranym/aranym-files/snapshots/_latestVersion#files)
Latest release: [![Download](https://api.bintray.com/packages/aranym/aranym-files/releases/images/download.svg) ](https://bintray.com/aranym/aranym-files/releases/_latestVersion#files)

<a alt="Get it from the Snap Store" href="https://snapcraft.io/aranym"><img alt="Get it from the Snap Store" src="https://snapcraft.io/static/images/badges/en/snap-store-black.svg" width="100"></a>

      ARAnyM (Atari Running on Any Machine)
      version 1.1.0 released on 2019/04/14


# License

    ARAnyM is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    ARAnyM is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with ARAnyM; if not, write to the Free Software
    Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA



# What is this?

ARAnyM is a multiplatform virtual machine (a software layer, or an emulator)
for running Atari ST/TT/Falcon operating systems and applications on almost
any hardware with many host operating systems.
The reason for writing ARAnyM is to provide Atari power users with
faster and better machines. The ultimate goal is to create a new platform
where TOS/GEM applications could continue to live forever.

# Features:

   - 68040 CPU (including MMU040)
   - 68040 and 68881/2 FPU
   - 14 MB ST-RAM and up to 3824 MB (configurable) of FastRAM
   - VIDEL, Blitter, MFP, SCC, ACIA, IKBD for highest possible compatibility
   - Sound (compatible with Atari Falcon DMA Sound)
   - Atari floppy DD/HD for connecting floppy image or real floppy drive
   - two IDE channels for connecting disk images, harddrives or CD-ROMs
   - extended keyboard and mouse support (including mouse wheel)
   - direct access to host file system via BetaDOS and MiNT xfs drivers
   - networking using ethernet emulation with a driver for MiNT-Net
   - TOS 4.04, EmuTOS, MagiC or Linux-m68k as the boot operating system
   - runs with FreeMiNT, MagiC, and any other operating system that runs
     also on real Atari computer.
   - Native CD-ROM access (under Linux, other OS: audio CD only), without
     scsi/ide/whatsoever emulation
   - USB and PCI NatFeat support for developing Atari drivers on ARAnyM

Important: ARAnyM is not finished. Consider this to be a beta version
and a work-in-progress. Sorry for incomplete and confusing documentation.
Any help with programming/documentation/porting is very welcome!


# Installing

See [INSTALL](INSTALL) file and https://github.com/aranym/aranym/wiki

You can also visit https://repology.org/metapackage/aranym/versions to check
whether your distribution has a pre-compiled version already.


# Configuring and running

See https://github.com/aranym/aranym/wiki


# Keyboard shortcuts

### Keys not found on a PC keyboard:

- Atari Help key - mapped on the F11
- Atari Undo key - mapped on the F12

### Keys not found on Atari keyboard:

- Page Up key    - mapped as Shift+Arrow Up (usual combination in GEM apps)
- Page Down key  - mapped as Shift+Arrow Down (usual combo in GEM apps)

### Special keys (most can be re-defined in the SETUP):

- Pause/Break    - invoke SETUP GUI (ARAnyM on-the-fly configuration)
- Shift+Pause    - QUIT ARAnyM
- Ctrl+Pause     - REBOOT ARAnyM
- Alt+Pause      - enter integrated debugger (only if started with "-D")

- PrintScreen    - create a screenshot in actual directory

- Alt+Ctrl+Shift+Esc - release the keyboard/mouse input focus so you can use
                 your keyboard and mouse in other host applications.
                 Can be changed to other key combo in the Settings.
                 Middle mouse does the same job.

- Scroll Lock    - switch between windowed and fullscreen mode


# Troubleshooting

If your Microsoft IntelliMouse doesn't work correctly on Linux framebuffer
console you may want to set the SDL_MOUSEDEV_IMPS2 environment variable to 1.

Example for bash: export SDL_MOUSEDEV_IMPS2=1; aranym


# More information

Read the [NEWS](NEWS) file for user visible changes.

Read [ChangeLog](ChangeLog) for internal changes.
Look at [TODO](TODO) if you want to help us.

Join our mailing list for ARAnyM users - the WEB interface for the list
is at https://groups.google.com/forum/#!forum/aranym

Visit https://github.com/aranym/ for latest information and source code.

Help us improve the Wiki documentation at https://github.com/aranym/aranym/wiki

The "Power Without The Price" is back!    https://aranym.github.io/
