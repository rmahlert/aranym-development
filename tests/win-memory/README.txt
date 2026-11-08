Windows fixed-memory regression

Use a disposable profile and no user disk image. The guest fixture calls
check_read_byte() inside the EmuTOS 1.4 ARAnyM ROM dated 2026/08/29:
SHA256 b723d04ae9912b8e5fdcb9bd2dffacb3d57968ab3e543b7ace5e687a04bef63b
The probe address 0x00e00ca0 is specific to this ROM. Verify its hash before
running the test; other ROM versions require a different fixture.

Build with m68k-atari-mint-gcc -m68040 -O2 memory-guest.c -o MEMORY.PRG.
Configure FastRAM=2000, FixedMemoryOffset=9a000000, that EmuTOS ROM, and
Bootstrap=the absolute Windows path to MEMORY.PRG. Disable all disk images.
Run the Windows executable with JIT=Yes and again with JIT=No.

Both runs must log a runtime remap from 9a000000 to 81000000, followed by:
MEMORY TEST PASS ramtop=7e000000 missed=0 guard-accepted=0 writes-restored=Yes
The fixture checks 1000 times that RAM near the formerly wrapped address
and the last real byte are readable, verifies and restores writes there,
and checks that the first invalid byte and the next MiB's last byte fault.
The on-disk config must remain byte-identical after launch/exit.

Control: FastRAM=1024 with FixedMemoryOffset=9a000000 must retain that
mapping and report bootstrap capacity 1073741812 (1024 MiB minus 12).
Do not use this 2000 MiB guest fixture for the 1024 MiB control.

The old binary on Wine cannot reserve a 2000 MiB window at 9a000000,
although Windows can. The old alias was separately demonstrated with
128 MiB at that offset: the ROM probe incorrectly accepted 660fffff in
1000 of 1000 calls, reading the host's read-only page at 000fffff.
With the lower offset 12000000 the same invalid probe was rejected in
all 1000 calls. No writes to the aliased host page were attempted.
