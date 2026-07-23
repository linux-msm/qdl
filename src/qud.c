// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) Qualcomm Technologies, Inc. and/or its subsidiaries.
 *
 * QUD ("Qualcomm USB Driver") backend: talks to a Qualcomm EDL device
 * through a kernel-mode driver that exposes the device as a character
 * file, rather than via libusb. Today only the Windows path is implemented:
 * the official QDLoader 9008 driver presents the device as a serial port
 * (\\.\COMx). The Sahara/Firehose framing is identical to the libusb
 * backend; this file only deals with how user space reaches the underlying
 * bulk pipes.
 */

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
/*
 * CancelIoEx() requires Vista+; MinGW headers gate it on _WIN32_WINNT >= 0x0600.
 * Windows 7 is the de-facto floor for this project so this is safe.
 */
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#include <windows.h>
/*
 * <initguid.h> must precede any header that declares the GUIDs we reference,
 * so that GUID_DEVCLASS_PORTS gets a real definition in this TU instead of an
 * extern reference (MinGW does not ship a uuid.lib that would resolve it).
 */
#include <initguid.h>
#include <devguid.h>
#include <setupapi.h>
#include <ctype.h>
#include <limits.h>
#endif

#include "qdl.h"
#include "oscompat.h"

#ifdef _WIN32

#define QCOM_HWID_PREFIX	"USB\\VID_05C6&PID_"

/*
 * The bus-reported device description is a PnP property (DEVPKEY), not
 * an SPDRP_* registry property; older MinGW headers don't define the
 * key, so we declare it locally. GUID + PID per Microsoft docs.
 */
static const DEVPROPKEY qud_devpkey_bus_reported_device_desc = {
	{ 0x540b947e, 0x8b40, 0x45bc,
	  { 0xa8, 0xa2, 0x6a, 0x0b, 0x89, 0x4c, 0xbd, 0xa2 } },
	4
};

struct qdl_device_qud {
	struct qdl_device base;
	void *handle;	/* HANDLE; void* avoids leaking <windows.h> to consumers */
	char path[PATH_MAX];
};

/*
 * Pull the iSerial out of the SetupAPI device-instance ID. The instance ID for
 * a USB device looks like "USB\\VID_05C6&PID_9008\\<SERIAL>"; the trailing
 * component after the last backslash is the iSerial as Windows stored it.
 */
static void win_extract_serial(const char *instance_id, char *out, size_t out_size)
{
	const char *last;

	out[0] = '\0';
	last = strrchr(instance_id, '\\');
	if (!last)
		return;
	last++;
	snprintf(out, out_size, "%s", last);
}

/*
 * Read PortName ("COM5", "COM12", ...) from the device's "Device Parameters"
 * registry subkey.
 */
static bool win_read_port_name(HDEVINFO devinfo, SP_DEVINFO_DATA *info,
			       char *out, size_t out_size)
{
	DWORD type = 0;
	DWORD size = 0;
	HKEY key;
	LSTATUS s;

	key = SetupDiOpenDevRegKey(devinfo, info, DICS_FLAG_GLOBAL, 0,
				   DIREG_DEV, KEY_QUERY_VALUE);
	if (key == INVALID_HANDLE_VALUE)
		return false;

	size = (DWORD)out_size;
	s = RegQueryValueExA(key, "PortName", NULL, &type,
			     (LPBYTE)out, &size);
	RegCloseKey(key);

	if (s != ERROR_SUCCESS || type != REG_SZ)
		return false;

	if (size > 0 && out[size - 1] != '\0' && size < out_size)
		out[size] = '\0';
	return true;
}

/*
 * The EDL firmware leaves iSerial == 0 and stuffs the real serial into
 * iProduct as "..._SN:<serial>_...". Windows surfaces iProduct verbatim
 * through SPDRP_BUSREPORTEDDEVICEDESC, so we can recover the same serial
 * the libusb backend extracts in usb_list() without opening the device.
 * Returns true and writes the serial on success; false if the property
 * is missing, empty, or does not contain the _SN: token.
 */
