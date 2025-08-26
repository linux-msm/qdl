/* SPDX-License-Identifier: BSD-3-Clause */
#ifndef __PROGRAM_H__
#define __PROGRAM_H__

#include <sys/types.h>
#include <stdbool.h>
#include <stdint.h>
#include "qdl.h"

struct program {
	unsigned int pages_per_block;
	unsigned int sector_size;
	unsigned int file_offset;
	const char *filename;
	const char *label;
	unsigned int num_sectors;
	unsigned int partition;
	bool sparse;
	const char *start_sector;
	unsigned int last_sector;

	bool is_nand;
	bool is_erase;

	unsigned int sparse_chunk_type;
	uint32_t sparse_fill_value;
	off_t sparse_offset;

	struct program *next;
};

int program_load(const char *program_file, bool is_nand);
int program_execute(struct qdl_device *qdl, int (*apply)(struct qdl_device *qdl, struct program *program, int fd),
		    const char *incdir, bool allow_missing);
int erase_execute(struct qdl_device *qdl, int (*apply)(struct qdl_device *qdl, struct program *program));
int program_find_bootable_partition(bool *multiple_found);
int program_is_sec_partition_flashed(void);

void free_programs(void);

#endif
