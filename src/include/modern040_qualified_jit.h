/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef ARANYM_MODERN040_QUALIFIED_JIT_H
#define ARANYM_MODERN040_QUALIFIED_JIT_H
#include <cstdlib>
#include <cstring>

// Only the preserving Windows upgrade profile selects qualified defaults.
// Experimental profiles remain opt-in. Explicit 0 disables a selected family
// in either profile, including the existing negative qualification controls.
inline bool aranym_qualified_jit_default() {
#ifdef MODERN040_QUALIFIED_JIT_DEFAULTS
    return true;
#else
    return false;
#endif
}
inline bool aranym_qualified_jit_gate(const char *name) {
    const char *value=std::getenv(name);
    if(!value)return aranym_qualified_jit_default();
    return !std::strcmp(value,"1")||!std::strcmp(value,"on")||
        !std::strcmp(value,"yes")||!std::strcmp(value,"true");
}
// Development trials stay off unless explicitly requested, even in the
// preserving upgrade profile. Do not route these through the qualified
// default: doing that silently widens the installed execution profile.
inline bool aranym_jit_opt_in(const char *name) {
    const char *value=std::getenv(name);
    return value&&(!std::strcmp(value,"1")||!std::strcmp(value,"on")||
        !std::strcmp(value,"yes")||!std::strcmp(value,"true"));
}
#endif
