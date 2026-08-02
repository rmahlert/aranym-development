CROSS   = m68k-atari-mint-
CC      = $(CROSS)gcc
CFLAGS  = -Wall -m68020-60 -O2 -fomit-frame-pointer -s
LIBS    = -lgem

netsetup.prg:	netsetup.c
	$(CC) $(CFLAGS) netsetup.c -o netsetup.prg $(LIBS)

clean:
	rm -f netsetup.o netsetup.prg
