/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef ARANYM_ARM64_NATIVE_FLAGS_H
#define ARANYM_ARM64_NATIVE_FLAGS_H
// The interpreter and generated code share the architectural NZCV encoding.
struct flag_struct { unsigned int nzcv, x; };
extern struct flag_struct regflags;
#define SET_CFLG(value) (regflags.nzcv = (regflags.nzcv & ~(1u<<29)) | ((!!(value))<<29))
#define SET_ZFLG(value) (regflags.nzcv = (regflags.nzcv & ~(1u<<30)) | ((!!(value))<<30))
#define SET_NFLG(value) (regflags.nzcv = (regflags.nzcv & ~(1u<<31)) | ((unsigned int)!!(value)<<31))
#define SET_VFLG(value) (regflags.nzcv = (regflags.nzcv & ~(1u<<28)) | ((!!(value))<<28))
#define SET_XFLG(value) (regflags.x = !!(value))
#define GET_CFLG() ((regflags.nzcv >> 29) & 1)
#define GET_ZFLG() ((regflags.nzcv >> 30) & 1)
#define GET_NFLG() ((regflags.nzcv >> 31) & 1)
#define GET_VFLG() ((regflags.nzcv >> 28) & 1)
#define GET_XFLG() (regflags.x)
#define CLEAR_CZNV() (regflags.nzcv = 0)
#define COPY_CARRY() SET_XFLG(GET_CFLG())
static inline int cctrue(int cc) {
    const unsigned int n=GET_NFLG(),z=GET_ZFLG(),c=GET_CFLG(),v=GET_VFLG();
    switch(cc) {
    case 0:return 1;case 1:return 0;case 2:return !c&&!z;case 3:return c||z;
    case 4:return !c;case 5:return c;case 6:return !z;case 7:return z;
    case 8:return !v;case 9:return v;case 10:return !n;case 11:return n;
    case 12:return n==v;case 13:return n!=v;case 14:return !z&&(n==v);case 15:return z||(n!=v);
    }
    return 0;
}
#endif
