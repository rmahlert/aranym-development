#pragma once
#ifdef TAP_REAL_WINDOWS
#include_next <winioctl.h>
#else
#define FILE_DEVICE_UNKNOWN 0x22
#define METHOD_BUFFERED 0
#define FILE_ANY_ACCESS 0
#define CTL_CODE(device, function, method, access) (((device) << 16) | ((access) << 14) | ((function) << 2) | (method))
#endif
