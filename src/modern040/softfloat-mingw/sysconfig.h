#ifndef ARANYM_MODERN040_SOFTFLOAT_MINGW_SYSCONFIG_H
#define ARANYM_MODERN040_SOFTFLOAT_MINGW_SYSCONFIG_H

/* Minimal WinUAE host contract for the isolated arithmetic backend.  Keep
 * Windows SDK headers and the rest of WinUAE's host integration out of this
 * static library: it imports no host, chipset, scheduler or memory service. */
#define FSUAE 1
#define UAE_TARGET_UNIX 1
#define FPUEMU 1
#define WITH_SOFTFLOAT 1
#define SUPPORT_LONG_DOUBLE 1
#define MAX_DPATH 1000
#define MAX_AMIGAMONITORS 4
#define MAX_AMIGADISPLAYS 4
#define PACKAGE_STRING "ARAnyM modern040 SoftFloat"

#define RETSIGTYPE void
#define TIME_WITH_SYS_TIME 0
#define HAVE_STRING_H 1
#define HAVE_SYS_TYPES_H 1
#define HAVE_SYS_STAT_H 1
#define HAVE_STDINT_H 1

#define SIZEOF_CHAR 1
#define SIZEOF_SHORT 2
#define SIZEOF_INT 4
#define SIZEOF_LONG 4
#define SIZEOF_LONG_LONG 8
#define SIZEOF_FLOAT 4
#define SIZEOF_DOUBLE 8
#define SIZEOF_VOID_P 8
#define CPU_64_BIT 1

#ifndef ZEXPORT
#define ZEXPORT
#endif
typedef long uae_atomic;

#endif
