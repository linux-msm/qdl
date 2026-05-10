// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2025, Qualcomm Innovation Center, Inc. All rights reserved.
 */
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#include "qdl.h"

struct qdl_device *qdl_init(enum QDL_DEVICE_TYPE type)
{
	if (type == QDL_DEVICE_USB)
		return usb_init();

	if (type == QDL_DEVICE_SIM)
		return sim_init();

	if (type == QDL_DEVICE_QUD)
		return qud_init();

	return NULL;
}

void qdl_deinit(struct qdl_device *qdl)
{
	if (qdl) {
		free(qdl->pending_buf);
		free(qdl);
	}
}

void qdl_set_out_chunk_size(struct qdl_device *qdl, long size)
{
	qdl->set_out_chunk_size(qdl, size);
}

int qdl_open(struct qdl_device *qdl, const char *serial)
{
	return qdl->open(qdl, serial);
}

void qdl_close(struct qdl_device *qdl)
{
	qdl->close(qdl);
}

/**
 * qdl_read() - Read a message from the device
 * @qdl: device handle
 * @buf: buffer to write the data into
 * @len: maximum length of data to be read
 * @timeout: timeout for the read, in milliseconds
 *
 * Drains the pushback buffer (qdl_push_back()) before touching the underlying
 * transport, so a previous read that crossed a Firehose message boundary can
 * deliver the trailing bytes here.
 *
 * Returns: number of bytes read, might be zero for a ZLP
 *	    negative errno on failure (notably -ETIMEDOUT)
 */
int qdl_read(struct qdl_device *qdl, void *buf, size_t len, unsigned int timeout)
{
	size_t available;
	size_t copy;

	if (qdl->pending_buf) {
		available = qdl->pending_len - qdl->pending_off;
		copy = available < len ? available : len;
		memcpy(buf, qdl->pending_buf + qdl->pending_off, copy);
		qdl->pending_off += copy;
		if (qdl->pending_off >= qdl->pending_len) {
			free(qdl->pending_buf);
			qdl->pending_buf = NULL;
			qdl->pending_len = 0;
			qdl->pending_off = 0;
		}
		return (int)copy;
	}

	return qdl->read(qdl, buf, len, timeout);
}

/**
 * qdl_push_back() - Stash unread bytes for a future qdl_read()
 * @qdl: device handle
 * @buf: bytes to remember
 * @len: number of bytes
 *
 * Concatenates @buf onto whatever is already pending (after any already-served
 * prefix). Used by firehose_read() when a single transport read returned more
 * than one Firehose message - or an XML envelope followed immediately by its
 * rawmode binary payload - and we need the trailing bytes to surface on the
 * next qdl_read() before any new transport I/O happens.
 *
 * Returns: 0 on success, negative errno on allocation failure.
 */
int qdl_push_back(struct qdl_device *qdl, const void *buf, size_t len)
{
	char *grown;
	size_t total;

	if (!len)
		return 0;

	if (qdl->pending_off > 0) {
		size_t leftover = qdl->pending_len - qdl->pending_off;

		memmove(qdl->pending_buf, qdl->pending_buf + qdl->pending_off,
			leftover);
		qdl->pending_len = leftover;
		qdl->pending_off = 0;
	}

	total = qdl->pending_len + len;
	grown = realloc(qdl->pending_buf, total);
	if (!grown)
		return -ENOMEM;

	memcpy(grown + qdl->pending_len, buf, len);
	qdl->pending_buf = grown;
	qdl->pending_len = total;
	return 0;
}

/**
 * qdl_write() - Write a message from the device
 * @qdl: device handle
 * @buf: buffer with data to be written
 * @len: length of data to be written
 * @timeout: timeout for write, in milliseconds
 *
 * Returns: number of bytes read
 *	    negative errno on failure (notably -ETIMEDOUT)
 */
int qdl_write(struct qdl_device *qdl, const void *buf, size_t len, unsigned int timeout)
{
	return qdl->write(qdl, buf, len, timeout);
}
