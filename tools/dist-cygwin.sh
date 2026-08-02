#!/bin/sh

datefile=version_date.h
versfile=src/include/version.h
log=build-cygwin.log

if ! test -f $datefile; then
	echo "$0: $datefile missing" >&2
	exit 1
fi
if ! test -f $versfile; then
	echo "$0: $versfile missing" >&2
	exit 1
fi

#
# configure args for all configurations
#
CONFIGURE_ARGS="--prefix=/usr --enable-usbhost"

major=`sed -n -e 's/#define[ \t]*VER_MAJOR[ \t]*\([0-9]*\)/\1/p' $versfile`
minor=`sed -n -e 's/#define[ \t]*VER_MINOR[ \t]*\([0-9]*\)/\1/p' $versfile`
micro=`sed -n -e 's/#define[ \t]*VER_MICRO[ \t]*\([0-9]*\)/\1/p' $versfile`

PROJECT_LOWER=aranym
VERSION=$major.$minor.$micro
archive_tag=-cygwin-i386
ATAG=${VERSION}${archive_tag}

#
# recent version of cygwin have the mingw libraries installed
# in a sys-rooted cross-compiler environment
#
if test -d /usr/i686-w64-mingw32/sys-root/mingw; then
	MINGW_ROOT=/usr/i686-w64-mingw32/sys-root/mingw
	CONFIGURE_ARGS="$CONFIGURE_ARGS --with-sdl-prefix=$MINGW_ROOT --with-sdl2-prefix=$MINGW_ROOT"
fi
if test `uname -m` = x86_64; then
	CONFIGURE_ARGS="$CONFIGURE_ARGS --host=i686-pc-cygwin"
	# that currently does not work:
	# we build a 32-bit binary, but the installation process
	# below would copy cygwin dlls from the 64-bit environment
	echo "building 32-bit distribution in 64-bit environment currently not supported" >&2
	echo "install cygwin using setup-x86.exe and try again" >&2
	exit 1
fi

DATE=`sed -n -e 's/#define[ \t]*VERSION_DATE[ \t]*"\([^"]*\)"/\1/p' $datefile`

echo VERSION=$VERSION
echo DATE=$DATE
rm -f $log

trap 'rm -f $log' 1 2 3 15

function build() {
	local sdloption

	case $SDL in 
	1) sdloption=--disable-sdl2 ;;
	2) sdloption=--enable-sdl2 ;;
	*) exit 1 ;;
	esac
	
	echo "configuring aranym-mmu..."
	./configure $CONFIGURE_ARGS $sdloption --enable-fullmmu --enable-lilo >> $log 2>&1 || {
		echo "configuring aranym-mmu failed; see $log for details" >&2
		exit 1
	}
	echo "building aranym-mmu..."
	make clean
	make >> $log 2>&1 || {
		echo "building aranym-mmu failed; see $log for details" >&2
		exit 1
	}
	strip aranym.exe || exit 1
	mv aranym.exe aranym-mmu.exe
	
	
	echo "configuring aranym-jit..."
	./configure $CONFIGURE_ARGS $sdloption --enable-jit-compiler --enable-jit-fpu >> $log 2>&1 || {
		echo "configuring aranym-mmu failed; see $log for details" >&2
		exit 1
	}
	echo "building aranym-jit..."
	make clean
	make >> $log 2>&1 || {
		echo "building aranym-jit failed; see $log for details" >&2
		exit 1
	}
	strip aranym.exe || exit 1
	mv aranym.exe aranym-jit.exe
	
	
	echo "configuring aranym..."
	./configure $CONFIGURE_ARGS $sdloption >> $log 2>&1 || {
		echo "configuring aranym failed; see $log for details" >&2
		exit 1
	}
	echo "building aranym..."
	make clean
	make >> $log 2>&1 || {
		echo "building aranym failed; see $log for details" >&2
		exit 1
	}
	strip aranym.exe || exit 1
	mv aranym.exe aranym-dflt.exe
	
	make clean
	
	mv aranym-dflt.exe aranym.exe
}


