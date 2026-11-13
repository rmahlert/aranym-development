/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef ARANYM_DISPLAY_DIAGNOSTICS_H
#define ARANYM_DISPLAY_DIAGNOSTICS_H
#include <cstdio>
#include <cstdlib>
#include <cstdarg>
/* Host-local observations only: no target reads, mode/config changes or
 * additional NatFeat calls. Off by default and bounded per translation unit. */
static inline bool aranym_display_diagnostics_enabled() {
    static const bool enabled=std::getenv("ARANYM_DISPLAY_DIAGNOSTICS")!=nullptr;
    return enabled;
}
static inline void aranym_display_trace(const char *format, ...) {
    static unsigned count=0;
    if(!aranym_display_diagnostics_enabled()||count>=128)return;
    ++count;std::fputs("DISPLAY_TRACE ",stderr);
    va_list args;va_start(args,format);std::vfprintf(stderr,format,args);va_end(args);
    std::fputc('\n',stderr);
}
#endif
