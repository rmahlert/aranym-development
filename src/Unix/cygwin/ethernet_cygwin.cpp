/*
    device.c -- Interaction with Windows tap driver in a Cygwin environment
    Copyright (C) 2002-2004 Ivo Timmermans <ivo@tinc-vpn.org>,
                  2002-2004 Guus Sliepen <guus@tinc-vpn.org>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

#include "cpu_emulation.h"
#include "main.h"
#include "ethernet_cygwin.h"

#define DEBUG 0
#include "debug.h"

#include <winioctl.h>

#ifdef HAVE_SYS_SOCKET_H
#include <sys/socket.h>
#endif

#include <errno.h>
#include <ctype.h>

#define MTU 1500

//=============
// TAP IOCTLs
//=============

#define TAP_CONTROL_CODE(request,method) \
  CTL_CODE (FILE_DEVICE_UNKNOWN, request, method, FILE_ANY_ACCESS)

#define TAP_IOCTL_GET_MAC               TAP_CONTROL_CODE (1, METHOD_BUFFERED)
#define TAP_IOCTL_GET_VERSION           TAP_CONTROL_CODE (2, METHOD_BUFFERED)
#define TAP_IOCTL_GET_MTU               TAP_CONTROL_CODE (3, METHOD_BUFFERED)
#define TAP_IOCTL_GET_INFO              TAP_CONTROL_CODE (4, METHOD_BUFFERED)
#define TAP_IOCTL_CONFIG_POINT_TO_POINT TAP_CONTROL_CODE (5, METHOD_BUFFERED)
#define TAP_IOCTL_SET_MEDIA_STATUS      TAP_CONTROL_CODE (6, METHOD_BUFFERED)
#define TAP_IOCTL_CONFIG_DHCP_MASQ      TAP_CONTROL_CODE (7, METHOD_BUFFERED)
#define TAP_IOCTL_GET_LOG_LINE          TAP_CONTROL_CODE (8, METHOD_BUFFERED)
#define TAP_IOCTL_CONFIG_DHCP_SET_OPT   TAP_CONTROL_CODE (9, METHOD_BUFFERED)

//=================
// Registry keys
//=================

#define ADAPTER_KEY "SYSTEM\\CurrentControlSet\\Control\\Class\\{4D36E972-E325-11CE-BFC1-08002BE10318}"

#define NETWORK_CONNECTIONS_KEY "SYSTEM\\CurrentControlSet\\Control\\Network\\{4D36E972-E325-11CE-BFC1-08002BE10318}"

//======================
// Filesystem prefixes
//======================

#define USERMODEDEVICEDIR "\\\\.\\Global\\"
#define SYSDEVICEDIR      "\\Device\\"
#define USERDEVICEDIR     "\\DosDevices\\Global\\"
#define TAPSUFFIX         ".tap"

//=========================================================
// TAP_COMPONENT_ID -- This string defines the TAP driver
// type -- different component IDs can reside in the system
// simultaneously.
//=========================================================

// Only adapters using the TAP-Windows Ethernet ABI are supported.
static bool known_tap_component(const char *id)
{
	return !strcmp(id, "tap0801") || !strcmp(id, "tap0901");
}

static char *winerror(int err) {
	static char buf[1024], *newline;

	if (!FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
	        NULL, err, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), buf, sizeof(buf), NULL)) {
		strncpy(buf, "(unable to format errormessage)", sizeof(buf));
	}

	if((newline = strchr(buf, '\r')))
		*newline = '\0';

	return buf;
}


WinTapEthernetHandler::WinTapEthernetHandler(int eth_idx)
	: ETHERNETDriver::Handler(eth_idx)
{
	device_handle = INVALID_HANDLE_VALUE;
	device = NULL;
	memset(&read_overlapped, 0, sizeof(read_overlapped));
	memset(&write_overlapped, 0, sizeof(write_overlapped));
	media_connected = false;
	read_pending = write_pending = false;

	device_total_in = 0;
	device_total_out = 0;
}

WinTapEthernetHandler::~WinTapEthernetHandler()
{
	close();
}

// RegQueryValueEx does not promise a terminating NUL, even for REG_SZ.
static bool registry_string(HKEY key, const char *value, char *text, DWORD capacity, LONG *result = NULL)
{
	DWORD type = 0, length = capacity - 1;
	LONG status = RegQueryValueExA(key, value, NULL, &type, (BYTE *)text, &length);
	if (result) *result = status;
	if (status != ERROR_SUCCESS || type != REG_SZ || !length || length >= capacity)
		return false;
	text[length] = '\0';
	const char *end = (const char *)memchr(text, '\0', length);
	return !end || end == text + length - 1;
}

static bool valid_guid(const char *guid)
{
	if (!guid || strlen(guid) != 38 || guid[0] != '{' || guid[37] != '}')
		return false;
	for (int i = 1; i < 37; ++i) {
		if (i == 9 || i == 14 || i == 19 || i == 24) {
			if (guid[i] != '-') return false;
		} else if (!isxdigit((unsigned char)guid[i])) {
			return false;
		}
	}
	return true;
}

bool is_tap_win32_dev(const char *guid, bool modern_only = false)
{
	if (!valid_guid(guid)) return false;
	HKEY adapters;
	if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, ADAPTER_KEY, 0, KEY_READ, &adapters) != ERROR_SUCCESS)
		return false;
	bool found = false;
	for (DWORD i = 0; ; ++i) {
		char subkey[256];
		DWORD length = sizeof(subkey);
		LONG status = RegEnumKeyExA(adapters, i, subkey, &length, NULL, NULL, NULL, NULL);
		if (status == ERROR_NO_MORE_ITEMS) break;
		if (status != ERROR_SUCCESS) break;
		HKEY adapter;
		if (RegOpenKeyExA(adapters, subkey, 0, KEY_READ, &adapter) != ERROR_SUCCESS)
			continue;
		char component[256], instance[256];
		found = registry_string(adapter, "ComponentId", component, sizeof(component))
			&& (modern_only ? !strcmp(component, "tap0901") : known_tap_component(component))
			&& registry_string(adapter, "NetCfgInstanceId", instance, sizeof(instance))
			&& valid_guid(instance) && !strcasecmp(instance, guid);
		RegCloseKey(adapter);
		if (found) break;
	}
	RegCloseKey(adapters);
	return found;
}

int get_device_guid(char *name, int name_size, char *actual_name, int actual_name_size)
{
	if (!name || name_size < 39 || (actual_name && actual_name_size < 1)) {
		SetLastError(ERROR_INVALID_PARAMETER);
		return -1;
	}
	HKEY connections;
	LONG opened = RegOpenKeyExA(HKEY_LOCAL_MACHINE, NETWORK_CONNECTIONS_KEY, 0, KEY_READ, &connections);
	if (opened != ERROR_SUCCESS) { SetLastError(opened); return -1; }
	bool found = false, failed = false;
	char selected_guid[39], selected_name[256];
	for (DWORD i = 0; ; ++i) {
		char guid[256];
		DWORD length = sizeof(guid);
		LONG status = RegEnumKeyExA(connections, i, guid, &length, NULL, NULL, NULL, NULL);
		if (status == ERROR_NO_MORE_ITEMS) break;
		if (status != ERROR_SUCCESS) { failed = true; break; }
		if (!valid_guid(guid)) continue;
		char path[sizeof(guid) + 12];
		snprintf(path, sizeof(path), "%s\\Connection", guid);
		HKEY connection;
		if (RegOpenKeyExA(connections, path, 0, KEY_READ, &connection) != ERROR_SUCCESS)
			continue;
		char friendly[256];
		bool usable = registry_string(connection, "Name", friendly, sizeof(friendly));
		RegCloseKey(connection);
		if (!usable || !is_tap_win32_dev(guid)) continue;
		if (actual_name && *actual_name) {
			bool matches = valid_guid(actual_name) ? !strcasecmp(guid, actual_name)
				: !strcmp(friendly, actual_name);
			if (!matches) continue;
		}
		// Never choose an arbitrary adapter when a name matches more than one.
		if (found) { failed = true; break; }
		strcpy(selected_guid, guid);
		strcpy(selected_name, friendly);
		found = true;
	}
	RegCloseKey(connections);
	if (!found || failed || (actual_name && !*actual_name
		&& strlen(selected_name) >= (size_t)actual_name_size)) {
		SetLastError(failed ? ERROR_INVALID_DATA : ERROR_FILE_NOT_FOUND);
		return -1;
	}
	strcpy(name, selected_guid);
	if (actual_name && !*actual_name) strcpy(actual_name, selected_name);
	return 0;
}

// The installer writes this GUID in the 64-bit view even for 32-bit ARAnyM.
// Missing setup preserves legacy configuration; stale setup must not select a
// different adapter just because it has the same display name.
static int managed_tap_guid(char *guid, DWORD capacity)
{
	HKEY key;
	LONG status = RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\ARAnyM\\LAN", 0,
		KEY_READ | KEY_WOW64_64KEY, &key);
	if (status == ERROR_FILE_NOT_FOUND) return 0;
	if (status != ERROR_SUCCESS) return -1;
	bool valid = registry_string(key, "TapGuid", guid, capacity, &status)
		&& valid_guid(guid) && is_tap_win32_dev(guid, true);
	RegCloseKey(key);
	if (status == ERROR_FILE_NOT_FOUND) return 0;
	return valid ? 1 : -1;
}

// CancelIo only cancels requests issued by this thread. The receive-thread
// cleanup below provides that fallback on XP; CancelIoEx also covers close().
static void cancel_io(HANDLE handle, OVERLAPPED *overlapped)
{
	typedef BOOL (WINAPI *CancelIoExFn)(HANDLE, LPOVERLAPPED);
	FARPROC address = GetProcAddress(GetModuleHandleA("kernel32.dll"), "CancelIoEx");
	CancelIoExFn cancel;
	static_assert(sizeof(cancel) == sizeof(address), "Windows procedure pointer size");
	memcpy(&cancel, &address, sizeof(cancel));
	if (cancel) cancel(handle, overlapped);
	else CancelIo(handle);
}

struct PendingTapIo {
	HANDLE handle;
	OVERLAPPED *overlapped;
	bool *pending;
};

static void cancel_pending_io(void *argument)
{
	PendingTapIo *io = (PendingTapIo *)argument;
	if (*io->pending) {
		cancel_io(io->handle, io->overlapped);
		DWORD transferred;
		// Cancellation is asynchronous: keep OVERLAPPED, event and buffer alive
		// until the driver has completed the request, including on reset.
		GetOverlappedResult(io->handle, io->overlapped, &transferred, TRUE);
		*io->pending = false;
	}
}

static bool finish_io(HANDLE handle, OVERLAPPED *overlapped, bool *pending, DWORD *transferred)
{
	PendingTapIo io = { handle, overlapped, pending };
	bool success = false;
	DWORD error = ERROR_SUCCESS;
	pthread_cleanup_push(cancel_pending_io, &io);
	for (;;) {
		// Native Windows waits are not pthread cancellation points on Cygwin.
		pthread_testcancel();
		DWORD status = WaitForSingleObject(overlapped->hEvent, 100);
		if (status == WAIT_TIMEOUT) continue;
		if (status != WAIT_OBJECT_0) { error = GetLastError(); break; }
		success = GetOverlappedResult(handle, overlapped, transferred, FALSE) != FALSE;
		error = success ? ERROR_SUCCESS : GetLastError();
		if (error != ERROR_IO_INCOMPLETE) *pending = false;
		break;
	}
	pthread_cleanup_pop(1);
	SetLastError(error);
	return success;
}

// The device is opened overlapped, so IOCTLs also need a valid OVERLAPPED.
static bool tap_ioctl(HANDLE handle, DWORD request, void *data, DWORD size, DWORD *returned)
{
	OVERLAPPED overlapped;
	memset(&overlapped, 0, sizeof(overlapped));
	overlapped.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
	if (!overlapped.hEvent) return false;
	BOOL success = DeviceIoControl(handle, request, data, size, data, size, returned, &overlapped);
	if (!success && GetLastError() == ERROR_IO_PENDING)
		success = GetOverlappedResult(handle, &overlapped, returned, TRUE);
	DWORD error = success ? ERROR_SUCCESS : GetLastError();
	CloseHandle(overlapped.hEvent);
	SetLastError(error);
	return success != FALSE;
}

bool WinTapEthernetHandler::open()
{
	char *type = bx_options.ethernet[ethX].type;
	char device_path[256 + sizeof(USERMODEDEVICEDIR) + sizeof(TAPSUFFIX) + 1];
	char device_guid[256];
	char name_buffer[256];

	close();

	if (strcmp(type, "none") == 0 || strlen(type) == 0)
	{
		return false;
	}
	debug = strstr(type, "debug") != NULL;

	const char *tunnel = bx_options.ethernet[ethX].tunnel;
	int managed = 0;
	if (!*tunnel || !strcmp(tunnel, "tap0")) {
		managed = managed_tap_guid(device_guid, sizeof(device_guid));
		if (managed < 0) {
			panicbug("ETH%d: ERROR: Managed ARAnyM TAP is missing or invalid; run Atari TAP Setup", ethX);
			return false;
		}
	}
	if (!managed) {
		if (!*tunnel) return false;
		safe_strncpy(name_buffer, tunnel, sizeof(name_buffer));
		if (get_device_guid(device_guid, sizeof(device_guid), name_buffer, sizeof(name_buffer)) < 0) {
			panicbug("ETH%d: ERROR: Could not find Windows tap device: %s", ethX, winerror(GetLastError()));
			return false;
		}
	}

	/*
	 * Open Windows TAP-Win32 adapter
	 */
	snprintf (device_path, sizeof(device_path), "%s%s%s",
		  USERMODEDEVICEDIR,
		  device_guid,
		  TAPSUFFIX);

	device_handle = CreateFile(device_path, GENERIC_READ | GENERIC_WRITE, 0, 0, OPEN_EXISTING, FILE_ATTRIBUTE_SYSTEM | FILE_FLAG_OVERLAPPED, 0);

	if (device_handle == INVALID_HANDLE_VALUE) {
		panicbug("ETH%d: ERROR: Could not open (%s) Windows tap device: %s", ethX, device_path, winerror(GetLastError()));
		return false;
	}
	read_overlapped.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
	write_overlapped.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
	ULONG version[3] = { 0, 0, 0 };
	DWORD returned = 0;
	if (!read_overlapped.hEvent || !write_overlapped.hEvent
		|| !tap_ioctl(device_handle, TAP_IOCTL_GET_VERSION, version, sizeof(version), &returned)
		|| returned != sizeof(version)
		|| !((version[0] == 8 && version[1] >= 1) || version[0] == 9)) {
		panicbug("ETH%d: ERROR: TAP requires a supported 8.1 or 9.x driver", ethX);
		close();
		return false;
	}
	ULONG connected = 1;
	media_connected = true;
	if (!tap_ioctl(device_handle, TAP_IOCTL_SET_MEDIA_STATUS, &connected, sizeof(connected), &returned)) {
		panicbug("ETH%d: ERROR: Could not connect TAP media: %s", ethX, winerror(GetLastError()));
		close();
		return false;
	}
	device = strdup(device_path);
	if (!device) { close(); return false; }
	D(bug("ETH%d: TAP %lu.%lu opened: %s", ethX, version[0], version[1], device));
	return true;
}