static bool win_read_iproduct_sn(HDEVINFO devinfo, SP_DEVINFO_DATA *info,
				 char *out, size_t out_size)
{
	WCHAR wdesc[256];
	char desc[512];
	DEVPROPTYPE prop_type = 0;
	DWORD required = 0;
	const char *p;
	size_t len;
	int n;

	if (!SetupDiGetDevicePropertyW(devinfo, info,
				       &qud_devpkey_bus_reported_device_desc,
				       &prop_type, (PBYTE)wdesc, sizeof(wdesc),
				       &required, 0))
		return false;
	if (prop_type != DEVPROP_TYPE_STRING)
		return false;

	wdesc[ARRAY_SIZE(wdesc) - 1] = L'\0';

	n = WideCharToMultiByte(CP_UTF8, 0, wdesc, -1, desc, sizeof(desc),
				NULL, NULL);
	if (n <= 0)
		return false;

	p = strstr(desc, "_SN:");
	if (!p)
		return false;
	p += 4;
	len = strcspn(p, " _");
	if (len == 0 || len + 1 > out_size)
		return false;

	memcpy(out, p, len);
	out[len] = '\0';
	return true;
}

/*
 * Parse the 4-digit hex PID immediately following the QCOM_HWID_PREFIX
 * in a HARDWAREID string. Returns 0 if no valid PID is found.
 */
static unsigned int win_parse_pid(const char *hwid)
{
	const char *p = hwid + sizeof(QCOM_HWID_PREFIX) - 1;
	unsigned int pid = 0;
	int i;

	for (i = 0; i < 4; i++) {
		char c = p[i];
		unsigned int digit;

		if (c >= '0' && c <= '9')
			digit = (unsigned int)(c - '0');
		else if (c >= 'a' && c <= 'f')
			digit = (unsigned int)(c - 'a') + 10;
		else if (c >= 'A' && c <= 'F')
			digit = (unsigned int)(c - 'A') + 10;
		else
			return 0;
		pid = (pid << 4) | digit;
	}
	return pid;
}

/*
 * Windows encodes each USB interface's class/subclass/protocol triple
 * in the devnode's compatible ids as "USB\Class_xx&SubClass_xx&Prot_xx".
 * Match the same EDL interface signature as the libusb backend
 * (usb_match_edl_interface() in usb.c): vendor-specific class and
 * subclass with a known Sahara protocol - 0x10, 0x11 or 0x13 on modern
 * devices, 0xff on older targets.
 */
static bool win_match_edl_interface(HDEVINFO devinfo, SP_DEVINFO_DATA *info)
{
	static const char * const edl_ids[] = {
		"USB\\Class_FF&SubClass_FF&Prot_10",
		"USB\\Class_FF&SubClass_FF&Prot_11",
		"USB\\Class_FF&SubClass_FF&Prot_13",
		"USB\\Class_FF&SubClass_FF&Prot_FF",
	};
	char ids[1024];
	DWORD type = 0;
	const char *p;
	size_t i;

	if (!SetupDiGetDeviceRegistryPropertyA(devinfo, info,
					       SPDRP_COMPATIBLEIDS, &type,
					       (BYTE *)ids, sizeof(ids), NULL))
		return false;
	if (type != REG_MULTI_SZ)
		return false;

	/* REG_MULTI_SZ: walk the NUL-separated entries */
	for (p = ids; *p; p += strlen(p) + 1) {
		for (i = 0; i < ARRAY_SIZE(edl_ids); i++) {
			if (!_stricmp(p, edl_ids[i]))
				return true;
		}
	}

	return false;
}

