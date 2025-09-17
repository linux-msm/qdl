// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2025, Qualcomm Innovation Center, Inc. All rights reserved.
 */
#include <stdlib.h>

#include "qdl.h"

struct qdl_device *qdl_init(enum QDL_DEVICE_TYPE type)
{
	if (type == QDL_DEVICE_USB)
		return usb_init();

	if (type == QDL_DEVICE_SIM)
		return sim_init();

	return NULL;
}

void qdl_deinit(struct qdl_device *qdl)
{
	if (qdl)
		free(qdl);
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
 * Returns: number of bytes read, might be zero for a ZLP
 *	    negative errno on failure (notably -ETIMEDOUT)
 */
int qdl_read(struct qdl_device *qdl, void *buf, size_t len, unsigned int timeout)
{
	return qdl->read(qdl, buf, len, timeout);
}

/**
 * qdl_write() - Write a message from the device
 * @qdl: device handle
 * @buf: buffer with data to be written
 * @len: length of data to be written
 *
 * Returns: number of bytes read
 *	    negative errno on failure (notably -ETIMEDOUT)
 */
int qdl_write(struct qdl_device *qdl, const void *buf, size_t len)
{
	return qdl->write(qdl, buf, len);
}
