ACLOCAL_AMFLAGS = -I m4
AUTOMAKE_OPTIONS = foreign

pkgdatadir = @datadir@/aranym
man1dir = $(mandir)/man1

# Unix makefile for ARAnyM
# Inspired in BasiliskII's Makefile

SUBDIRS = src doc

## Rules

.PHONY: depend
SUFFIXES = .c .cpp .s .o .h .m .M

BUILT_SOURCES = \
	version_date.h \
	$(top_srcdir)/src/natfeat/nfosmesa/call-gl.c \
	$(top_srcdir)/src/natfeat/nfosmesa/dispatch-gl.c \
	$(top_srcdir)/src/natfeat/nfosmesa/paramcount-gl.c \
	$(top_srcdir)/atari/nfosmesa/glfuncs.h \
	$(top_srcdir)/atari/nfosmesa/glfuncs-bynum.h \
	$(top_srcdir)/atari/nfosmesa/ldg/osmesa.h \
	$(top_srcdir)/atari/nfosmesa/ldg/tiny_gl.h \
	$(top_srcdir)/atari/nfosmesa/osmesa_load.c \
	$(top_srcdir)/atari/nfosmesa/tinygl_load.c \
	$(top_srcdir)/atari/nfosmesa/link-tinygl.h \
	$(top_srcdir)/atari/nfosmesa/slb/tiny_gl.h \
	$(top_srcdir)/atari/nfosmesa/slb/osmesa.h \
	$(top_srcdir)/atari/nfosmesa/tinygl_loadslb.c \
	$(top_srcdir)/atari/nfosmesa/osmesa_loadslb.c \
	$(empty)

version_date.h:
	@echo generating $@
	@( if test -f "$(top_srcdir)/.isrelease"; then \
		echo '/* generated from version.h */'; \
		echo '#define VERSION_DATE " " RELEASE_DATE " (release)"'; \
	  elif test -d "$(top_srcdir)/.git"; then \
		echo '/* generated from git log */'; \
		cd "$(top_srcdir)" && git log -n 1 --pretty=format:'#define VERSION_DATE " %ad (git:%h)"%n' --date=format:%Y/%m/%d; \
	  else \
		echo '/* generated from local source */'; \
		echo '#define VERSION_DATE " " __DATE__ " (local)"'; \
	  fi ) > $@

built_sources: $(BUILT_SOURCES)

