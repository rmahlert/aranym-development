/* SPDX-License-Identifier: GPL-2.0-or-later
 * Minimal build-host portability shim for unmodified upstream generators and
 * decoder. This is not a substitute for the production CPU headers. */
#pragma once
#include <cassert>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <strings.h>
#include <uae/types.h>
#include "tchar.h"
#define ENUMDECL typedef enum
#define ENUMNAME(name) name
#define REGPARAM
#define REGPARAM2
#define REGPARAM3
#define xmalloc(type, count) static_cast<type *>(std::malloc(sizeof(type) * (count)))
#define xfree(pointer) std::free(pointer)
#define strnicmp strncasecmp
#define NOWARN_UNUSED(declaration) declaration
#define NORETURN [[noreturn]]
#define STATIC_INLINE static inline
#define CPU_EMU_SIZE 0
#define _istspace isspace
#define _vsnprintf vsnprintf
inline char *ua(const TCHAR *value) { return ::strdup(value); }
void write_log(const TCHAR *, ...);
