#!/bin/sh

set -eu

target=${1:-linux}
jobs=${JOBS:-$(getconf _NPROCESSORS_ONLN 2>/dev/null || echo 2)}
root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)

prepare_tree()
{
	tree="$root/build/source-$1"
	rm -rf "$tree"
	mkdir -p "$tree"
	tar -C "$root" --exclude=.git --exclude=build --exclude=dist \
		--exclude=lan-share --exclude=.deps --exclude='*.o' \
		--exclude='*.a' --exclude='*.Po' --exclude='*.Plo' \
		--exclude='*.exe' --exclude=__pycache__ --exclude='*.pyc' -cf - . | tar -C "$tree" -xf -
	echo "$tree"
}

build_linux()
{
	linux_portmidi_prefix=${PORTMIDI_LINUX_PREFIX:-${PORTMIDI_PREFIX:-}}
	linux_portmidi_prefix=$(verify_portmidi linux "$linux_portmidi_prefix")
	build_dir=$(prepare_tree linux)
	cd "$build_dir"
	make distclean >/dev/null 2>&1 || true
	./configure --disable-opengl --enable-jit-compiler --enable-portmidi \
		PORTMIDI_CFLAGS="-I$linux_portmidi_prefix/include" \
		PORTMIDI_LIBS="-L$linux_portmidi_prefix/lib -lportmidi"
	make -j"$jobs"
}

verify_portmidi()
{
	if [ -z "$2" ]; then
		echo "Set PORTMIDI_PREFIX to the pinned PortMidi dependency installation." >&2
		echo "For both targets, use PORTMIDI_LINUX_PREFIX and PORTMIDI_WINDOWS_PREFIX." >&2
		exit 1
	fi
	python3 -B - "$root/tools" "$2" "$1" <<'PYVERIFY'
import sys
sys.path.insert(0, sys.argv[1])
from portmidi_dist import PortMidiDistribution
dependency = PortMidiDistribution(sys.argv[2])
dependency.library(sys.argv[3])
print(dependency.prefix)
PYVERIFY
}

build_windows()
{
	windows_jit=${WINDOWS_JIT:-0}
	: "${CC:=x86_64-w64-mingw32-gcc}"
	: "${CXX:=x86_64-w64-mingw32-g++}"
	: "${AR:=x86_64-w64-mingw32-ar}"
    : "${RANLIB:=x86_64-w64-mingw32-ranlib}"
    : "${CC_FOR_BUILD:=gcc}"
    : "${CXX_FOR_BUILD:=g++}"
    : "${CXXCPP_FOR_BUILD:=$CXX_FOR_BUILD -E}"

	for tool in "$CC" "$CXX" "$AR" "$RANLIB"; do
		command -v "$tool" >/dev/null 2>&1 || {
			echo "Missing Windows tool: $tool" >&2
			exit 1
		}
	done

	if [ -z "${SDL2_CFLAGS:-}" ] || [ -z "${SDL2_LIBS:-}" ]; then
		echo "Set SDL2_CFLAGS and SDL2_LIBS for the MinGW SDL2 installation." >&2
		echo "Do not use the native Linux SDL2 pkg-config result." >&2
		exit 1
	fi

	windows_portmidi_prefix=${PORTMIDI_WINDOWS_PREFIX:-${PORTMIDI_PREFIX:-}}
	windows_portmidi_prefix=$(verify_portmidi windows "$windows_portmidi_prefix")

	build_dir=$(prepare_tree windows)
	cd "$build_dir"
	make distclean >/dev/null 2>&1 || true
	configure_args="--host=x86_64-w64-mingw32 --disable-opengl --enable-addressing=normal"
	if [ "$windows_jit" = 1 ]; then
		configure_args="$configure_args --enable-jit-compiler"
	else
		configure_args="$configure_args --disable-jit-compiler"
	fi
	CC="$CC" CXX="$CXX" AR="$AR" RANLIB="$RANLIB" \
        CC_FOR_BUILD="$CC_FOR_BUILD" CXX_FOR_BUILD="$CXX_FOR_BUILD" \
        CXXCPP_FOR_BUILD="$CXXCPP_FOR_BUILD" \
        ./configure $configure_args \
            --enable-slirp \
            --enable-portmidi \
            --enable-nfjpeg \
            PORTMIDI_CFLAGS="-I$windows_portmidi_prefix/include" \
            PORTMIDI_LIBS="-L$windows_portmidi_prefix/lib -lportmidi" \
            SDL2_CFLAGS="$SDL2_CFLAGS" SDL2_LIBS="$SDL2_LIBS"
	make -j"$jobs"
}

case "$target" in
	linux)
		build_linux
		;;
	windows)
		build_windows
		;;
	all)
		build_linux
		build_windows
		;;
	*)
		echo "Usage: $0 {linux|windows|all}" >&2
		exit 2
		;;
esac