static int win_enumerate_qcom(struct qud_device_desc *out, size_t out_max)
{
	HDEVINFO devinfo;
	SP_DEVINFO_DATA info = { .cbSize = sizeof(info) };
	char hwid[256];
	char instance_id[256];
	char port_name[32];
	size_t count = 0;
	DWORD i;

	devinfo = SetupDiGetClassDevsA(&GUID_DEVCLASS_PORTS, NULL, NULL,
				       DIGCF_PRESENT);
	if (devinfo == INVALID_HANDLE_VALUE)
		return -1;

	for (i = 0; SetupDiEnumDeviceInfo(devinfo, i, &info); i++) {
		unsigned int pid;
		DWORD type = 0;
		DWORD size = 0;

		if (!SetupDiGetDeviceRegistryPropertyA(devinfo, &info,
						       SPDRP_HARDWAREID, &type,
						       (BYTE *)hwid, sizeof(hwid),
						       &size))
			continue;

		/* HARDWAREID is a REG_MULTI_SZ; first entry is what we want. */
		if (strncmp(hwid, QCOM_HWID_PREFIX,
			    sizeof(QCOM_HWID_PREFIX) - 1) != 0)
			continue;

		/*
		 * The prefix match already guarantees Qualcomm's vid; require
		 * the same EDL interface signature the libusb backend
		 * matches, so both backends filter other Qualcomm COM ports
		 * the same way.
		 */
		if (!win_match_edl_interface(devinfo, &info))
			continue;

		pid = win_parse_pid(hwid);

		if (!SetupDiGetDeviceInstanceIdA(devinfo, &info, instance_id,
						 sizeof(instance_id), NULL))
			continue;

		if (!win_read_port_name(devinfo, &info, port_name,
					sizeof(port_name)))
			continue;

		if (count < out_max) {
			out[count].pid = pid;
			if (!win_read_iproduct_sn(devinfo, &info,
						  out[count].serial,
						  sizeof(out[count].serial)))
				win_extract_serial(instance_id, out[count].serial,
						   sizeof(out[count].serial));
			snprintf(out[count].path, sizeof(out[count].path),
				 "\\\\.\\%s", port_name);
		}
		count++;
	}

	SetupDiDestroyDeviceInfoList(devinfo);
	return (int)count;
}

static int win_configure_handle(HANDLE h)
{
	COMMTIMEOUTS to = { 0 };
	DCB dcb = { .DCBlength = sizeof(dcb) };

	/*
	 * The QDLoader 9008 driver presents the device as a serial port, but
	 * line-discipline parameters (baud, parity, stop bits) are irrelevant
	 * over USB-CDC. Some driver builds nevertheless reject I/O until DCB
	 * has been initialised, so we set sane defaults and never touch them
	 * again.
	 */
	if (!GetCommState(h, &dcb))
		return -1;
	dcb.BaudRate = 115200;
	dcb.ByteSize = 8;
	dcb.Parity   = NOPARITY;
	dcb.StopBits = ONESTOPBIT;
	dcb.fBinary  = TRUE;
	dcb.fNull    = FALSE;
	dcb.fAbortOnError = FALSE;
	/*
	 * Force-disable hardware/software flow control: some QDLoader driver
	 * builds gate bulk-IN delivery on these handshake lines, which would
	 * stall reads even though USB-CDC has no real RS-232 signals.
	 */
	dcb.fOutxCtsFlow = FALSE;
	dcb.fOutxDsrFlow = FALSE;
	dcb.fDsrSensitivity = FALSE;
	dcb.fOutX = FALSE;
	dcb.fInX = FALSE;
	dcb.fDtrControl = DTR_CONTROL_ENABLE;
	dcb.fRtsControl = RTS_CONTROL_ENABLE;
	if (!SetCommState(h, &dcb))
		return -1;

	/*
	 * Mirror libusb bulk-read semantics: ReadFile returns as soon as any
	 * data is available, or after ReadTotalTimeoutConstant ms with zero
	 * bytes if the link is idle (MSDN COMMTIMEOUTS, "MAXDWORD interval +
	 * MAXDWORD multiplier" case). qud_read() rewrites the constant
	 * per call; this just seeds a sane default for writes and any read
	 * issued before that.
	 */
	to.ReadIntervalTimeout = MAXDWORD;
	to.ReadTotalTimeoutMultiplier = MAXDWORD;
	to.ReadTotalTimeoutConstant = 1000;
	to.WriteTotalTimeoutMultiplier = 0;
	to.WriteTotalTimeoutConstant = 0;
	if (!SetCommTimeouts(h, &to))
		return -1;

	SetupComm(h, 64 * 1024, 64 * 1024);
	/*
	 * Only purge the TX side. The QDLoader driver claims the USB
	 * interface during PnP enumeration, well before CreateFile() runs,
	 * so the device's Sahara HELLO is typically already sitting in the
	 * driver's RX FIFO by the time we get here. Purging RX would
	 * discard it, and the device only sends HELLO once.
	 */
	PurgeComm(h, PURGE_TXCLEAR | PURGE_TXABORT);
	return 0;
}

