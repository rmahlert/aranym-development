// Runs the production TAP implementation against a deterministic Windows API
// shim. No installed driver, registry, or physical adapter is modified.
#include <algorithm>
#include <atomic>
#include <cstdarg>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include <thread>
#include <chrono>
#include "shim/cpu_emulation.h"
#include "shim/windows_api.h"
#include "shim/main.h"
#include "../../src/Unix/cygwin/ethernet_cygwin.cpp"

TapOptions bx_options;
void panicbug(const char *, ...) {}
static int failures;
#define CHECK(test) do { if (!(test)) { fprintf(stderr, "line %d: %s\n", __LINE__, #test); ++failures; } } while (0)
static thread_local DWORD last_error;
DWORD GetLastError() { return last_error; }
void SetLastError(DWORD value) { last_error = value; }
DWORD FormatMessage(DWORD, const void *, DWORD, DWORD, char *buffer, DWORD capacity, void *) {
    snprintf(buffer, capacity, "test Windows error"); return 18;
}
struct Value { DWORD type; std::vector<BYTE> bytes; };
struct Key {
    std::map<std::string, std::unique_ptr<Key>> children;
    std::map<std::string, Value> values;
};
static Key root;
static int registry_handles;
static bool enum_error;
static DWORD managed_access;
static std::string opened_path;
static Key *add_key(Key &parent, const std::string &path) {
    Key *at = &parent;
    size_t start = 0;
    for (;;) {
        size_t end = path.find('\\', start);
        std::string name = path.substr(start, end - start);
        auto &child = at->children[name];
        if (!child) child.reset(new Key);
        at = child.get();
        if (end == std::string::npos) return at;
        start = end + 1;
    }
}
static void set_string(Key *key, const std::string &name, const std::string &value, bool terminated = true) {
    Value &v = key->values[name]; v.type = REG_SZ;
    v.bytes.assign(value.begin(), value.end());
    if (terminated) v.bytes.push_back(0);
}
LONG RegOpenKeyExA(HKEY parent, const char *path, DWORD, DWORD access, HKEY *result) {
    if (!strcmp(path, "SOFTWARE\\ARAnyM\\LAN")) managed_access = access;
    Key *at = parent == HKEY_LOCAL_MACHINE ? &root : static_cast<Key *>(parent);
    std::string text(path); size_t start = 0;
    for (;;) {
        size_t end = text.find('\\', start);
        auto child = at->children.find(text.substr(start, end - start));
        if (child == at->children.end()) return ERROR_FILE_NOT_FOUND;
        at = child->second.get();
        if (end == std::string::npos) break;
        start = end + 1;
    }
    *result = at; ++registry_handles; return ERROR_SUCCESS;
}
LONG RegEnumKeyExA(HKEY key, DWORD index, char *buffer, DWORD *length, void *, void *, void *, void *) {
    if (enum_error) return ERROR_ACCESS_DENIED;
    auto &children = static_cast<Key *>(key)->children;
    if (index >= children.size()) return ERROR_NO_MORE_ITEMS;
    auto it = children.begin(); std::advance(it, index);
    if (*length <= it->first.size()) return ERROR_MORE_DATA;
    strcpy(buffer, it->first.c_str()); *length = it->first.size(); return ERROR_SUCCESS;
}
LONG RegQueryValueExA(HKEY key, const char *name, void *, DWORD *type, BYTE *buffer, DWORD *length) {
    auto &values = static_cast<Key *>(key)->values;
    auto it = values.find(name);
    if (it == values.end()) return ERROR_FILE_NOT_FOUND;
    *type = it->second.type;
    if (*length < it->second.bytes.size()) { *length = it->second.bytes.size(); return ERROR_MORE_DATA; }
    *length = it->second.bytes.size(); memcpy(buffer, it->second.bytes.data(), *length); return ERROR_SUCCESS;
}
LONG RegCloseKey(HKEY) { --registry_handles; return ERROR_SUCCESS; }
static const char *guid1 = "{01234567-89AB-CDEF-0123-456789ABCDEF}";
static const char *guid2 = "{11234567-89AB-CDEF-0123-456789ABCDEF}";
static Key *adapter(const char *slot, const char *guid, const char *component, const char *name) {
    Key *key = add_key(root, std::string(ADAPTER_KEY) + "\\" + slot);
    set_string(key, "ComponentId", component); set_string(key, "NetCfgInstanceId", guid);
    Key *connection = add_key(root, std::string(NETWORK_CONNECTIONS_KEY) + "\\" + guid + "\\Connection");
    set_string(connection, "Name", name); return key;
}

