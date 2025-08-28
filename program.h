/* SPDX-License-Identifier: BSD-3-Clause */
#ifndef __PROGRAM_H__
#define __PROGRAM_H__

#include <sys/types.h>
#include <stdbool.h>
#include <stdint.h>
#include "list.h"

struct program {
	unsigned int pages_per_block;
	unsigned int sector_size;
	unsigned int file_offset;
	const char *filename;
	const char *label;
	unsigned int num_sectors;
	int partition;
	bool sparse;
	const char *start_sector;
	unsigned int last_sector;

	bool is_nand;
	bool is_erase;

	unsigned int sparse_chunk_type;
	uint32_t sparse_fill_value;
	off_t sparse_offset;

	const char *gpt_partition;

	struct list_head node;
};

struct qdl_device;

int program_load(const char *program_file, bool is_nand, bool allow_missing, const char *incdir);
int program_execute(struct qdl_device *qdl, int (*apply)(struct qdl_device *qdl, struct program *program, int fd));
int erase_execute(struct qdl_device *qdl, int (*apply)(struct qdl_device *qdl, struct program *program));
int program_find_bootable_partition(bool *multiple_found);
int program_is_sec_partition_flashed(void);
int program_cmd_add(const char *address, const char *filename);
int program_resolve_gpt_deferrals(struct qdl_device *qdl);

void free_programs(void);

#endif