static int qud_open(struct qdl_device *qdl, const char *serial)
{
	struct qdl_device_qud *qd = container_of(qdl,
						 struct qdl_device_qud,
						 base);
	struct qud_device_desc matches[16];
	const char *path = NULL;
	HANDLE h;
	int found;
	int i;

	/* Allow an explicit \\.\COMx (or COMx) path via --serial. */
	if (serial && (serial[0] == '\\' ||
		       (strlen(serial) >= 3 &&
			(serial[0] == 'C' || serial[0] == 'c') &&
			(serial[1] == 'O' || serial[1] == 'o') &&
			(serial[2] == 'M' || serial[2] == 'm')))) {
		path = serial;
	} else {
		found = win_enumerate_qcom(matches, ARRAY_SIZE(matches));
		if (found < 0) {
			ux_err("qud: SetupDiGetClassDevs failed (error %lu)\n",
			       (unsigned long)GetLastError());
			return -1;
		}
		if (found == 0) {
			ux_err("qud: no Qualcomm COM ports found\n");
			return -1;
		}

		for (i = 0; i < found && i < (int)ARRAY_SIZE(matches); i++) {
			if (!serial || serial[0] == '\0' ||
			    _stricmp(matches[i].serial, serial) == 0) {
				path = matches[i].path;
				break;
			}
		}

		if (!path) {
			ux_err("qud: no Qualcomm device matching serial \"%s\"\n",
			       serial);
			return -1;
		}
	}

	h = CreateFileA(path, GENERIC_READ | GENERIC_WRITE, 0, NULL,
			OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);
	if (h == INVALID_HANDLE_VALUE) {
		ux_err("qud: CreateFile(%s) failed (error %lu)\n",
		       path, (unsigned long)GetLastError());
		return -1;
	}

	if (win_configure_handle(h) < 0) {
		ux_err("qud: failed to configure %s (error %lu)\n",
		       path, (unsigned long)GetLastError());
		CloseHandle(h);
		return -1;
	}

	qd->handle = h;
	snprintf(qd->path, sizeof(qd->path), "%s", path);

	ux_debug("qud: opened %s\n", path);
	return 0;
}

static int win_overlapped_io(HANDLE h, void *buf, size_t len,
			     unsigned int timeout_ms, bool is_write)
{
	OVERLAPPED ovl = { 0 };
	HANDLE ev;
	DWORD wait;
	DWORD done = 0;
	BOOL ok;

	ev = CreateEvent(NULL, TRUE, FALSE, NULL);
	if (!ev)
		return -EIO;
	ovl.hEvent = ev;

	if (is_write)
		ok = WriteFile(h, buf, (DWORD)len, NULL, &ovl);
	else
		ok = ReadFile(h, buf, (DWORD)len, NULL, &ovl);

	if (!ok && GetLastError() != ERROR_IO_PENDING) {
		CloseHandle(ev);
		return -EIO;
	}

	wait = WaitForSingleObject(ev, timeout_ms);
	if (wait == WAIT_TIMEOUT) {
		CancelIoEx(h, &ovl);
		/* Drain the cancellation so the OVERLAPPED isn't reused live. */
		GetOverlappedResult(h, &ovl, &done, TRUE);
		CloseHandle(ev);
		return -ETIMEDOUT;
	}
	if (wait != WAIT_OBJECT_0) {
		CloseHandle(ev);
		return -EIO;
	}

	if (!GetOverlappedResult(h, &ovl, &done, FALSE)) {
		CloseHandle(ev);
		return -EIO;
	}

	CloseHandle(ev);
	return (int)done;
}

