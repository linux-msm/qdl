/* SPDX-License-Identifier: BSD-3-Clause */
#ifndef __PATCH_H__
#define __PATCH_H__

#include "list.h"

struct qdl_device;

struct patch {
	unsigned int sector_size;
	unsigned int byte_offset;
	const char *filename;
	unsigned int partition;
	unsigned int size_in_bytes;
	const char *start_sector;
	const char *value;
	const char *what;

	struct list_head node;
};

int patch_load(const char *patch_file);
int patch_execute(struct qdl_device *qdl, int (*apply)(struct qdl_device *qdl, struct patch *patch));
void free_patches(void);

#endif
