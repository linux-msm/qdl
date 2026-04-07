/* SPDX-License-Identifier: BSD-3-Clause */
#ifndef __FIREHOSE_H__
#define __FIREHOSE_H__

#include <stdbool.h>
#include <stdint.h>
#include <sys/stat.h>

#include "list.h"
#include "qdl.h"

enum firehose_op_type {
	FIREHOSE_OP_NONE,
	FIREHOSE_OP_CONFIGURE,
	FIREHOSE_OP_PROGRAM,
	FIREHOSE_OP_ERASE,
	FIREHOSE_OP_READ,
	FIREHOSE_OP_PATCH,
	FIREHOSE_OP_SET_BOOTABLE,
};

struct firehose_op {
	enum firehose_op_type type;
	struct list_head node;

	/* program, read, patch, set_bootable */
	int partition;

	/* program, read, patch */
	unsigned int sector_size;
	struct qdl_zip *zip;
	const char *filename;
	const char *start_sector;

	/* program, read */
	unsigned int num_sectors;
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

	/* patch */
	unsigned int byte_offset;
	unsigned int size_in_bytes;
	const char *value;
	const char *what;

	/* configure */
	enum qdl_storage_type storage_type;
};

struct firehose_op *firehose_alloc_op(int type);
void firehose_free_ops(struct list_head *ops);

#endif
