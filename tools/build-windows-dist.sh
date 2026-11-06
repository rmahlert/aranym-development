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
#   STRIP            cross strip tool (derived from MINGW_TOOL_PREFIX)
#   ATARI_TAP_SETUP_ZIP optional shared TAP setup kit, with matching .sha256

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
config_header="$build_dir/config.h"
if ! test -f "$config_header" ||
	! grep -Eq '^[[:space:]]*#define[[:space:]]+HAVE_SLIRP[[:space:]]+1([[:space:]]|$)' "$config_header"; then
	echo "$0: $exe was not configured with SLIRP support" >&2
	echo "$0: rebuild it with tools/build-target.sh windows before packaging" >&2
	exit 1
fi
if ! grep -Eq '^[[:space:]]*#define[[:space:]]+ETHERNET_SUPPORT[[:space:]]+1([[:space:]]|$)' "$config_header"; then
	echo "$0: $exe has SLIRP but the NatFeat Ethernet device is disabled" >&2
	echo "$0: refusing to package a build that the MiNTNet driver cannot use" >&2
	exit 1
fi
package_objdump=${OBJDUMP:-${MINGW_TOOL_PREFIX:-x86_64-w64-mingw32}-objdump}
if ! type "$package_objdump" > /dev/null 2>&1; then
	echo "$0: $package_objdump not found; cannot verify the Windows executable" >&2
	exit 1
fi
if ! "$package_objdump" -p "$exe" 2>/dev/null |
	sed -n 's/^[[:space:]]*DLL Name:[[:space:]]*//p' |
	grep -Eiq '^libslirp-[0-9]+\.dll$'; then
	echo "$0: $exe does not import the SLIRP runtime" >&2
	echo "$0: refusing to package a Windows build whose SLIRP option cannot work" >&2
	exit 1
fi
package_strip=${STRIP:-${MINGW_TOOL_PREFIX:-x86_64-w64-mingw32}-strip}
if ! type "$package_strip" > /dev/null 2>&1; then
	echo "$0: $package_strip not found; cannot prepare release binaries" >&2
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
if ! MINGW_ROOT="$MINGW_ROOT" "$root/tools/collect-mingw-dlls.sh" \
	"$stage" "$stage"/*.exe > "$work/dll-list"; then
	exit 1
fi
sed -e 's/^/  /' "$work/dll-list"

# Ubuntu's MinGW runtime DLLs and the default ARAnyM build contain debug
# sections.  Apart from bloating the package, sufficiently large unstripped
# PE files can make makensis fail while compressing them.
for f in "$stage"/*.exe "$stage"/*.dll; do
	test -f "$f" || continue
	"$package_strip" --strip-unneeded "$f"
done

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
for f in nfeth.xif nfeth-config netsetup.prg eth0-config.sh README-setup.txt; do
	cp -a "$root/data/network/$f" "$stage/aranym/network/$f"
done

tap_define=
if test -n "${ATARI_TAP_SETUP_ZIP:-}"; then
    python3 "$root/tools/stage-tap-setup.py" "$ATARI_TAP_SETUP_ZIP" "$stage/aranym/network"
    tap_define=-DTAP_SETUP
fi

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
	find . -type f ! -name aranym.files ! -path './aranym/emutos-aranym.img' |
		sed -e 's|^./\(.*\)$|${File} "\1"|' | tr '/' '\\' >> aranym.files
)

mkdir -p "$OUTPUT_DIR"

echo "creating zip"
# zip adds to an existing archive rather than replacing it, which would
# silently merge a previous release into this one.
package_zip="$work/$name.zip"
( cd "$work" && zip -qr "$package_zip" "$name" -x "$name/aranym.files" )

echo "creating installer"
cp "$root/tools/aranym.nsi" "$stage/aranym.nsi"
for f in "$root"/tools/*.nsh; do
	test -f "$f" && cp "$f" "$stage/"
done
( cd "$stage" && makensis -V2 -NOCD \
	-DVER_MAJOR="$major" -DVER_MINOR="$minor" -DVER_MICRO="$micro" \
	$easy_define \
    $tap_define \
	-DOUTFILE="$work/$name-setup.exe" \
	aranym.nsi )

# Publish only after both artifacts have been built successfully.
cp "$package_zip" "$OUTPUT_DIR/$name.zip"
cp "$work/$name-setup.exe" "$OUTPUT_DIR/$name-setup.exe"

echo
echo "$OUTPUT_DIR/$name.zip"
echo "$OUTPUT_DIR/$name-setup.exe"
