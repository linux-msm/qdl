/*
 * Copyright (c) 2025, Qualcomm Innovation Center, Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors
 * may be used to endorse or promote products derived from this software without
 * specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
#include <stdlib.h>
#include <string.h>

#include "qdl.h"

struct qdl_device_sim
{
	struct qdl_device base;
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