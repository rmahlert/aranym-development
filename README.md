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

- **Falcon audio and DSP.** Fixed left/right channel alignment when sound DMA
  starts or restarts, and corrected frame sync on DSP serial receive overruns.
  The emulator gives the 68k CPU time to run during heavy DSP processing.
- **MIDI recording.** Improved input byte pacing when the host delivers
  PortMidi events in batches.
- **IDE and x86-64 FPU.** Added ATA multiple-sector transfers and extended
  native JIT coverage for 68040 floating-point addition, subtraction and division.

- **Falcon DSP56001.** Updated the emulator with work from Hatari, including DSP
  instruction timing and host-port transfers. Hardware timing comparisons are ongoing.
- **MIDI.** Select separate input and output ports in Setup → MIDI, or list host
  devices with `aranym --midi-list`. The emulated 6850 now schedules serial
  transfers and interrupts. Physical interface latency and Cubase timing still
  need measurements.
- **VIDEL and fVDI.** Improved redraws, Falcon palette handling and mouse capture
  when the emulator window regains focus.

- **SLIRP networking:** user-mode networking on Windows, both Mac architectures
  and Linux, with receive-thread, socket polling and shutdown fixes.
- **MiNTNet / NatFeat Ethernet:** corrected interrupt acknowledgement and receive
  handling for guest networking.
- **UAE 68040 / x86-64 JIT:** newer, pinned WinUAE CPU code, paged-MMU support
  and guarded native FPU arithmetic backed by SoftFloat on Windows, Linux and
  Intel Mac.
- **ARM JIT:** ARM64 guest JIT for Apple Silicon and 64-bit Raspberry Pi,
  ARM32 JIT for 32-bit Pi, and guarded native FPU arithmetic on both.
- **FPU performance.** The x86-64 JIT accelerates common arithmetic, memory
  transfers and comparisons used by Kronos and HighWire. Floating-point flags and
  classic round-to-nearest-even conversions have been corrected. ARM retains
  its native FPU paths. Enable native arithmetic with `JITFPU = Yes`.
- **Compatibility:** corrected finite FCMP status, interpreter fault PCs and
  original fault addresses/data function codes in x64 and ARM32 signal faults.
- **Display:** Windows redraw and high-resolution desktop fixes.
- **Windows installer:** preserves configurations and boot images, includes
  required runtime dependencies, and preserves configured EmuTOS firmware.
- **Packages and testing:** added a native Intel Mac app, expanded exact FPU,
  CPU/fault-frame and installed-network checks, plus counted arithmetic timings.

## Latest testing downloads

**Unofficial Beta 3** updates the emulator application. Keep your existing
configuration, guest programs and boot drive.

| Platform | Direct download |
| --- | --- |
| Windows x86-64 | [Installer (.exe)](https://github.com/rmahlert/aranym-development/releases/download/v1.2.0-unofficial-beta3/aranym-1.2.0-unofficial-beta3-windows-x86_64-setup.exe) |
| Linux x86-64 — Ubuntu 24.04 | [Debian package (.deb)](https://github.com/rmahlert/aranym-development/releases/download/v1.2.0-unofficial-beta3/aranym-1.2.0-unofficial-beta3-linux-amd64.deb) |
| Mac Intel — macOS 15+ | [App bundle (.tar.gz)](https://github.com/rmahlert/aranym-development/releases/download/v1.2.0-unofficial-beta3/aranym-1.2.0-unofficial-beta3-macos-x86_64.tar.gz) |
| Mac Apple Silicon — macOS 15+ | [App bundle (.tar.gz)](https://github.com/rmahlert/aranym-development/releases/download/v1.2.0-unofficial-beta3/aranym-1.2.0-unofficial-beta3-macos-arm64.tar.gz) |
| Raspberry Pi 64-bit — Bookworm or newer | [arm64 package (.deb)](https://github.com/rmahlert/aranym-development/releases/download/v1.2.0-unofficial-beta3/aranym-1.2.0-unofficial-beta3-rpi-arm64.deb) |
| Raspberry Pi 32-bit — Bookworm or newer | [armhf package (.deb)](https://github.com/rmahlert/aranym-development/releases/download/v1.2.0-unofficial-beta3/aranym-1.2.0-unofficial-beta3-rpi-armhf.deb) |

[Release notes, portable packages, validation and checksums](https://github.com/rmahlert/aranym-development/releases/tag/v1.2.0-unofficial-beta3) ·
[Matching source bundle](https://github.com/rmahlert/aranym-development/releases/download/v1.2.0-unofficial-beta3/aranym-1.2.0-unofficial-beta3-source.tar.gz) ·
[Platform status and known limitations](https://github.com/rmahlert/aranym-development/releases/tag/v1.2.0-unofficial-beta3)

FPU JIT is off by default. To try it, set `JIT = Yes`, `JITSafe = Yes` and
`JITFPU = Yes` in your existing `[JIT]` configuration. Set `JITFPU = No` to
return to the prior FPU path. Windows, Linux x86-64 and Intel Mac use guarded
native arithmetic with pinned SoftFloat fallback; ARM retains its existing
IEEE64 precision and interpreter fallback, without the modern040 paged-MMU core.
Enabled arithmetic exception/FPSP continuation and general precise cached-native
hardware-fault recovery remain incomplete.

Both Mac apps are ad-hoc signed, not notarized; their bundled libraries require
macOS 15 or newer. On Raspberry Pi/BeePi, use `dpkg --print-architecture` to
choose `arm64` or `armhf`. ARM32 qualification uses QEMU; physical Pi/BeePi and
FreeMiNT/XaAES/GBE application testing remains necessary.

The [earlier testing releases](https://github.com/rmahlert/aranym-development/releases)
remain available. Community Raspberry Pi 5 testing of the integer JIT reported
a Kronos 2.50 CPU score of **6,238**, versus **1,567** for its stored
“Rpi5 Std FVDI” reference (about **3.98×**). This is a setup-dependent CPU
benchmark comparison. Current FPU measurements and their workload limits are
in [the Beta 2 platform notes](https://github.com/rmahlert/aranym-development/releases/tag/v1.2.0-unofficial-beta2).

## Reporting issues

Please [report problems with these unofficial builds](https://github.com/rmahlert/aranym-development/issues/new)
in this repository. Include the build/version, host OS and architecture, steps
to reproduce, relevant configuration settings and console log. For display
problems, include a screenshot; for networking, say whether you use SLIRP or TAP. For MIDI timing reports, include
the host interface, selected ports, music software and how you measured timing.

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
   - a built-in MCP server, so an AI agent can drive the machine: screen,
     keyboard, mouse, serial, floppy and a 68040 debugger
     (see [doc/MCP.md](doc/MCP.md))

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
