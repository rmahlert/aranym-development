#pragma once
struct TapOptions { struct { char type[32]; char tunnel[256]; } ethernet[1]; };
extern TapOptions bx_options;
inline void safe_strncpy(char *out, const char *in, size_t capacity) { snprintf(out, capacity, "%s", in); }
void panicbug(const char *, ...);