void WinTapEthernetHandler::close()
{
	// ETHERNETDriver stops and joins the receiver before close/destruction.
	// Its cancellation cleanup has drained any pending read before we get here.
	if (device_handle != INVALID_HANDLE_VALUE) {
		ULONG connected = 0;
		DWORD returned;
		if (media_connected)
			tap_ioctl(device_handle, TAP_IOCTL_SET_MEDIA_STATUS, &connected, sizeof(connected), &returned);
		PendingTapIo read = { device_handle, &read_overlapped, &read_pending };
		PendingTapIo write = { device_handle, &write_overlapped, &write_pending };
		cancel_pending_io(&read);
		cancel_pending_io(&write);
		CloseHandle(device_handle);
		device_handle = INVALID_HANDLE_VALUE;
	}
	if (read_overlapped.hEvent) CloseHandle(read_overlapped.hEvent);
	if (write_overlapped.hEvent) CloseHandle(write_overlapped.hEvent);
	memset(&read_overlapped, 0, sizeof(read_overlapped));
	memset(&write_overlapped, 0, sizeof(write_overlapped));
	media_connected = false;
	read_pending = write_pending = false;
	free(device);
	device = NULL;
}

int WinTapEthernetHandler::recv(uint8 *buf, int len)
{
	if (device_handle == INVALID_HANDLE_VALUE || !buf || len <= 0) return -1;
	DWORD transferred = 0;
	if (!ResetEvent(read_overlapped.hEvent)) return -1;
	if (!ReadFile(device_handle, buf, len, &transferred, &read_overlapped)) {
		if (GetLastError() != ERROR_IO_PENDING) return -1;
		read_pending = true;
		if (!finish_io(device_handle, &read_overlapped, &read_pending, &transferred)) return -1;
	}
	if (transferred > (DWORD)len) return -1;
	device_total_in += transferred;
	return (int)transferred;
}

int WinTapEthernetHandler::send(const uint8 *buf, int len)
{
	if (device_handle == INVALID_HANDLE_VALUE || !buf || len <= 0) return -1;
	DWORD transferred = 0;
	if (!ResetEvent(write_overlapped.hEvent)) return -1;
	if (!WriteFile(device_handle, buf, len, &transferred, &write_overlapped)) {
		if (GetLastError() != ERROR_IO_PENDING) return -1;
		write_pending = true;
		if (!finish_io(device_handle, &write_overlapped, &write_pending, &transferred)) return -1;
	}
	// A short Ethernet write is not a successfully transmitted packet.
	if (transferred != (DWORD)len) return -1;
	device_total_out += transferred;
	return (int)transferred;
}
