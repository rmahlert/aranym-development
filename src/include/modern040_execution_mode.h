/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef ARANYM_MODERN040_EXECUTION_MODE_H
#define ARANYM_MODERN040_EXECUTION_MODE_H
#include <cstdint>

// Native ARAnyM JIT memory operations use physical guest addresses.  The
// modern core must own execution while 68040 paging is active. Transparent
// ranges do not remap addresses, so ordinary TTR cache/supervisor attributes
// remain compatible; a data-TTR write-protect attribute is not and requires
// the modern permission path.
inline bool aranym_modern_translation_active(std::uint32_t tc,
    std::uint32_t itt0,std::uint32_t itt1,
    std::uint32_t dtt0,std::uint32_t dtt1) {
    constexpr std::uint32_t enabled=1u<<15,write_protect=1u<<2;
    (void)itt0;(void)itt1;
    return (tc&enabled)||
        ((dtt0&enabled)&&(dtt0&write_protect))||
        ((dtt1&enabled)&&(dtt1&write_protect));
}

#endif