static int qud_read(struct qdl_device *qdl, void *buf, size_t len,
		    unsigned int timeout)
{
	struct qdl_device_qud *qd = container_of(qdl,
						 struct qdl_device_qud,
						 base);
	HANDLE h = (HANDLE)qd->handle;
	COMMTIMEOUTS to = {
		.ReadIntervalTimeout = MAXDWORD,
		.ReadTotalTimeoutMultiplier = MAXDWORD,
		.ReadTotalTimeoutConstant = timeout ? timeout : 1,
		.WriteTotalTimeoutMultiplier = 0,
		.WriteTotalTimeoutConstant = 0,
	};
	int ret;

	/*
	 * The COM driver itself enforces the per-call timeout via
	 * COMMTIMEOUTS; the WaitForSingleObject() inside win_overlapped_io()
	 * is a backstop with a small grace window in case the driver
	 * completes a hair after the constant.
	 */
	if (!SetCommTimeouts(h, &to))
		return -EIO;

	ret = win_overlapped_io(h, buf, len, timeout + 250, false);
	if (ret == 0)
		return -ETIMEDOUT;
	return ret;
}

static int qud_write(struct qdl_device *qdl, const void *buf, size_t len,
		     unsigned int timeout)
{
	struct qdl_device_qud *qd = container_of(qdl,
						 struct qdl_device_qud,
						 base);

	return win_overlapped_io((HANDLE)qd->handle, (void *)buf, len, timeout,
				 true);
}

static void qud_close(struct qdl_device *qdl)
{
	struct qdl_device_qud *qd = container_of(qdl,
						 struct qdl_device_qud,
						 base);
	HANDLE h = (HANDLE)qd->handle;

	if (h && h != INVALID_HANDLE_VALUE) {
		CloseHandle(h);
		qd->handle = NULL;
	}
}

static void qud_set_out_chunk_size(struct qdl_device *qdl __unused,
				   long size __unused)
{
	/* The kernel-mode driver handles bulk-transfer chunking. */
}

struct qdl_device *qud_init(void)
{
	struct qdl_device_qud *qd = calloc(1, sizeof(*qd));

	if (!qd)
		return NULL;

	qd->handle = NULL;
	qd->base.dev_type = QDL_DEVICE_QUD;
	qd->base.open = qud_open;
	qd->base.read = qud_read;
	qd->base.write = qud_write;
	qd->base.close = qud_close;
	qd->base.set_out_chunk_size = qud_set_out_chunk_size;
	qd->base.max_payload_size = 1048576;

	return &qd->base;
}

/*
 * qud_probe_present() - count Qualcomm COM ports exposed by the QDLoader
 * 9008 driver. Used by the auto-backend selector on Windows to decide
 * whether the QUD backend can take over when libusb cannot open the
 * device.
 */
int qud_probe_present(void)
{
	struct qud_device_desc matches[16];
	int found;

	found = win_enumerate_qcom(matches, ARRAY_SIZE(matches));
	return found > 0 ? found : 0;
}

/*
 * Enumerate Qualcomm COM ports the QUD backend can drive, returning a
 * heap-allocated array of qud_device_desc and writing the count to
 * *devices_found. Caller frees with free(). Returns NULL on error or
 * when no devices are present (count is still written).
 */
struct qud_device_desc *qud_list(unsigned int *devices_found)
{
	struct qud_device_desc *result;
	int found;

	*devices_found = 0;

	result = calloc(16, sizeof(*result));
	if (!result)
		return NULL;

	found = win_enumerate_qcom(result, 16);
	if (found <= 0) {
		free(result);
		return NULL;
	}

	if (found > 16) {
		ux_err("qud: %d devices present, only the first 16 will be listed\n",
		       found);
		found = 16;
	}

	*devices_found = (unsigned int)found;
	return result;
}

#else /* unsupported platform */

struct qdl_device *qud_init(void)
{
	ux_err("qud backend is not supported on this platform\n");
	return NULL;
}

int qud_probe_present(void)
{
	return 0;
}

struct qud_device_desc *qud_list(unsigned int *devices_found)
{
	*devices_found = 0;
	return NULL;
}

#endif
