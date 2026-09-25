#!/bin/sh
#
# Copy the transitive DLL dependencies of one or more PE binaries into a
# directory, so the result starts on a clean Windows system.
#
# Usage: collect-mingw-dlls.sh <destdir> <binary> [binary...]
#
# Environment:
#   MINGW_ROOT        where to look for DLLs (searches $MINGW_ROOT/bin,
#                     $MINGW_ROOT/lib, and the binary's own directory)
#   OBJDUMP           cross objdump to read import tables with
#   MINGW_TOOL_PREFIX toolchain prefix used to derive OBJDUMP
#   MINGW_CXX         cross C++ compiler used to locate its runtime DLLs
#
# Dependencies are resolved by reading PE import tables rather than from a
# hardcoded list, so a new library (libslirp, and the glib chain behind it)
# is picked up without editing this script.  An import that cannot be
# resolved is a hard error: shipping a package whose exe will not load is
# worse than failing the build.

set -eu

if test $# -lt 2; then
	echo "usage: $0 <destdir> <binary> [binary...]" >&2
	exit 2
fi

destdir=$1
shift

: "${MINGW_ROOT:=/usr/x86_64-w64-mingw32/sys-root/mingw}"
: "${MINGW_TOOL_PREFIX:=x86_64-w64-mingw32}"
: "${OBJDUMP:=${MINGW_TOOL_PREFIX}-objdump}"
: "${MINGW_CXX:=${MINGW_TOOL_PREFIX}-g++}"

if ! type "$OBJDUMP" > /dev/null 2>&1; then
	echo "$0: $OBJDUMP not found; cannot resolve DLL dependencies" >&2
	exit 1
fi
if ! test -d "$destdir"; then
	echo "$0: $destdir is not a directory" >&2
	exit 1
fi

work=`mktemp -d`
trap 'rm -rf "$work"' 0 1 2 3 15
: > "$work/seen"
: > "$work/todo"
: > "$work/missing"

for bin in "$@"; do
	if test -f "$bin"; then
		echo "$bin" >> "$work/todo"
	fi
done

while test -s "$work/todo"; do
	cur=`head -n 1 "$work/todo"`
	sed -i '1d' "$work/todo"
	curdir=`dirname "$cur"`

	imports=`"$OBJDUMP" -p "$cur" 2>/dev/null |
		sed -n 's/^[[:space:]]*DLL Name:[[:space:]]*//p'` || true

	for dep in $imports; do
		lower=`echo "$dep" | tr '[A-Z]' '[a-z]'`
		# Anything Windows ships itself must not be bundled.
		case $lower in
		advapi32.dll | kernel32.dll | user32.dll | gdi32.dll | \
		shell32.dll | shlwapi.dll | ole32.dll | oleaut32.dll | \
		ws2_32.dll | wsock32.dll | iphlpapi.dll | msvcrt.dll | \
		winmm.dll | imm32.dll | version.dll | comdlg32.dll | \
		comctl32.dll | setupapi.dll | rpcrt4.dll | dwmapi.dll | \
		uxtheme.dll | crypt32.dll | secur32.dll | bcrypt.dll | \
		ntdll.dll | opengl32.dll | glu32.dll | winspool.drv | \
		dnsapi.dll | mswsock.dll | hid.dll | usp10.dll | \
		d3d9.dll | dsound.dll | dxgi.dll | avicap32.dll | \
		api-ms-win-* | ext-ms-win-* )
			continue ;;
		esac

		if grep -qxF "$lower" "$work/seen"; then
			continue
		fi
		echo "$lower" >> "$work/seen"

		found=
		for d in "$curdir" "$MINGW_ROOT/bin" "$MINGW_ROOT/lib"; do
			if test -f "$d/$dep"; then
				found="$d/$dep"
				break
			fi
		done
		if test -z "$found" && type "$MINGW_CXX" > /dev/null 2>&1; then
			compiler_file=`"$MINGW_CXX" -print-file-name="$dep" 2>/dev/null || true`
			if test -f "$compiler_file"; then
				found="$compiler_file"
			fi
		fi
		if test -z "$found"; then
			echo "$dep" >> "$work/missing"
			continue
		fi

		if test "$found" != "$destdir/${found##*/}"; then
			cp -a "$found" "$destdir/"
		fi
		# Walk it too: libslirp pulls in glib, which pulls in
		# pcre2, intl and iconv.
		echo "$found" >> "$work/todo"
	done
done

if test -s "$work/missing"; then
	echo "$0: unresolved DLL dependencies (not found under $MINGW_ROOT):" >&2
	sort -u "$work/missing" | sed -e 's/^/  /' >&2
	echo "$0: the resulting package would not start on a clean Windows system" >&2
	exit 1
fi

sort "$work/seen"
