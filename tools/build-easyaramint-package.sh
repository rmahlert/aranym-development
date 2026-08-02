#!/bin/sh

set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
source_dir=${EASYARAMINT_DIR:-"$root/lan-share"}
output_dir=${1:-"$root/dist/easyaramint"}
work=${TMPDIR:-/tmp}/easyaramint-build
exe=${ARANYM_EXE:-"$root/build/source-windows/aranym.exe"}

part1="$source_dir/EAM2.0.7z.001"
part2="$source_dir/EAM2.0.7z.002"
part3="$source_dir/EAM2.0.7z.003"

for file in "$part1" "$part2" "$part3"; do
	if [ ! -f "$file" ]; then
		echo "Missing EasyARAnyM archive part: $file" >&2
		exit 1
	fi
done

extractor=$(command -v 7z || command -v 7zz || command -v 7za || true)
if [ -z "$extractor" ]; then
	echo "Install p7zip-full or 7zip before running this script." >&2
	exit 1
fi

rm -rf "$work" "$output_dir"
mkdir -p "$work/extracted" "$output_dir"
"$extractor" x "$part1" "-o$work/extracted" >/dev/null

source_root=$(find "$work/extracted" -type f -path '*/Config_Win/config' -print -quit | sed 's|/Config_Win/config$||')
if [ -z "$source_root" ]; then
	echo "EasyARAnyM Windows layout was not found in the archive." >&2
	exit 1
fi

mkdir -p "$output_dir/host_fs"
cp -a "$source_root/host_fs"/. "$output_dir/host_fs/"
cp -a "$source_root/EasyMint"/. "$output_dir/EasyMint/"
cp "$source_root/Config_Win/config" "$output_dir/config"
cp "$source_root/emutos-aranym.img" "$output_dir/emutos-aranym.img"
mkdir -p "$output_dir/host_fs/Applications/floppy"
cp "$root/data/network/nfeth.xif" "$output_dir/nfeth.xif"
cp "$root/data/network/nfeth.xif" "$output_dir/host_fs/nfeth.xif"

# The Atari-side setup tools go on host_fs so the guest can reach them as
# drive H: without modifying the disk image -- which also means they are
# available to someone using their own MiNT install.
mkdir -p "$output_dir/host_fs/Network"
for f in nfeth.xif nfeth-config eth0-config.sh; do
	cp "$root/data/network/$f" "$output_dir/host_fs/Network/$f"
done

sed -i 's/\r$//' "$output_dir/config"
sed -i \
	-e '/^\[ETH0\]/,/^$/ s|^Type = .*|Type = slirp|' \
	-e '/^\[ETH0\]/,/^$/ s|^Tunnel = .*|Tunnel = |' \
	-e '/^\[ETH0\]/,/^$/ s|^HostIP = .*|HostIP = 10.0.2.2|' \
	-e '/^\[ETH0\]/,/^$/ s|^AtariIP = .*|AtariIP = 10.0.2.15|' \
	-e '/^\[ETH0\]/,/^$/ s|^Netmask = .*|Netmask = 255.255.255.0|' \
	-e '/^\[ETH0\]/,/^$/ s|^MAC = .*|MAC = 52:54:00:12:34:56|' \
	"$output_dir/config"

if [ -f "$exe" ]; then
	cp "$exe" "$output_dir/aranym.exe"
fi

# Resolve the DLLs from the exe's own import tables.  A hardcoded list used
# to be copied here, which silently omitted libslirp-0.dll and the glib
# chain behind it -- the resulting bundle could not start at all.
if [ -f "$output_dir/aranym.exe" ]; then
	MINGW_ROOT=${MINGW_ROOT:-/tmp/mingw} \
		"$root/tools/collect-mingw-dlls.sh" "$output_dir" "$output_dir/aranym.exe"
fi

cat > "$output_dir/start-aranym.bat" <<'EOF'
@echo off
cd /d "%~dp0"
aranym.exe -c config
EOF

cat > "$output_dir/README-Network.txt" <<'EOF'
EasyARAnyM network package
==========================

Start with start-aranym.bat, or aranym.exe -c config from this directory.

The Windows side is configured already: SLIRP user-mode NAT, Atari 10.0.2.15,
gateway 10.0.2.2, DNS 10.0.2.3. No TAP adapter, bridge, or administrator
rights are needed.

Atari side (one time)
---------------------

The MiNT image still carries its old static network settings, so the guest
has to be told the new ones once.  From a MiNT shell:

    cd h:/Network
    ./eth0-config.sh

That configures eth0, adds the default route, and writes /etc/resolv.conf.
It does not hardcode any addresses -- nfeth-config asks ARAnyM what it was
configured with, so this stays correct if you change the addresses on the
installer's network page.

If eth0 does not exist at all, copy nfeth.xif from this directory into the
MiNT folder on the boot drive and reboot first.  MiNT-Net must be running:
SLIRP on the host is a transport, not a TCP/IP stack.

Testing
-------

Test with TCP, not ping.  SLIRP does not carry ICMP unless the host permits
raw sockets, so a failing ping is expected and does not mean networking is
broken.
EOF

(cd "$output_dir/.." && zip -qr "$(basename "$output_dir").zip" "$(basename "$output_dir")")
echo "$output_dir"
