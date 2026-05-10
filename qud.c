// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2026, Qualcomm Innovation Center, Inc. All rights reserved.
 *
 * QUD ("Qualcomm USB Driver") backend skeleton.
 *
 * This is a transport that talks to a Qualcomm EDL device through a
 * kernel-mode driver presenting it as a character file (e.g. \\.\COMx
 * on Windows via the official QDLoader 9008 driver), rather than via
 * libusb. No platform implementation lives here yet -- qud_init() returns
 * NULL on every host, signalling that --backend=qud is recognised but not
 * usable until a follow-up patch wires up an actual transport.
 */

#include <stdlib.h>

#include "qdl.h"

struct qdl_device *qud_init(void)
{
	ux_err("qud backend is not supported on this platform\n");
	return NULL;
}