function mkdist() {
	local sdlname
	local sdldef
	
	case $SDL in 
	1) sdlname=-sdl1
	   ;;
	2) sdlname=-sdl2
	   ;;
	*) exit 1 ;;
	esac
	sdldef=-DSDL=${sdlname}
	
	tmpdir="${TMPDIR:-/tmp}"
	distdir="$tmpdir/${PROJECT_LOWER}-$VERSION${sdlname}"
	
	mkdir -p "$distdir/doc" "$distdir/aranym" || exit 1
	
	cp -a COPYING "$distdir/COPYING.txt" || exit 1
	sed -e "s|@VERSION@|$VERSION|g" -e "s|@DATE@|$DATE|g" README-cygwin.in > "$distdir/README.txt" || exit 1
	for f in AUTHORS BUGS README ChangeLog FAQ INSTALL NEWS TODO; do
		cp -a $f "$distdir/doc/$f.txt" || exit 1
	done
	for f in config; do
		cp -a doc/$f "$distdir/doc/$f.txt" || exit 1
	done
	for f in logo.bmp wm_icon.bmp installer_welcome.bmp config emutos-aranym.img; do
		cp -a data/$f "$distdir/aranym/$f" || exit 1
	done
	if test -n "${EASYARAMINT_DIR:-}" && test -f "$EASYARAMINT_DIR/config"; then
		mkdir -p "$distdir/aranym/atari"
		for f in config emutos-aranym.img EasyMint host_fs nfeth.xif; do
			if test -e "$EASYARAMINT_DIR/$f"; then
				cp -a "$EASYARAMINT_DIR/$f" "$distdir/aranym/atari/" || exit 1
			fi
		done
		echo "including optional EasyARAnyM Atari drives from $EASYARAMINT_DIR"
	fi
	
	(cd "$distdir"
	  for f in doc/* *.txt; do
	     cat $f | tr -d '\r' | sed -e 's/$/\r/' > $$.tmp
	     touch -r $f $$.tmp
	     mv $$.tmp $f
	  done
	)
	
	MINGW_DLLS="SDL.dll SDL_image.dll SDL2.dll SDL2_image.dll libgcc_s_sjlj-1.dll libwinpthread-1.dll iconv.dll"
	rm -f $MINGW_DLLS
	for dll in $MINGW_DLLS; do cp -a "$MINGW_ROOT/bin/$dll" .; done

	for f in aranym.exe aranym-jit.exe aranym-mmu.exe; do
		cp -a "$f" "$distdir" || exit 1
	done

	# Collect the transitive DLL dependencies of the built binaries.
	#
	# This used to shell out to ./ldd.exe, which exists only inside a
	# Cygwin/MSYS environment.  Cross-building from Linux it is absent,
	# the walk yielded nothing without erroring, and only the hardcoded
	# MINGW_DLLS above were shipped.  That is how libslirp and its glib
	# chain went missing from the 1.1.1 packages, leaving an aranym.exe
	# that Windows refuses to load at all.  Read the PE import tables
	# directly so new dependencies are picked up without editing a list.
	OBJDUMP="${OBJDUMP:-${MINGW_TOOL_PREFIX:-x86_64-w64-mingw32}-objdump}"
	if ! type "$OBJDUMP" > /dev/null 2>&1; then
		if type ldd.exe > /dev/null 2>&1; then
			OBJDUMP=
		else
			echo "error: no $OBJDUMP to resolve DLL dependencies with" >&2
			exit 1
		fi
	fi

	depdir=`mktemp -d` || exit 1
	: > "$depdir/seen"
	: > "$depdir/todo"
	: > "$depdir/missing"
	for f in aranym.exe aranym-jit.exe aranym-mmu.exe; do
		test -f "$f" && echo "$f" >> "$depdir/todo"
	done

	while test -s "$depdir/todo"; do
		cur=`head -n 1 "$depdir/todo"`
		sed -i '1d' "$depdir/todo"
		if test -n "$OBJDUMP"; then
			imports=`"$OBJDUMP" -p "$cur" 2>/dev/null | sed -n 's/^[[:space:]]*DLL Name:[[:space:]]*//p'`
		else
			imports=`./ldd.exe --path "$cur" 2>/dev/null | sed -e 's|.*[/\\]||'`
		fi
		for dep in $imports; do
			lower="`echo \"$dep\" | tr '[A-Z]' '[a-z]'`"
			# Anything shipped by Windows itself must not be bundled.
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
			grep -qxF "$lower" "$depdir/seen" && continue
			echo "$lower" >> "$depdir/seen"

			found=
			for d in "$MINGW_ROOT/bin" "$MINGW_ROOT/lib" .; do
				if test -f "$d/$dep"; then
					found="$d/$dep"
					break
				fi
			done
			if test -z "$found"; then
				echo "$dep" >> "$depdir/missing"
				continue
			fi
			cp -a "$found" "$distdir" || exit 1
			# Walk it too: libslirp pulls in glib, which pulls in
			# intl/iconv/pcre2/ffi.
			echo "$found" >> "$depdir/todo"
		done
	done

	if test -s "$depdir/missing"; then
		echo "error: unresolved DLL dependencies (not found under $MINGW_ROOT):" >&2
		sort -u "$depdir/missing" | sed -e 's/^/  /' >&2
		echo "the resulting package would not start on a clean Windows system" >&2
		rm -rf "$depdir"
		exit 1
	fi
	rm -rf "$depdir"
	rm -f $MINGW_DLLS
	
	ARCHIVE="${PROJECT_LOWER}-${ATAG}${sdlname}.zip"
	SETUP_EXE="${PROJECT_LOWER}-${ATAG}${sdlname}-setup.exe"
	( cd "$tmpdir"
	  zip -r "$ARCHIVE" ${PROJECT_LOWER}-$VERSION${sdlname}
	) || exit 1
	
	echo "$tmpdir/$ARCHIVE ready for release"
	
	# NSIS may be a native Linux makensis (cross-packaging) or the
	# Windows build under Program Files (Cygwin/MSYS).
	nsis=
	if type makensis > /dev/null 2>&1; then
		nsis=`command -v makensis`
	elif type cygpath > /dev/null 2>&1; then
		nsis=`cygpath "$PROGRAMFILES"`/NSIS/makensis.exe
		test -x "$nsis" || nsis=
	fi
	if test -z "$nsis"; then
	    echo "warning: no makensis found, skipping the installer" >&2
	fi
	if test -n "$nsis"; then
	    cwd=`pwd`
	    cd "$distdir"
	    echo "creating Windows installer"
	    find . -type d | sed -e 's|^./\(.*\)$|${CreateDirectory} "$INSTDIR\\\1"|' -e '/^\.$/d' | tr '/' '\\' > ../aranym.files
	    find . -type f | sed -e 's|^./\(.*\)$|${File} "\1"|' | tr '/' '\\' >> ../aranym.files
	    easy_define=
	    if test -d aranym/atari && test -f aranym/atari/config; then
		easy_define=-DEASY_DRIVES
	    fi
	    if type cygpath > /dev/null 2>&1; then
		nsi=`cygpath -m $cwd/tools/aranym.nsi`
	    else
		nsi="$cwd/tools/aranym.nsi"
	    fi
	    "$nsis" -V2 -NOCD -DVER_MAJOR=$major -DVER_MINOR=$minor -DVER_MICRO=$micro $sdldef $easy_define -DOUTFILE=../${SETUP_EXE} "$nsi" || exit 1
	    rm -f ../aranym.files
	    cd $cwd
	    echo "$tmpdir/${SETUP_EXE} ready for release"
	fi
	
}


# ldd.exe is only usable under Cygwin/MSYS.  Cross-building from Linux the
# dependency walk uses objdump instead, so a failure here is not fatal.
gcc -O2 -mwin32 tools/ldd.c -o ldd.exe 2>/dev/null || \
	echo "note: no ldd.exe (not a Cygwin host); using objdump for DLL deps" >&2

SDL=1
build
mkdist

SDL=2
build
mkdist


rm -f $log ldd.exe
