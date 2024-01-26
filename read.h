#ifndef __READ_H__
#define __READ_H__

#include <stdbool.h>

struct qdl_device;

struct read_op {
	unsigned sector_size;
	const char *filename;
	unsigned partition;
	unsigned num_sectors;
	const char *start_sector;
	struct read_op *next;
};

int read_op_load(const char *read_op_file);
int read_op_execute(struct qdl_device *qdl,
					int (*apply)(struct qdl_device *qdl, struct read_op *read_op, int fd),
					const char *incdir);

#endif
