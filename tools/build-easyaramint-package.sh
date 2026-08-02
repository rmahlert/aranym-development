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
cp "$root/data/network/nfeth.xif" "$output_dir/nfeth.xif"

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

runtime_dir=${ARANYM_RUNTIME_DIR:-/tmp/aranym-nsis-root}
for file in SDL2.dll libgcc_s_seh-1.dll libstdc++-6.dll libwinpthread-1.dll; do
	if [ -f "$runtime_dir/$file" ]; then
		cp "$runtime_dir/$file" "$output_dir/$file"
	fi
done

cat > "$output_dir/start-aranym.bat" <<'EOF'
@echo off
cd /d "%~dp0"
aranym.exe -c config
EOF

cat > "$output_dir/README-Network.txt" <<'EOF'
EasyARAnyM network package

This package boots the included EasyARAnyM MiNT environment and includes
nfeth.xif. The ARAnyM host configuration uses SLIRP at 10.0.2.15 with gateway
10.0.2.2. The host_fs directory is the guest host filesystem. Run aranym.exe
with -c config from this directory, or use start-aranym.bat.
EOF

(cd "$output_dir/.." && zip -qr "$(basename "$output_dir").zip" "$(basename "$output_dir")")
echo "$output_dir"