struct Handle { bool event, closed; };
static std::vector<std::unique_ptr<Handle>> handles;
struct Operation { HANDLE handle; DWORD length; bool read, aborted; pthread_t owner; };
static std::map<OVERLAPPED *, Operation> operations;
static ULONG version[3];
static DWORD version_size;
static bool ioctl_failure, media_failure, use_cancel_ex, ioctl_pending, wait_failure;
static int create_event_failure, event_calls, media_up, media_down, cancel_calls, drain_calls, result_calls;
static std::atomic<bool> read_started;
static int read_mode, write_mode; // 0 immediate; 1 pending success; 2 pending failure; 3 short; 4 wait for cancel
static HANDLE new_handle(bool event) {
    handles.emplace_back(new Handle{event, false}); return handles.back().get();
}
static int live_handles() {
    return std::count_if(handles.begin(), handles.end(), [](const std::unique_ptr<Handle> &h) { return !h->closed; });
}
HANDLE CreateFile(const char *path, DWORD access, DWORD share, void *, DWORD, DWORD flags, HANDLE) {
    opened_path = path;
    CHECK(strstr(path, ".tap") != nullptr);
    CHECK(access == (GENERIC_READ | GENERIC_WRITE)); CHECK(share == 0);
    CHECK((flags & FILE_FLAG_OVERLAPPED) != 0); return new_handle(false);
}
HANDLE CreateEvent(void *, BOOL manual, BOOL initial, const char *) {
    ++event_calls; CHECK(manual == TRUE); CHECK(initial == FALSE);
    if (event_calls == create_event_failure) return nullptr;
    return new_handle(true);
}
BOOL CloseHandle(HANDLE value) {
    CHECK(value && value != INVALID_HANDLE_VALUE);
    Handle *handle = static_cast<Handle *>(value); CHECK(!handle->closed);
    for (auto &op : operations) CHECK(op.second.handle != value && op.first->hEvent != value);
    handle->closed = true; return TRUE;
}
BOOL ResetEvent(HANDLE value) { CHECK(value && !static_cast<Handle *>(value)->closed); return TRUE; }
BOOL DeviceIoControl(HANDLE h, DWORD code, void *, DWORD input_size, void *output, DWORD output_size, DWORD *returned, OVERLAPPED *ov) {
    CHECK(ov && ov->hEvent); CHECK(input_size == output_size);
    if (ioctl_failure) { SetLastError(ERROR_ACCESS_DENIED); return FALSE; }
    if (code == TAP_IOCTL_GET_VERSION) {
        CHECK(output_size == sizeof(version)); memcpy(output, version, sizeof(version)); *returned = version_size;
    } else {
        CHECK(code == TAP_IOCTL_SET_MEDIA_STATUS); CHECK(output_size == sizeof(ULONG));
        ULONG connected = *static_cast<ULONG *>(output);
        if (connected) ++media_up; else ++media_down;
        if (connected && media_failure) { SetLastError(ERROR_ACCESS_DENIED); return FALSE; }
        *returned = sizeof(ULONG);
    }
    if (ioctl_pending) {
        operations[ov] = Operation{h, *returned, false, false, pthread_self()};
        SetLastError(ERROR_IO_PENDING); return FALSE;
    }
    return TRUE;
}
static BOOL cancel_specific(HANDLE h, OVERLAPPED *ov) {
    ++cancel_calls;
    for (auto &it : operations) if (it.second.handle == h && (!ov || it.first == ov)) it.second.aborted = true;
    return TRUE;
}
HANDLE GetModuleHandleA(const char *) { return reinterpret_cast<HANDLE>(1); }
FARPROC GetProcAddress(HANDLE, const char *) {
    if (!use_cancel_ex) return nullptr;
    auto function = cancel_specific; FARPROC address;
    static_assert(sizeof(function) == sizeof(address), "procedure size");
    memcpy(&address, &function, sizeof(address)); return address;
}
BOOL CancelIo(HANDLE h) {
    for (auto &it : operations) if (it.second.handle == h) CHECK(pthread_equal(it.second.owner, pthread_self()));
    return cancel_specific(h, nullptr);
}
BOOL GetOverlappedResult(HANDLE h, OVERLAPPED *ov, DWORD *transferred, BOOL wait) {
    ++result_calls;
    auto it = operations.find(ov);
    CHECK(it != operations.end());
    if (it == operations.end()) return FALSE;
    CHECK(it->second.handle == h);
    Operation op = it->second; operations.erase(it);
    if (op.aborted) { ++drain_calls; CHECK(wait); SetLastError(ERROR_OPERATION_ABORTED); return FALSE; }
    if ((op.read ? read_mode : write_mode) == 2 && !wait) { SetLastError(ERROR_ACCESS_DENIED); return FALSE; }
    *transferred = op.length; return TRUE;
}
DWORD WaitForSingleObject(HANDLE event, DWORD timeout) {
    CHECK(timeout == 100);
    if (wait_failure) { SetLastError(ERROR_INVALID_HANDLE); return WAIT_FAILED; }
    for (auto &it : operations) if (it.first->hEvent == event) {
        if (it.second.read && read_mode == 4) return WAIT_TIMEOUT;
        return WAIT_OBJECT_0;
    }
    CHECK(false); return WAIT_FAILED;
}
static BOOL begin_io(bool read, HANDLE h, DWORD length, DWORD *transferred, OVERLAPPED *ov) {
    int mode = read ? read_mode : write_mode;
    CHECK(!operations.count(ov));
    if (mode == 0 || mode == 3) { *transferred = mode == 3 ? length - 1 : length; return TRUE; }
    operations[ov] = Operation{h, length, read, false, pthread_self()};
    if (read) read_started.store(true, std::memory_order_release);
    SetLastError(ERROR_IO_PENDING); return FALSE;
}
BOOL ReadFile(HANDLE h, void *, DWORD length, DWORD *out, OVERLAPPED *ov) { return begin_io(true, h, length, out, ov); }
BOOL WriteFile(HANDLE h, const void *, DWORD length, DWORD *out, OVERLAPPED *ov) { return begin_io(false, h, length, out, ov); }

