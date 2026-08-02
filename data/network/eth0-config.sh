#!/bin/bash
#
# Configure the Atari side of ARAnyM networking.
#
# The addresses are not hardcoded here: nfeth-config asks ARAnyM itself what
# it was configured with, so this works whatever the installer's network page
# was set to, and on any host.  Run it once from a MiNT shell:
#
#     ./eth0-config.sh
#
# It needs nfeth.xif installed in the mint folder and MiNT-Net running.

set -e

cd "$(dirname "$0")"

if [ ! -x ./nfeth-config ]; then
	echo "nfeth-config not found next to this script" >&2
	exit 1
fi

atari_ip=$(./nfeth-config --get-atari-ip eth0)
netmask=$(./nfeth-config --get-netmask eth0)
host_ip=$(./nfeth-config --get-host-ip eth0)

if [ -z "$atari_ip" ] || [ -z "$host_ip" ]; then
	echo "ARAnyM did not report an address for eth0." >&2
	echo "Check that ETH0 is enabled in the ARAnyM configuration." >&2
	exit 1
fi

echo "Atari IP : $atari_ip"
echo "Netmask  : $netmask"
echo "Gateway  : $host_ip"

ifconfig eth0 addr "$atari_ip" netmask "$netmask" up
route add default eth0 gw "$host_ip"

# SLIRP puts its DNS forwarder one address above the gateway, matching the
# emulator's own default.  Only write resolv.conf if there is not already
# one, so an existing setup is left alone.
dns="${host_ip%.*}.$(( ${host_ip##*.} + 1 ))"
if [ ! -f /etc/resolv.conf ]; then
	echo "nameserver $dns" > /etc/resolv.conf
	echo "DNS      : $dns (written to /etc/resolv.conf)"
else
	echo "DNS      : /etc/resolv.conf already exists, left unchanged"
	echo "           (SLIRP's resolver is $dns)"
fi

echo
echo "Done.  Test with TCP, not ping:"
echo "    telnet 10.0.2.2 or a web fetch"
echo "SLIRP does not carry ICMP unless the host allows raw sockets, so a"
echo "failing ping does not mean networking is broken."
