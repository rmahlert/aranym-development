#!/bin/sh
#
# Package a cross-built Windows ARAnyM into a zip and an NSIS installer.
#
# tools/dist-cygwin.sh only runs inside a 32-bit Cygwin environment (it
# needs cygpath and ldd.exe, and refuses to run on x86_64).  The x86_64
# Windows packages were previously assembled by hand, which is how the
# libslirp DLLs came to be missing from them.  This script is the
# reproducible path for a Linux cross-build.
#
# Usage: tools/build-windows-dist.sh [build-dir]
#
# Environment:
#   MINGW_ROOT       prefix holding the MinGW DLLs (default /tmp/mingw)
#   EASYARAMINT_DIR  optional EasyARAnyM drives to include as a component
#   OUTPUT_DIR       where to write the packages (default dist/windows)
#   VERSION_SUFFIX   appended to the package name (default -unofficial)

set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
build_dir=${1:-"$root/build/source-windows"}
: "${MINGW_ROOT:=/tmp/mingw}"
: "${OUTPUT_DIR:="$root/dist/windows"}"
: "${VERSION_SUFFIX:=-unofficial}"

versfile="$root/src/include/version.h"
major=$(sed -n -e 's/#define[ \t]*VER_MAJOR[ \t]*\([0-9]*\)/\1/p' "$versfile")
minor=$(sed -n -e 's/#define[ \t]*VER_MINOR[ \t]*\([0-9]*\)/\1/p' "$versfile")
micro=$(sed -n -e 's/#define[ \t]*VER_MICRO[ \t]*\([0-9]*\)/\1/p' "$versfile")
version="$major.$minor.$micro"

exe="$build_dir/aranym.exe"
if ! test -f "$exe"; then
	echo "$0: $exe not found; run tools/build-target.sh windows first" >&2
	exit 1
fi
if ! type makensis > /dev/null 2>&1; then
	echo "$0: makensis not found; install nsis" >&2
	exit 1
fi

name="aranym-${version}${VERSION_SUFFIX}-windows-x86_64"
work=$(mktemp -d)
trap 'rm -rf "$work"' 0 1 2 3 15
stage="$work/$name"
mkdir -p "$stage/aranym/network"

echo "staging $name"
for f in aranym.exe aranym-jit.exe aranym-mmu.exe; do
	test -f "$build_dir/$f" && cp -a "$build_dir/$f" "$stage/"
done

echo "resolving DLL dependencies"
MINGW_ROOT="$MINGW_ROOT" "$root/tools/collect-mingw-dlls.sh" "$stage" "$stage"/*.exe |
	sed -e 's/^/  /'

# Documentation, with the line endings Windows expects.
cp "$root/COPYING" "$stage/COPYING.txt"
for f in "$root"/doc/*.txt "$root"/doc/config; do
	test -f "$f" || continue
	base=$(basename "$f")
	case $base in
	*.txt) ;;
	*) base="$base.txt" ;;
	esac
	sed -e 's/$/\r/' "$f" > "$stage/$base"
done

# Data the installer references directly.
for f in config logo.bmp wm_icon.bmp installer_welcome.bmp emutos-aranym.img; do
	test -f "$root/data/$f" && cp -a "$root/data/$f" "$stage/aranym/$f"
done
cp -a "$root/data/network/nfeth.xif" "$stage/aranym/network/nfeth.xif"

easy_define=
if test -n "${EASYARAMINT_DIR:-}" && test -f "$EASYARAMINT_DIR/config"; then
	echo "including EasyARAnyM drives from $EASYARAMINT_DIR"
	mkdir -p "$stage/aranym/atari"
	for f in config emutos-aranym.img EasyMint host_fs nfeth.xif; do
		test -e "$EASYARAMINT_DIR/$f" &&
			cp -a "$EASYARAMINT_DIR/$f" "$stage/aranym/atari/"
	done
	easy_define=-DEASY_DRIVES
fi

# The installer's file list, generated before the build inputs are copied
# in so they do not end up installed on the target machine.
(
	cd "$stage"
	find . -type d | sed -e 's|^./\(.*\)$|${CreateDirectory} "$INSTDIR\\\1"|' -e '/^\.$/d' | tr '/' '\\' > aranym.files
	find . -type f ! -name aranym.files | sed -e 's|^./\(.*\)$|${File} "\1"|' | tr '/' '\\' >> aranym.files
)

mkdir -p "$OUTPUT_DIR"

echo "creating zip"
# zip adds to an existing archive rather than replacing it, which would
# silently merge a previous release into this one.
rm -f "$OUTPUT_DIR/$name.zip"
( cd "$work" && zip -qr "$OUTPUT_DIR/$name.zip" "$name" -x "$name/aranym.files" )

echo "creating installer"
cp "$root/tools/aranym.nsi" "$stage/aranym.nsi"
for f in "$root"/tools/*.nsh; do
	test -f "$f" && cp "$f" "$stage/"
done
( cd "$stage" && makensis -V2 -NOCD \
	-DVER_MAJOR="$major" -DVER_MINOR="$minor" -DVER_MICRO="$micro" \
	$easy_define \
	-DOUTFILE="$OUTPUT_DIR/$name-setup.exe" \
	aranym.nsi )

echo
echo "$OUTPUT_DIR/$name.zip"
echo "$OUTPUT_DIR/$name-setup.exe"
