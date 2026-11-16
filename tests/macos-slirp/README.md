# Mac SLIRP networking acceptance

The Mac package includes libslirp and its recursive runtime dependencies.
Choose SLIRP in ARAnyM's Network settings, or use the bundled
`slirp.config.example` ETH0 settings. For the guest MiNT NatFeat Ethernet driver:

- Atari IP: `10.0.2.15`, netmask `255.255.255.0`
- Gateway: `10.0.2.2`
- DNS: `10.0.2.3`

SLIRP requires no TAP device, privileged helper, or host network configuration.
Existing configurations still select their explicit networking type.

Run native acceptance with:

```
python3 tests/macos-slirp/run.py --app PATH/ARAnyM-Unofficial.app --output RESULTS
```

This relocates the signed app, boots an owned standalone Atari program through
EmuTOS, and uses the real NatFeat Ethernet v5 packet/ack interface. It verifies:

- DHCP offer/ACK, configured IP, router and DNS options.
- ARP gateway lookup and UDP NAT against a host loopback echo fixture.
- DNS A-record resolution for example.com through SLIRP's virtual DNS proxy.
- TCP handshake and HTTP request/response through a host loopback fixture.
- Three receive-thread stop/start cycles with real traffic.
- Host exit0 while the SLIRP receiver remains active.

The suite repeats on `10.0.2.0/24` and `10.42.0.0/24`. The host independently
records actual UDP payloads and HTTP paths; guest PASS text alone is insufficient.
It uses no personal image, installed guest applications, or replacement desktop.
It does not claim full FreeMiNT/HighWire application acceptance. The owned guest
polls/acks the interface while masking only Ethernet level3; other timer IRQs
remain enabled. Native Mac acceptance requires Apple Silicon and the bundled app.

`guest.hex` is a stripped TOS PRG compiled from `entry.S` and `guest.c` without
a C runtime, using m68k-atari-mint-gcc (MiNT 20230719) 4.6.4. Verify its exact
source correspondence with:

```
python3 tests/macos-slirp/build-guest.py /tmp/NETTEST.PRG --verify-compiler
```

CI uses the checked-in executable hex and needs no Atari cross compiler. The
host runner patches two unique data words with its ephemeral fixture ports.
The same guest runs against the Linux package using
`--linux-emulator BINARY --firmware IMAGE`. That mode requires Linux and records
Linux acceptance separately from Mac acceptance. `--reference-emulator` remains
available for explicitly marked development comparisons; those results are
never package acceptance. Linux now configures the production SLIRP backend
normally with `--enable-slirp`, without local build overrides.

Guest JIT package qualification adds `--require-jit`. It enables JIT in the owned
configuration and requires positive executed guest JIT counts in both network
profiles. Use `--architecture arm32` for the Pi armhf compiler. Cross-emulated
ARM32 checks explicitly require `--qemu`, `--qemu-sysroot` and
`--qemu-library-path`, and record `cross_emulated=true`, `native_linux=false`.
