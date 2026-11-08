#pragma once
#include <cstdint>
#define WINAPI
using DWORD = uint32_t;
using ULONG = uint32_t;
using LONG = int32_t;
using BOOL = int;
using BYTE = unsigned char;
using HANDLE = void *;
using HKEY = void *;
using FARPROC = void (*)();
struct OVERLAPPED { uintptr_t Internal, InternalHigh; DWORD Offset, OffsetHigh; HANDLE hEvent; };
using LPOVERLAPPED = OVERLAPPED *;
#define TRUE 1
#define FALSE 0
#define INVALID_HANDLE_VALUE ((HANDLE)(intptr_t)-1)
#define HKEY_LOCAL_MACHINE ((HKEY)(intptr_t)-2)
#define ERROR_SUCCESS 0
#define ERROR_FILE_NOT_FOUND 2
#define ERROR_ACCESS_DENIED 5
#define ERROR_INVALID_HANDLE 6
#define ERROR_INVALID_DATA 13
#define ERROR_INVALID_PARAMETER 87
#define ERROR_MORE_DATA 234
#define ERROR_NO_MORE_ITEMS 259
#define ERROR_OPERATION_ABORTED 995
#define ERROR_IO_INCOMPLETE 996
#define ERROR_IO_PENDING 997
#define REG_SZ 1
#define REG_BINARY 3
#define KEY_READ 0x20019
#define KEY_WOW64_64KEY 0x100
#define GENERIC_READ 0x80000000
#define GENERIC_WRITE 0x40000000
#define OPEN_EXISTING 3
#define FILE_ATTRIBUTE_SYSTEM 4
#define FILE_FLAG_OVERLAPPED 0x40000000
#define FORMAT_MESSAGE_FROM_SYSTEM 0x1000
#define FORMAT_MESSAGE_IGNORE_INSERTS 0x200
#define LANG_NEUTRAL 0
#define SUBLANG_DEFAULT 1
#define MAKELANGID(a,b) ((a) | ((b)<<10))
#define WAIT_OBJECT_0 0
#define WAIT_TIMEOUT 258
#define WAIT_FAILED 0xffffffff
#define INFINITE 0xffffffff
LONG RegOpenKeyExA(HKEY, const char *, DWORD, DWORD, HKEY *);
LONG RegEnumKeyExA(HKEY, DWORD, char *, DWORD *, void *, void *, void *, void *);
LONG RegQueryValueExA(HKEY, const char *, void *, DWORD *, BYTE *, DWORD *);
LONG RegCloseKey(HKEY);
DWORD FormatMessage(DWORD, const void *, DWORD, DWORD, char *, DWORD, void *);
DWORD GetLastError();
void SetLastError(DWORD);
HANDLE GetModuleHandleA(const char *);
FARPROC GetProcAddress(HANDLE, const char *);
BOOL CancelIo(HANDLE);
BOOL GetOverlappedResult(HANDLE, OVERLAPPED *, DWORD *, BOOL);
HANDLE CreateFile(const char *, DWORD, DWORD, void *, DWORD, DWORD, HANDLE);
HANDLE CreateEvent(void *, BOOL, BOOL, const char *);
BOOL CloseHandle(HANDLE);
BOOL ResetEvent(HANDLE);
BOOL DeviceIoControl(HANDLE, DWORD, void *, DWORD, void *, DWORD, DWORD *, OVERLAPPED *);
DWORD WaitForSingleObject(HANDLE, DWORD);
BOOL ReadFile(HANDLE, void *, DWORD, DWORD *, OVERLAPPED *);
BOOL WriteFile(HANDLE, const void *, DWORD, DWORD *, OVERLAPPED *);

// Legacy source uses the ANSI aliases; keep the pre-fix control compilable.
#define RegOpenKeyEx RegOpenKeyExA
#define RegEnumKeyEx RegEnumKeyExA
#define RegQueryValueEx RegQueryValueExA
