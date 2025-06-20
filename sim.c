// SPDX-License-Identifier: BSD-3-Clause
/*
 * Copyright (c) 2025, Qualcomm Innovation Center, Inc. All rights reserved.
 */
#include <stdlib.h>
#include <string.h>

#include "sim.h"

struct qdl_device_sim {
	struct qdl_device base;
	struct vip_table_generator *vip_gen;
	bool create_digests;
};

static int sim_open(struct qdl_device *qdl, const char *serial)
{
	ux_info("This is a dry-run execution of QDL. No actual flashing has been performed\n");

	return 0;
}

static void sim_close(struct qdl_device *qdl)
{
	return;
}

static int sim_read(struct qdl_device *qdl, void *buf, size_t len, unsigned int timeout)
{
	return len;
}

static int sim_write(struct qdl_device *qdl, const void *buf, size_t len)
{
	return len;
}

static void sim_set_out_chunk_size(struct qdl_device *qdl, long size)
{
	return;
}

struct qdl_device *sim_init(void)
{
	struct qdl_device *qdl = malloc(sizeof(struct qdl_device_sim));

	if (!qdl)
		return NULL;

	memset(qdl, 0, sizeof(struct qdl_device_sim));

	qdl->dev_type = QDL_DEVICE_SIM;
	qdl->open = sim_open;
	qdl->read = sim_read;
	qdl->write = sim_write;
	qdl->close = sim_close;
	qdl->set_out_chunk_size = sim_set_out_chunk_size;

	return qdl;
}

struct vip_table_generator *sim_get_vip_generator(struct qdl_device *qdl)
{
	struct qdl_device_sim *qdl_sim;

	if (qdl->dev_type != QDL_DEVICE_SIM)
		return NULL;

	qdl_sim = container_of(qdl, struct qdl_device_sim, base);

	if (!qdl_sim->create_digests)
		return NULL;

	return qdl_sim->vip_gen;
}

bool sim_set_digest_generation(bool create_digests, struct qdl_device *qdl,
			       struct vip_table_generator *vip_gen)
{
	struct qdl_device_sim *qdl_sim;

	if (qdl->dev_type != QDL_DEVICE_SIM)
		return false;

	qdl_sim = container_of(qdl, struct qdl_device_sim, base);

	qdl_sim->create_digests = create_digests;
	qdl_sim->vip_gen = vip_gen;

	return true;
}
