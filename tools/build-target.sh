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
		--exclude=lan-share -cf - . | tar -C "$tree" -xf -
	echo "$tree"
}

build_linux()
{
	build_dir=$(prepare_tree linux)
	cd "$build_dir"
	make distclean >/dev/null 2>&1 || true
	./configure --disable-opengl --enable-jit-compiler
	make -j"$jobs"
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
            --disable-nfjpeg \
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
