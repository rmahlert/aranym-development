Windows TAP handler regression checks
=====================================

Run from the source root:

    tests/win-tap/run.sh

Requirements: g++, pthreads, and both x86_64-w64-mingw32-g++ and
i686-w64-mingw32-g++. The script builds in a fresh /tmp directory and prints
its location. It does not install a driver or change Windows networking.

The native test includes the actual ethernet_cygwin.cpp and its real class
header. The shim replaces the Windows API and the unrelated emulator headers;
it does not copy or reimplement adapter selection or completion logic.

Coverage includes rejecting physical/unknown ComponentIds, accepting tap0801
and tap0901, nonterminated and malformed REG_SZ data, case-insensitive GUIDs,
explicit names/GUIDs and ambiguous-name rejection, missing/invalid/stale managed
GUIDs, the 64-bit registry view, explicit custom tunnel precedence, absent setup
fallback, version responses, failed event/IOCTL/media setup, media teardown,
manual-reset events, synchronous and overlapped completion, short/failed writes,
wait failure, receive cancellation and restart with the same open handle. The
last scenario models ETHERNETDriver::reset(): pthread_cancel plus join, retaining
the TAP handle. Both CancelIoEx and same-thread CancelIo fallback are exercised.
The fake API checks that requests are drained before handle/event release, and
that no opened registry/device/event handle remains.

The native executable uses AddressSanitizer and UndefinedBehaviorSanitizer.
Some sandbox runners use ptrace, under which LeakSanitizer cannot run. There:

    ASAN_OPTIONS=detect_leaks=0 tests/win-tap/run.sh

This keeps ASan/UBSan enabled; explicit API resource accounting still runs.

The script also compiles the production handler for 32-bit and 64-bit Windows
with real MinGW Windows headers and -Wall -Wextra -Werror. Only unrelated
emulator headers are shimmed in these translation-unit checks. This is not a
full Cygwin application build or a live Windows driver test.

To compare the focused baseline checks, preserve this test directory and build
it with the previous ethernet_cygwin.cpp/.h at the same relative paths. Run the
native binary with TAP_TEST_CONTROL=1. That control checks non-TAP rejection,
GUID case, media state, actual pending-write completion and resource release.
The previous implementation failed 11 checks; the corrected implementation
passes the same control and the full suite. The control clears a mock pending
request left by the previous implementation only after reporting that failure,
so the known bug does not obscure subsequent resource assertions.

Lifetime and adapter policy
--------------------------

As in the existing caller contract, open/close/destruction occur after stopping
and joining the receive thread, with no concurrent send. close() is idempotent.
The receive cleanup cancels and drains its own outstanding request before thread
exit, which is also required on reset before reusing OVERLAPPED and packet
storage. Windows cancellation alone does not imply request completion.

TAP_IOCTL_GET_VERSION must return three ULONGs identifying legacy 8.1+ or modern
9.x. Unknown major versions fail before media-up. The handle remains exclusive;
each concurrently running emulator therefore requires a distinct TAP adapter.

For an enabled TAP mode, an empty/default tap0 tunnel uses the exact managed
modern tap0901 GUID stored in the 64-bit registry view at:

    HKLM\SOFTWARE\ARAnyM\LAN\TapGuid

An explicit custom tunnel name or GUID takes precedence. Without a configured
managed GUID, legacy tap0 lookup is preserved and an empty tunnel stays disabled.
A malformed/stale configured GUID produces an error, rather than selecting a
similarly named adapter. The standalone Atari TAP Setup supplies that registry
entry; it does not itself prove Ethernet or Wi-Fi LAN forwarding.

No SLIRP, guest addressing, physical bridge, or installer policy is changed by
this patch. Actual Windows TAP 9.27.0 open/traffic/reset/exit, Cygwin cancellation,
legacy-driver compatibility, and Wi-Fi LAN forwarding still require live tests.
