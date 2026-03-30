/* SPDX-License-Identifier: BSD-3-Clause */
#ifndef __FIREHOSE_H__
#define __FIREHOSE_H__

#include <stdbool.h>
#include <stdint.h>
#include <sys/stat.h>

#include "list.h"

enum firehose_op_type {
	FIREHOSE_OP_NONE,
	FIREHOSE_OP_PROGRAM,
	FIREHOSE_OP_ERASE,
	FIREHOSE_OP_READ,
};

struct firehose_op {
	enum firehose_op_type type;
	struct list_head node;

	/* program, read */
	unsigned int sector_size;
	const char *filename;
	int partition;
	unsigned int num_sectors;
	const char *start_sector;
	const char *gpt_partition;

	/* program, erase */
	unsigned int pages_per_block;
	unsigned int file_offset;
	const char *label;
	bool sparse;
	unsigned int last_sector;
	bool is_nand;

	unsigned int sparse_chunk_type;
	uint32_t sparse_fill_value;
	off_t sparse_offset;
};

struct firehose_op *firehose_alloc_op(int type);
void firehose_free_ops(struct list_head *ops);

#endif