static void reset() {
    CHECK(registry_handles == 0); CHECK(operations.empty()); CHECK(live_handles() == 0);
    root = Key(); handles.clear(); enum_error = false; managed_access = 0; opened_path.clear();
    version[0] = 9; version[1] = 27; version[2] = 0; version_size = sizeof(version);
    ioctl_failure = media_failure = ioctl_pending = wait_failure = false; use_cancel_ex = true;
    create_event_failure = event_calls = media_up = media_down = cancel_calls = drain_calls = result_calls = 0;
    read_started = false; read_mode = write_mode = 0;
    strcpy(bx_options.ethernet[0].type, "bridge"); strcpy(bx_options.ethernet[0].tunnel, "ARAnyM LAN");
    adapter("0000", guid1, "tap0901", "ARAnyM LAN");
}
static void registry_tests() {
    reset(); CHECK(is_tap_win32_dev(guid1));
    Key *key = add_key(root, std::string(ADAPTER_KEY) + "\\0000");
    set_string(key, "ComponentId", "pci\\physical"); CHECK(!is_tap_win32_dev(guid1));
    set_string(key, "ComponentId", "tap0801"); CHECK(is_tap_win32_dev(guid1));
    set_string(key, "ComponentId", "tap0901", false); CHECK(is_tap_win32_dev(guid1));
    set_string(key, "NetCfgInstanceId", "{01234567-89ab-cdef-0123-456789abcdef}", false); CHECK(is_tap_win32_dev(guid1));
    CHECK(!is_tap_win32_dev("{not-a-guid}"));
    set_string(key, "ComponentId", std::string("tap0901\0evil", 12)); CHECK(!is_tap_win32_dev(guid1));
    set_string(key, "ComponentId", std::string(256, 'x')); CHECK(!is_tap_win32_dev(guid1));
    set_string(key, "ComponentId", "tap0901"); key->values["ComponentId"].type = REG_BINARY;
    CHECK(!is_tap_win32_dev(guid1));
    key->values["ComponentId"].type = REG_SZ;
    char out[64], requested[256]; strcpy(requested, "ARAnyM LAN");
    CHECK(get_device_guid(out, sizeof(out), requested, sizeof(requested)) == 0); CHECK(!strcmp(out, guid1));
    strcpy(requested, "{01234567-89ab-cdef-0123-456789abcdef}");
    CHECK(get_device_guid(out, sizeof(out), requested, sizeof(requested)) == 0);
    adapter("0001", guid2, "tap0901", guid1); // misleading friendly name is not GUID identity
    strcpy(requested, guid1);
    CHECK(get_device_guid(out, sizeof(out), requested, sizeof(requested)) == 0); CHECK(!strcmp(out, guid1));
    strcpy(requested, "wrong adapter"); CHECK(get_device_guid(out, sizeof(out), requested, sizeof(requested)) == -1);
    adapter("0001", guid2, "tap0901", "ARAnyM LAN"); strcpy(requested, "ARAnyM LAN");
    CHECK(get_device_guid(out, sizeof(out), requested, sizeof(requested)) == -1);
    enum_error = true; CHECK(!is_tap_win32_dev(guid1)); CHECK(registry_handles == 0);
}
static void open_tests() {
    for (int fail = 1; fail <= 3; ++fail) {
        reset(); create_event_failure = fail;
        WinTapEthernetHandler handler(0); CHECK(!handler.open()); handler.close(); CHECK(live_handles() == 0);
    }
    for (int major : {7, 8, 10}) {
        reset(); version[0] = major; version[1] = 0;
        WinTapEthernetHandler handler(0); CHECK(!handler.open()); CHECK(media_up == 0); CHECK(live_handles() == 0);
    }
    reset(); version_size = 4;
    { WinTapEthernetHandler h(0); CHECK(!h.open()); CHECK(live_handles() == 0); }
    reset(); ioctl_failure = true;
    { WinTapEthernetHandler h(0); CHECK(!h.open()); CHECK(live_handles() == 0); }
    reset(); media_failure = true;
    { WinTapEthernetHandler h(0); CHECK(!h.open()); CHECK(media_down == 1); CHECK(live_handles() == 0); }
    for (ULONG major : {8u, 9u}) {
        reset(); version[0] = major; version[1] = 1; ioctl_pending = true;
        { WinTapEthernetHandler h(0); CHECK(h.open()); CHECK(media_up == 1); h.close(); h.close(); CHECK(media_down == 1); }
        CHECK(live_handles() == 0);
    }
}
static void control_tests() {
    reset();
    Key *key = add_key(root, std::string(ADAPTER_KEY) + "\\0000");
    set_string(key, "ComponentId", "pci\\physical"); CHECK(!is_tap_win32_dev(guid1));
    set_string(key, "ComponentId", "tap0901");
    set_string(key, "NetCfgInstanceId", "{01234567-89ab-cdef-0123-456789abcdef}");
    CHECK(is_tap_win32_dev(guid1));
    reset();
    { WinTapEthernetHandler h(0); CHECK(h.open()); CHECK(media_up == 1);
      uint8 buffer[64] = {}; write_mode = 1; h.send(buffer, sizeof(buffer));
      CHECK(result_calls == 1); CHECK(operations.empty());
      // Only the buggy baseline leaves this request pending. Clear the mock
      // record before destruction so its known leak does not mask other checks.
      operations.clear(); h.close(); CHECK(media_down == 1); CHECK(live_handles() == 0);
    }
}
static void managed_tests() {
    for (const char *tunnel : {"", "tap0"}) {
        reset(); strcpy(bx_options.ethernet[0].tunnel, tunnel);
        Key *key = add_key(root, "SOFTWARE\\ARAnyM\\LAN"); set_string(key, "TapGuid", guid1);
        adapter("0001", guid2, "tap0901", "ARAnyM LAN");
        { WinTapEthernetHandler h(0); CHECK(h.open()); CHECK(opened_path.find(guid1) != std::string::npos); }
        CHECK((managed_access & KEY_WOW64_64KEY) != 0);
    }
    reset(); adapter("0001", guid2, "tap0901", "Custom LAN");
    Key *key = add_key(root, "SOFTWARE\\ARAnyM\\LAN"); set_string(key, "TapGuid", guid1);
    strcpy(bx_options.ethernet[0].tunnel, "Custom LAN");
    { WinTapEthernetHandler h(0); CHECK(h.open()); CHECK(opened_path.find(guid2) != std::string::npos); }
    for (const char *bad : {"bad-guid", "{21234567-89AB-CDEF-0123-456789ABCDEF}"}) {
        reset(); strcpy(bx_options.ethernet[0].tunnel, "tap0");
        key = add_key(root, "SOFTWARE\\ARAnyM\\LAN"); set_string(key, "TapGuid", bad);
        { WinTapEthernetHandler h(0); CHECK(!h.open()); CHECK(opened_path.empty()); }
    }
    reset(); strcpy(bx_options.ethernet[0].tunnel, "tap0");
    key = add_key(root, "SOFTWARE\\ARAnyM\\LAN"); set_string(key, "TapGuid", guid1);
    key = add_key(root, std::string(ADAPTER_KEY) + "\\0000"); set_string(key, "ComponentId", "tap0801");
    { WinTapEthernetHandler h(0); CHECK(!h.open()); }
    reset(); strcpy(bx_options.ethernet[0].tunnel, "");
    { WinTapEthernetHandler h(0); CHECK(!h.open()); }
    reset(); strcpy(bx_options.ethernet[0].tunnel, "tap0");
    add_key(root, "SOFTWARE\\ARAnyM\\LAN"); // retained key, no managed adapter
    key = add_key(root, std::string(NETWORK_CONNECTIONS_KEY) + "\\" + guid1 + "\\Connection");
    set_string(key, "Name", "tap0");
    { WinTapEthernetHandler h(0); CHECK(h.open()); }
    reset(); strcpy(bx_options.ethernet[0].type, "none");
    { WinTapEthernetHandler h(0); CHECK(!h.open()); CHECK(opened_path.empty()); }
}
static void io_tests() {
    reset();
    { WinTapEthernetHandler h(0); CHECK(h.open()); uint8 buffer[64] = {};
      for (int mode : {0, 1, 2, 3}) {
          write_mode = mode; CHECK(h.send(buffer, sizeof(buffer)) == (mode < 2 ? 64 : -1));
          CHECK(operations.empty());
      }
      read_mode = 1; CHECK(h.recv(buffer, sizeof(buffer)) == 64);
      read_mode = 2; CHECK(h.recv(buffer, sizeof(buffer)) == -1);
      write_mode = 1; wait_failure = true; CHECK(h.send(buffer, sizeof(buffer)) == -1);
      CHECK(cancel_calls == 1 && drain_calls == 1); CHECK(operations.empty());
      CHECK(h.send(buffer, 0) == -1); h.close(); CHECK(h.recv(buffer, 64) == -1);
    }
}
static void *reader(void *arg) {
    auto *h = static_cast<WinTapEthernetHandler *>(arg);
    uint8 buffer[64]; h->recv(buffer, sizeof(buffer)); return nullptr;
}
static void cancellation_tests() {
    for (bool modern : {false, true}) {
        reset(); use_cancel_ex = modern;
        { WinTapEthernetHandler h(0); CHECK(h.open()); read_mode = 4;
          pthread_t thread; CHECK(pthread_create(&thread, nullptr, reader, &h) == 0);
          while (!read_started.load(std::memory_order_acquire)) std::this_thread::yield();
          CHECK(pthread_cancel(thread) == 0); void *result;
          CHECK(pthread_join(thread, &result) == 0); CHECK(result == PTHREAD_CANCELED);
          CHECK(cancel_calls == 1 && drain_calls == 1); CHECK(operations.empty());
          // OS reset joins the old receiver but retains the open TAP handle.
          read_mode = 1; uint8 buffer[64]; CHECK(h.recv(buffer, sizeof(buffer)) == 64);
          h.close(); CHECK(live_handles() == 0);
        }
    }
}
int main() {
    if (getenv("TAP_TEST_CONTROL")) control_tests();
    else { registry_tests(); open_tests(); managed_tests(); io_tests(); cancellation_tests(); }
    CHECK(registry_handles == 0); CHECK(live_handles() == 0); CHECK(operations.empty());
    printf("TAP regression failures: %d\n", failures); return failures ? 1 : 0;
}
