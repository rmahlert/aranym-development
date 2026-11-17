# JIT hardware-probe regression

The guest warms a `MOVE.L (A0),D0` function using RAM, then calls it with the
absent SFP004 address `$FFFFFA44`. A guest bus-error handler discards the
format-7 frame and checks its saved instruction PC, every D/A register, the
pre-fault supervisor stack pointer, and SR/CCR. D1 must contain the value
established before the faulting read, rather than the following ADDQ's result.
The second fixture places NOPs around the read. Both fixtures repeat the fault
with `$FFFFFA44` and `$00FFFA44` in one process, and report the effective fault
address and special-status word. The second fault checks that alias handling
does not leak a prior access's high byte through the bus-error longjmp.

This exercises the hardware-probe concern raised in upstream
[issue #116](https://github.com/aranym/aranym/issues/116). It does not contain
GBE, require a personal drive image, or establish that GBE 3.76 is fixed.

Run the interpreter, cache-disabled, and cached-NOP profiles:

```sh
python3 tests/jit-hardware-probe/run.py \
  --emulator /path/to/aranym --firmware data/emutos-aranym.img \
  --output /path/to/results
```

`--native-nopless` additionally requires precise recovery from the warmed
native instruction without NOPs. This remains a known failure on x86-64:
the saved instruction PC is stale and D1 can reflect the following ADDQ.
The default profiles deliberately report that this case is unqualified.

`--strict-frame` additionally requires the original `$FFFFFA44` effective
address and supervisor-data function code in the special-status word. Older
classic signal handlers instead reported the decoder's `$00FFFA44` hardware
alias and omitted the function code. The default profiles record these
fields separately because other host fault paths need independent qualification.

Rebuild the checked fixtures with `build-fixtures.py --output DIRECTORY` and
copy its two `.hex` files and `provenance.json` into `fixtures/`. The compiler
and exact guest/source hashes are recorded with the fixtures.
