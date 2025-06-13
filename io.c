// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2025, Qualcomm Innovation Center, Inc. All rights reserved.
 */
#include <stdlib.h>

#include "qdl.h"
#include "vip.h"

struct qdl_device *qdl_init(enum QDL_DEVICE_TYPE type)
{
	if (type == QDL_DEVICE_USB)
		return usb_init();

	if (type == QDL_DEVICE_SIM)
		return sim_init();

	return NULL;
}

int qdl_vip_transfer_enable(struct qdl_device *qdl, const char *vip_table_path)
{
	return vip_transfer_init(qdl, vip_table_path);
}

void qdl_deinit(struct qdl_device *qdl)
{
	if (qdl->vip_transfer.state != VIP_DISABLED)
		vip_transfer_deinit(qdl);

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

int qdl_read(struct qdl_device *qdl, void *buf, size_t len, unsigned int timeout)
{
	return qdl->read(qdl, buf, len, timeout);
}

int qdl_write(struct qdl_device *qdl, const void *buf, size_t len)
{
	return qdl->write(qdl, buf, len);
}
