ARAnyM network setup
====================

This folder is reachable from the Atari side as a drive letter.  The ARAnyM
installer added the mapping to your configuration and told you which letter
it used; you can also see it in the [HOSTFS] section of your ARAnyM config
file, as the entry pointing at this folder.

To configure networking
-----------------------

From the Atari desktop, open that drive and run:

    NETSETUP.PRG

It asks ARAnyM which addresses it was configured with, brings up eth0 with
them, adds the default route, and writes /etc/resolv.conf if there is not
one already.  It then reports what it did.

Nothing is hardcoded: if you change the addresses on the installer's network
page, or in the ARAnyM setup GUI, running NETSETUP.PRG again picks up the
new values.

If it says the interface was not found
--------------------------------------

Copy nfeth.xif from this folder into the MiNT folder on your boot drive and
reboot, then run NETSETUP.PRG again.  MiNT-Net must be installed and running:
SLIRP on the Windows side is a transport, not a TCP/IP stack.

Other files here
----------------

  nfeth.xif       the MiNT-Net driver for the ARAnyM ethernet interface
  nfeth-config    command line tool that reports the configured addresses
  eth0-config.sh  shell script equivalent of NETSETUP.PRG, for scripted
                  setups; needs bash, ifconfig and route in the guest

Testing
-------

Test with TCP, not ping.  SLIRP does not carry ICMP unless the host allows
raw sockets, so a failing ping is expected and does not mean networking is
broken.