install-data-local:
	$(mkinstalldirs) $(DESTDIR)$(bindir) $(DESTDIR)$(man1dir) $(DESTDIR)$(pkgdatadir) $(DESTDIR)$(pkgdatadir)/atari $(DESTDIR)$(pkgdatadir)/atari/hostfs $(DESTDIR)$(pkgdatadir)/atari/nfcdrom $(DESTDIR)$(pkgdatadir)/atari/nfjpeg $(DESTDIR)$(pkgdatadir)/atari/nfosmesa $(DESTDIR)$(pkgdatadir)/atari/network $(DESTDIR)$(pkgdatadir)/atari/tools $(DESTDIR)$(pkgdatadir)/atari/sound $(DESTDIR)$(docdir)
	$(INSTALL_DATA) $(top_srcdir)/ChangeLog $(DESTDIR)$(docdir)/changelog
	$(INSTALL_DATA) $(top_srcdir)/TODO $(DESTDIR)$(docdir)/TODO
	$(INSTALL_DATA) $(top_srcdir)/AUTHORS $(DESTDIR)$(docdir)/AUTHORS
	$(INSTALL_DATA) $(top_srcdir)/NEWS $(DESTDIR)$(docdir)/NEWS
	$(INSTALL_DATA) $(top_srcdir)/README.md $(DESTDIR)$(docdir)/README
	$(INSTALL_DATA) $(top_srcdir)/FAQ $(DESTDIR)$(docdir)/FAQ
	$(INSTALL_DATA) $(top_srcdir)/COPYING $(DESTDIR)$(docdir)/COPYING
	$(INSTALL_DATA) $(top_srcdir)/BUGS $(DESTDIR)$(docdir)/BUGS
	$(INSTALL_DATA) $(top_srcdir)/INSTALL $(DESTDIR)$(docdir)/INSTALL
	$(INSTALL_DATA) $(top_srcdir)/doc/config $(DESTDIR)$(docdir)/config
	$(INSTALL_DATA) $(top_srcdir)/atari/newdesk.inf $(DESTDIR)$(pkgdatadir)/atari/newdesk.inf
	$(INSTALL_DATA) $(top_srcdir)/atari/mmusetup.cnf $(DESTDIR)$(pkgdatadir)/atari/mmusetup.cnf
	$(INSTALL_DATA) $(top_srcdir)/atari/hostfs/bdconfig.sys $(DESTDIR)$(pkgdatadir)/atari/hostfs/bdconfig.sys
	$(INSTALL_DATA) $(top_srcdir)/atari/hostfs/README $(DESTDIR)$(pkgdatadir)/atari/hostfs/README
	$(INSTALL_DATA) $(top_srcdir)/extras/hostfs/hostfs.dos $(DESTDIR)$(pkgdatadir)/atari/hostfs/hostfs.dos
	$(INSTALL_DATA) $(top_srcdir)/extras/nfcdrom/nfcdrom.bos $(DESTDIR)$(pkgdatadir)/atari/nfcdrom/nfcdrom.bos
	$(INSTALL_DATA) $(top_srcdir)/extras/nfjpeg/nfjpeg.prg $(DESTDIR)$(pkgdatadir)/atari/nfjpeg/nfjpeg.prg
	$(INSTALL_DATA) $(top_srcdir)/atari/nfosmesa/README $(DESTDIR)$(pkgdatadir)/atari/nfosmesa/README
	$(INSTALL_DATA) $(top_srcdir)/atari/network/ethernet/nfeth-config/eth0-config.sh $(DESTDIR)$(pkgdatadir)/atari/network/eth0-config.sh
	$(INSTALL_DATA) $(top_srcdir)/atari/network/ethernet/README $(DESTDIR)$(pkgdatadir)/atari/network/README
	$(INSTALL) $(top_srcdir)/tools/arabridge $(DESTDIR)$(pkgdatadir)/arabridge
	$(INSTALL) $(top_srcdir)/tools/floppy.sh $(DESTDIR)$(pkgdatadir)/floppy.sh
	$(INSTALL_DATA) $(top_srcdir)/atari/tools/README $(DESTDIR)$(pkgdatadir)/atari/tools/README
	$(INSTALL_DATA) $(top_srcdir)/atari/tools/pc101us.kbd.bz2 $(DESTDIR)$(pkgdatadir)/atari/tools/pc101us.kbd.bz2
	$(INSTALL_DATA) $(top_srcdir)/atari/sound/README $(DESTDIR)$(pkgdatadir)/atari/sound/README
	$(INSTALL_DATA) $(top_srcdir)/data/wm_icon.bmp $(DESTDIR)$(pkgdatadir)/wm_icon.bmp
	$(INSTALL_DATA) $(top_srcdir)/data/logo.bmp $(DESTDIR)$(pkgdatadir)/logo.bmp
	for s in 32 48; do \
		$(mkinstalldirs) "${DESTDIR}${datadir}/icons/hicolor/$${s}x$${s}/apps/"; \
		$(INSTALL_DATA) $(top_srcdir)/contrib/icon-$${s}.png "${DESTDIR}${datadir}/icons/hicolor/$${s}x$${s}/apps/aranym.png"; \
		$(INSTALL_DATA) $(top_srcdir)/contrib/icon-$${s}.png "${DESTDIR}${datadir}/icons/hicolor/$${s}x$${s}/apps/aranym-jit.png"; \
		$(INSTALL_DATA) $(top_srcdir)/contrib/icon-$${s}.png "${DESTDIR}${datadir}/icons/hicolor/$${s}x$${s}/apps/aranym-mmu.png"; \
	done
	$(mkinstalldirs) "${DESTDIR}${datadir}/pixmaps"
	rm -f "${DESTDIR}${datadir}/pixmaps/aranym"*.png
	ln -s ../icons/hicolor/32x32/apps/aranym.png "${DESTDIR}${datadir}/pixmaps/aranym.png"
	ln -s ../icons/hicolor/32x32/apps/aranym.png "${DESTDIR}${datadir}/pixmaps/aranym-jit.png"
	ln -s ../icons/hicolor/32x32/apps/aranym.png "${DESTDIR}${datadir}/pixmaps/aranym-mmu.png"
	$(mkinstalldirs) "${DESTDIR}${datadir}/applications"
	for name in aranym aranym-jit aranym-mmu; do \
		$(INSTALL_DATA) "$(top_srcdir)/contrib/$${name}.desktop" "${DESTDIR}${datadir}/applications/$${name}.desktop"; \
	done

