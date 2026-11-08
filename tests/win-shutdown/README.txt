Windows SLIRP receiver shutdown regression

Run on a disposable ARAnyM profile with Type=slirp, EmuTOS, no boot disk, and
Bootstrap pointing at one guest PRG. No MiNT image or font changes are needed.
Use the same runtime DLLs for all emulator builds and test JIT on and off.
The ordinary guest programs use the public ETHERNET and NF_SHUTDOWN NatFeats.

Build with m68k-atari-mint-gcc -m68040 -O2 -Wall -Wextra -Werror guest.c
and these definitions:
  NOETH.PRG:    -DSTART_NETWORK=0
  NETQUIT.PRG:  defaults
  NETSTOP.PRG:  -DEXPLICIT_STOP=1
  NETLOOP.PRG:  -DEXPLICIT_STOP=1 -DREPEAT_STOPS=10

NOETH requests shutdown without starting the receiver. NETQUIT starts an idle
receiver, waits 200ms, and requests NF_SHUTDOWN0. NETSTOP explicitly stops the
receiver first. NETLOOP repeats that start/stop cycle ten times before exit.
Each emits progress to NF_STDERR. Success requires the emulator actually to
exit0; printing a request alone or merely leaving the guest is insufficient.
NETSTOP must print receiver-stopped after the stop call returns.

Original shipped Windows executable and same-source unmodified rebuild:
  NOETH exits0 with JIT on/off.
  NETQUIT hangs after requesting NF_SHUTDOWN (12-second test timeout).
  NETSTOP hangs inside XIF_STOP (no receiver-stopped message).

Winsock select is not a pthread cancellation point. The SLIRP recv method
loops without returning when idle, so the outer receive thread never reaches
its interrupt-ack condition wait, another cancellation point. stopThread
requests deferred cancellation and then joins that thread, waiting forever.

The repair puts pthread_testcancel at the beginning of the SLIRP recv loop,
before taking slirp_lock. The receiver can stop without leaving that lock held.
It preserves the existing thread lifecycle and packet/NAT protocol behavior.
A matched rebuild should exit0 in every arm; NETLOOP additionally checks reuse.

These tests establish a host-thread shutdown defect independently of the old
MiNT shutdown menu mode. The owner's full Windows guest still needs a matched
same-action test before claiming that all reported shutdown behavior is fixed.
No debugger, injected DLL, forced production close, or kernel change is used.

Full MiNT integration regression:
Build mint-poweroff-guest.c with the same compiler/options and -lgem. On a
restored DISPOSABLE EasyMiNT snapshot, hold the fVDI core/driver/config and
NVDI/XBoot selections fixed, and replace only that snapshot's desktop shell
application with this test. It registers as an AES client, ensures ETHERNET
receiver0 is active, and invokes GEMDOS Shutdown(0). It does not call the
host-shutdown NatFeat directly. Never install it as the owner's desktop.

Compare emulator executables, restoring the identical modified snapshot
between arms. Both should show the test marker and kernel close/sync/unmount/
final-sync messages. In the reproduced old build the host remains alive;
with the cancellation repair it exits0. This tests actual MiNT poweroff with
an active receiver, not which option an owner selected inside shutdown.app.