uninstall-local:
	rm -f $(DESTDIR)$(docdir)/changelog
	rm -f $(DESTDIR)$(docdir)/TODO
	rm -f $(DESTDIR)$(docdir)/AUTHORS
	rm -f $(DESTDIR)$(docdir)/NEWS
	rm -f $(DESTDIR)$(docdir)/README
	rm -f $(DESTDIR)$(docdir)/FAQ
	rm -f $(DESTDIR)$(docdir)/COPYING
	rm -f $(DESTDIR)$(docdir)/BUGS
	rm -f $(DESTDIR)$(docdir)/INSTALL
	rm -f $(DESTDIR)$(docdir)/config
	rm -f $(DESTDIR)$(pkgdatadir)/atari/newdesk.inf
	rm -f $(DESTDIR)$(pkgdatadir)/atari/mmusetup.cnf
	rm -f $(DESTDIR)$(pkgdatadir)/atari/hostfs/bdconfig.sys
	rm -f $(DESTDIR)$(pkgdatadir)/atari/hostfs/README
	rm -f $(DESTDIR)$(pkgdatadir)/atari/hostfs/hostfs.dos
	rm -f $(DESTDIR)$(pkgdatadir)/atari/nfcdrom/nfcdrom.bos
	rm -f $(DESTDIR)$(pkgdatadir)/atari/nfjpeg/nfjpeg.prg
	rm -f $(DESTDIR)$(pkgdatadir)/atari/nfosmesa/README
	rm -f $(DESTDIR)$(pkgdatadir)/atari/network/eth0-config.sh
	rm -f $(DESTDIR)$(pkgdatadir)/atari/network/README
	rm -f $(DESTDIR)$(pkgdatadir)/arabridge
	rm -f $(DESTDIR)$(pkgdatadir)/floppy.sh
	rm -f $(DESTDIR)$(pkgdatadir)/atari/tools/README
	rm -f $(DESTDIR)$(pkgdatadir)/atari/tools/pc101us.kbd.bz2
	rm -f $(DESTDIR)$(pkgdatadir)/atari/sound/README
	rm -f $(DESTDIR)$(pkgdatadir)/wm_icon.bmp
	rm -f $(DESTDIR)$(pkgdatadir)/logo.bmp
	rm -f $(DESTDIR)$(pkgdatadir)/logo.png
	rm -f $(DESTDIR)$(pkgdatadir)/logo.png
	rm -f $(DESTDIR)$(datadir)/icons/hicolor/*/apps/aranym*.png
	rm -f $(DESTDIR)$(datadir)/applications/aranym*.desktop
	rm -f $(DESTDIR)$(datadir)/pixmaps/aranym*.png
	-rmdir $(DESTDIR)$(datadir)/icons/hicolor/*/apps
	-rmdir $(DESTDIR)$(datadir)/icons/hicolor/*
	-rmdir $(DESTDIR)$(datadir)/icons/hicolor
	-rmdir $(DESTDIR)$(datadir)/icons
	-rmdir $(DESTDIR)$(datadir)/pixmaps
	rmdir $(DESTDIR)$(pkgdatadir)/atari/hostfs
	rmdir $(DESTDIR)$(pkgdatadir)/atari/nfcdrom
	rmdir $(DESTDIR)$(pkgdatadir)/atari/nfjpeg
	rmdir $(DESTDIR)$(pkgdatadir)/atari/nfosmesa
	rmdir $(DESTDIR)$(pkgdatadir)/atari/network
	rmdir $(DESTDIR)$(pkgdatadir)/atari/tools
	rmdir $(DESTDIR)$(pkgdatadir)/atari/sound
	rmdir $(DESTDIR)$(pkgdatadir)/atari
	-rmdir $(DESTDIR)$(docdir)
	-rmdir $(DESTDIR)$(man1dir)

DISTCLEANFILES = config.cache config.log config.status config.h version_date.h appimage/appimagetool

# remove the excutables copied from the src/Makefile.
# previously, the executables where built in the top level
# directory, and some external build scripts (e.g. rpm) excpect
# them there. Remove this line once the scripts have been updated
CLEANFILES = aranym$(EXEEXT) aratapif$(EXEEXT)

mrproper: distclean
	rm -f $(top_srcdir)/aclocal.m4 $(builddir)/configure $(top_srcdir)/config.h.in
	rm -rf $(top_srcdir)/autom4te.cache

bindist: all
	rm -rf tmpdist
	$(MAKE) DESTDIR=tmpdist install
	chmod -R a+r tmpdist
	cd tmpdist && GZIP=--best tar chozf ../$(PACKAGE_TARNAME).tar.gz *
	rm -rf tmpdist

appimage::
	rm -rf tmpdist
	test "$(srcdir)" = "." || test -d appimage || mkdir appimage
	unset CDPATH && \
	mkdir tmpdist && \
	DESTDIR=`(cd tmpdist; pwd)` && \
	$(MAKE) DESTDIR="$$DESTDIR" install-strip && \
	cd appimage && \
	CPU_TYPE="$(CPU_TYPE)" \
	VERSION="$(VERSION)" \
	top_srcdir="$(abs_top_srcdir)" \
	BUILDROOT="$$DESTDIR" \
	$(SHELL) "$(abs_top_srcdir)/appimage/build.sh"
	rm -rf tmpdist
	
devdist: mrproper 
	cd $(srcdir)/../ && GZIP=--best tar chozf $(PACKAGE_TARNAME).tar.gz aranym

# not really needed anymore, but *.spec files might do a "make depend"
depend:

#
# check/update autogenerated OpenGL source files needed for NFOSMESA_SUPPORT
#
DYNGL_PL = $(top_srcdir)/tools/dyngl.pl
GLTYPES_H = -incfile $(top_srcdir)/atari/nfosmesa/gltypes.h
GLENUMS = -enums $(top_srcdir)/atari/nfosmesa/enum-gl.h

$(top_srcdir)/atari/nfosmesa/glfuncs.h: $(top_srcdir)/tools/glfuncs.h $(DYNGL_PL)
	$(AM_V_GEN)if test -z "$(PERL)"; then \
		echo "$@ is out of date, but PERL not available" >&2; \
	else \
		$(PERL) $(DYNGL_PL) -macros $(top_srcdir)/tools/glfuncs.h > $$$$.tmp && mv $$$$.tmp $@ || { rm -f $$$$.tmp; false; } \
	fi

$(top_srcdir)/atari/nfosmesa/glfuncs-bynum.h: $(top_srcdir)/tools/glfuncs.h $(DYNGL_PL)
	$(AM_V_GEN)if test -z "$(PERL)"; then \
		echo "$@ is out of date, but PERL not available" >&2; \
	else \
		$(PERL) $(DYNGL_PL) $(GLENUMS) -macros $(top_srcdir)/tools/glfuncs.h > $$$$.tmp && mv $$$$.tmp $@ || { rm -f $$$$.tmp; false; } \
	fi

$(top_srcdir)/src/natfeat/nfosmesa/call-gl.c: $(top_srcdir)/atari/nfosmesa/glfuncs.h $(DYNGL_PL)
	$(AM_V_GEN)if test -z "$(PERL)"; then \
		echo "$@ is out of date, but PERL not available" >&2; \
	else \
		$(PERL) $(DYNGL_PL) -calls $(top_srcdir)/tools/glfuncs.h > $$$$.tmp && mv $$$$.tmp $@ || { rm -f $$$$.tmp; false; } \
	fi

$(top_srcdir)/src/natfeat/nfosmesa/dispatch-gl.c: $(top_srcdir)/atari/nfosmesa/glfuncs.h $(DYNGL_PL)
	$(AM_V_GEN)if test -z "$(PERL)"; then \
		echo "$@ is out of date, but PERL not available" >&2; \
	else \
		$(PERL) $(DYNGL_PL) -dispatch $(top_srcdir)/tools/glfuncs.h > $$$$.tmp && mv $$$$.tmp $@ || { rm -f $$$$.tmp; false; } \
	fi

$(top_srcdir)/src/natfeat/nfosmesa/paramcount-gl.c: $(top_srcdir)/atari/nfosmesa/glfuncs.h $(DYNGL_PL)
	$(AM_V_GEN)if test -z "$(PERL)"; then \
		echo "$@ is out of date, but PERL not available" >&2; \
	else \
		$(PERL) $(DYNGL_PL) $(GLENUMS) -paramcount $(top_srcdir)/tools/glfuncs.h > $$$$.tmp && mv $$$$.tmp $@ || { rm -f $$$$.tmp; false; } \
	fi

$(top_srcdir)/atari/nfosmesa/ldg/osmesa.h: $(top_srcdir)/tools/glfuncs.h $(DYNGL_PL) $(top_srcdir)/atari/nfosmesa/gltypes.h
	$(AM_V_GEN)if test -z "$(PERL)"; then \
		echo "$@ is out of date, but PERL not available" >&2; \
	else \
		$(PERL) $(DYNGL_PL) $(GLTYPES_H) $(GLENUMS) -ldgheader $(top_srcdir)/tools/glfuncs.h > $$$$.tmp && mv $$$$.tmp $@ || { rm -f $$$$.tmp; false; } \
	fi

$(top_srcdir)/atari/nfosmesa/osmesa_load.c: $(top_srcdir)/tools/glfuncs.h $(DYNGL_PL)
	$(AM_V_GEN)if test -z "$(PERL)"; then \
		echo "$@ is out of date, but PERL not available" >&2; \
	else \
		$(PERL) $(DYNGL_PL) $(GLTYPES_H) $(GLENUMS) -ldgsource $(top_srcdir)/tools/glfuncs.h > $$$$.tmp && mv $$$$.tmp $@ || { rm -f $$$$.tmp; false; } \
	fi

$(top_srcdir)/atari/nfosmesa/ldg/tiny_gl.h: $(top_srcdir)/tools/glfuncs.h $(DYNGL_PL) $(top_srcdir)/atari/nfosmesa/gltypes.h
	$(AM_V_GEN)if test -z "$(PERL)"; then \
		echo "$@ is out of date, but PERL not available" >&2; \
	else \
		$(PERL) $(DYNGL_PL) $(GLTYPES_H) -tinyldgheader $(top_srcdir)/tools/glfuncs.h > $$$$.tmp && mv $$$$.tmp $@ || { rm -f $$$$.tmp; false; } \
	fi

$(top_srcdir)/atari/nfosmesa/tinygl_load.c: $(top_srcdir)/tools/glfuncs.h $(DYNGL_PL)
	$(AM_V_GEN)if test -z "$(PERL)"; then \
		echo "$@ is out of date, but PERL not available" >&2; \
	else \
		$(PERL) $(DYNGL_PL) -tinyldgsource $(top_srcdir)/tools/glfuncs.h > $$$$.tmp && mv $$$$.tmp $@ || { rm -f $$$$.tmp; false; } \
	fi

$(top_srcdir)/atari/nfosmesa/link-tinygl.h: $(top_srcdir)/tools/glfuncs.h $(DYNGL_PL)
	$(AM_V_GEN)if test -z "$(PERL)"; then \
		echo "$@ is out of date, but PERL not available" >&2; \
	else \
		$(PERL) $(DYNGL_PL) -tinyldglink $(top_srcdir)/tools/glfuncs.h > $$$$.tmp && mv $$$$.tmp $@ || { rm -f $$$$.tmp; false; } \
	fi

$(top_srcdir)/atari/nfosmesa/slb/tiny_gl.h: $(top_srcdir)/tools/glfuncs.h $(DYNGL_PL) $(top_srcdir)/atari/nfosmesa/gltypes.h
	$(AM_V_GEN)if test -z "$(PERL)"; then \
		echo "$@ is out of date, but PERL not available" >&2; \
	else \
		$(PERL) $(DYNGL_PL) $(GLTYPES_H) -tinyslbheader $(top_srcdir)/tools/glfuncs.h > $$$$.tmp && mv $$$$.tmp $@ || { rm -f $$$$.tmp; false; } \
	fi

$(top_srcdir)/atari/nfosmesa/slb/osmesa.h: $(top_srcdir)/tools/glfuncs.h $(DYNGL_PL) $(top_srcdir)/atari/nfosmesa/gltypes.h
	$(AM_V_GEN)if test -z "$(PERL)"; then \
		echo "$@ is out of date, but PERL not available" >&2; \
	else \
		$(PERL) $(DYNGL_PL) $(GLTYPES_H) $(GLENUMS) -slbheader $(top_srcdir)/tools/glfuncs.h > $$$$.tmp && mv $$$$.tmp $@ || { rm -f $$$$.tmp; false; } \
	fi

$(top_srcdir)/atari/nfosmesa/tinygl_loadslb.c: $(top_srcdir)/tools/glfuncs.h $(DYNGL_PL)
	$(AM_V_GEN)if test -z "$(PERL)"; then \
		echo "$@ is out of date, but PERL not available" >&2; \
	else \
		$(PERL) $(DYNGL_PL) $(GLENUMS) -tinyslbsource $(top_srcdir)/tools/glfuncs.h > $$$$.tmp && mv $$$$.tmp $@ || { rm -f $$$$.tmp; false; } \
	fi

$(top_srcdir)/atari/nfosmesa/osmesa_loadslb.c: $(top_srcdir)/tools/glfuncs.h $(DYNGL_PL)
	$(AM_V_GEN)if test -z "$(PERL)"; then \
		echo "$@ is out of date, but PERL not available" >&2; \
	else \
		$(PERL) $(DYNGL_PL) $(GLENUMS) -slbsource $(top_srcdir)/tools/glfuncs.h > $$$$.tmp && mv $$$$.tmp $@ || { rm -f $$$$.tmp; false; } \
	fi

# For tests
gldiff:
	$(PERL) $(DYNGL_PL) -protos /usr/include/GL/gl.h /usr/include/GL/glext.h /usr/include/GL/osmesa.h | diff $(top_srcdir)/tools/glfuncs.h - || true
	$(PERL) $(DYNGL_PL) -macros $(top_srcdir)/tools/glfuncs.h | diff $(top_srcdir)/atari/nfosmesa/glfuncs.h - || true
	$(PERL) $(DYNGL_PL) -calls $(top_srcdir)/tools/glfuncs.h | diff $(top_srcdir)/src/natfeat/nfosmesa/call-gl.c - || true
	$(PERL) $(DYNGL_PL) -dispatch $(top_srcdir)/tools/glfuncs.h | diff $(top_srcdir)/src/natfeat/nfosmesa/dispatch-gl.c - || true
	$(PERL) $(DYNGL_PL) $(GLENUMS) -ldgheader $(top_srcdir)/tools/glfuncs.h | diff $(top_srcdir)/atari/nfosmesa/ldg/osmesa.h - || true
	$(PERL) $(DYNGL_PL) $(GLENUMS) -ldgsource $(top_srcdir)/tools/glfuncs.h | diff $(top_srcdir)/atari/nfosmesa/osmesa_load.c - || true
	$(PERL) $(DYNGL_PL) -tinyldgheader $(top_srcdir)/tools/glfuncs.h | diff $(top_srcdir)/atari/nfosmesa/ldg/tiny_gl.h - || true
	$(PERL) $(DYNGL_PL) -tinyldgsource $(top_srcdir)/tools/glfuncs.h | diff $(top_srcdir)/atari/nfosmesa/tinygl_load.c - || true
	$(PERL) $(DYNGL_PL) -tinyldglink $(top_srcdir)/tools/glfuncs.h | diff $(top_srcdir)/atari/nfosmesa/link-tinygl.h - || true
	$(PERL) $(DYNGL_PL) -tinyslbheader $(top_srcdir)/tools/glfuncs.h | diff $(top_srcdir)/atari/nfosmesa/slb/tiny_gl.h - || true
	$(PERL) $(DYNGL_PL) $(GLENUMS) -tinyslbsource $(top_srcdir)/tools/glfuncs.h | diff $(top_srcdir)/atari/nfosmesa/tinygl_loadslb.c - || true

EXTRA_DIST = \
	BUGS FAQ README.md README-cygwin.in \
	aranym.spec autogen.sh \
	android atari contrib data tools \
	extras/hostfs/hostfs.dos \
	extras/nfcdrom/nfcdrom.bos \
	extras/nfjpeg/nfjpeg.prg \
	appimage/build.sh \
	appimage/desktopintegration \
	appimage/excludelist \
	appimage/functions.sh

# Corresponding source required by the opt-in modern040 build/library tool.
EXTRA_DIST += tools/build-modern040-library.py \
 src/modern040/native-ea-plan.hpp tests/modern-jit/native-ea-plan-test.cpp \
 tests/modern-jit/native-rmw-plan-test.cpp \
 tests/modern-jit/native-movem-plan-test.cpp \
 tests/modern-jit/generate-rmw-oracle.py tests/modern-jit/guest/rmw-oracle.c \
 tests/modern-jit/guest/rmw-fault.c tests/modern-jit/guest/rmw-fault.S \
 tests/modern-jit/guest/movem-alias.c tests/modern-jit/guest/movem-alias.S \
 tests/modern-jit/guest/movem-oracle.c tests/modern-jit/guest/movem-oracle.S \
 tests/modern-jit/guest/movem-bus.c \
 tests/modern-jit/guest/movem-fault.c tests/modern-jit/guest/movem-fault.S \
 tests/modern-jit/guest/movem-fetch.c tests/modern-jit/guest/movem-fetch.S \
 tests/modern-jit/guest/writeback.c tests/modern-jit/guest/writeback.S \
 tests/modern-jit/guest/trace-fault.c tests/modern-jit/guest/trace-fault.S \
 tests/modern-jit/guest/irq-boundary.c tests/modern-jit/guest/irq-boundary.S \
 tests/modern-jit/guest/moves-probe.c tests/modern-jit/guest/moves-probe.S \
 tests/modern-jit/guest/rte-boundary.c tests/modern-jit/guest/rte-boundary.S \
 tests/modern-jit/guest/bitfield-containment.c tests/modern-jit/guest/bitfield-containment.S \
 tests/cpu-core-upgrade/runner/trace-fault-test.cpp \
 tests/cpu-core-upgrade/runner/irq-restart-test.cpp \
 tests/cpu-core-upgrade/runner/write-producer-test.cpp \
 tests/cpu-core-upgrade/runner/multiwrite-producer-test.cpp \
 tests/cpu-core-upgrade/runner/writeback-layout.hpp tests/cpu-core-upgrade/runner/writeback-layout-test.cpp \
 tests/cpu-core-upgrade/runner/producer-boundary-cases.hpp \
 tests/cpu-core-upgrade/runner/detailed-bus-test.cpp tests/cpu-core-upgrade/runner/detailed-bus-c-test.c \
 tests/cpu-core-upgrade/runner/physical-writeback-test.cpp tools/adapt-modern040-writeback-frame.py \
 tests/cpu-core-upgrade/runner/physical-read-test.cpp \
 tests/cpu-core-upgrade/runner/moves-trace-test.cpp \
 tests/cpu-core-upgrade/runner/rte-load-test.cpp \
 src/modern040/RTE-LOAD-POLICY.json \
 src/modern040/BITFIELD-WRITE-CONTAINMENT.json \
 src/modern040/DETAILED-BUS-CONTRACT.json \
 src/modern040/PHYSICAL-WRITEBACK-FRAME-CORRECTION.json src/modern040/PHYSICAL-WRITEBACK-POLICY.json \
 src/modern040/PHYSICAL-READ-POLICY.json \
 tools/adapt-modern040-trace-restart.py src/modern040/TRACE-RESTART-CORRECTION.json \
 src/modern040/MOVES-TRACE-POLICY.json \
 tests/cpu-core-upgrade/runner/movem-fetch-restart-test.cpp \
 tests/cpu-core-upgrade/runner/movem-restart-test.cpp \
 tools/adapt-modern040-movem-restart.py src/modern040/MOVEM-RESTART-CORRECTION.json \
 tools/adapt-modern040-movem.py tests/modern-jit/MOVEM-PREDECREMENT-CORRECTION.json \
 tests/modern-jit/import-provenance.json \
 src/modern040/native-ea-plan-source.json \
 tests/modern-jit/generate-extended-ea.py tests/modern-jit/guest/extended-ea-oracle.c \
 tests/modern-jit/generate-scalar-oracle.py tests/modern-jit/guest/scalar-oracle.c \
 src/modern040/integer.cpp src/modern040/restart.cpp src/modern040/RESTART-SOURCE.json \
 tools/extract-modern040-restart.py tests/cpu-core-upgrade/runner/restart.hpp \
 tests/cpu-core-upgrade/runner/restart-test.cpp src/modern040/include/uae/types.h \
 tests/modern-jit/guest/access-probe.c tests/modern-jit/guest/access-probe.S \
 tests/modern-jit/guest/ram-benchmark.c tests/modern-jit/guest/ram-benchmark.S \
 tests/modern-jit/generate-ram-alias.py tests/modern-jit/guest/ram-alias.c \
 tests/modern-jit/guest/ram-alias-probes.h tests/modern-jit/guest/ram-alias-probes.S \
 src/modern040/COPYING.WinUAE src/modern040/SOURCE.json src/modern040/README.txt \
 tests/cpu-core-upgrade/runner/core.cpp tests/cpu-core-upgrade/runner/core.hpp \
 tests/cpu-core-upgrade/runner/c-api.cpp tests/cpu-core-upgrade/runner/c-api.h \
 tests/cpu-core-upgrade/platform/adapter.h \
 tests/modern-jit/modern-fallback.cpp tests/modern-jit/upstream-compemu.cpp \
 tests/modern-jit/upstream-compstbl.cpp tests/modern-jit/upstream-comptbl.h
